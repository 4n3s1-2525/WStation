/*
  Stazione Meteo ESP32 con Ethernet/WiFi
  Autore: Anesi Christian
  Versione con aggiunta feature MQTT

  Funzionalità principali:
  - Lettura sensori SHT35 (Temp/Umidità) e BMP280 (Pressione)
  - Connessione duale Ethernet/WiFi con fallback automatico
  - Sincronizzazione orario via NTP
  - Invio dati a Blynk e servizio PWSWeather
  - Aggiornamento OTA via Blynk Air
  - Risparmio energetico con deep sleep
  - Invio dati in formato JSON a server MQTT Mosquitto
*/

#include "config.h"         // File di configurazione sensibile
#define BLYNK_PRINT Serial  // Abilita debug Blynk su Serial

#define BLYNK_FIRMWARE_VERSION "0.2.1"  // Versione firmware (incrementare ad ogni release)

// Librerie principali
#include <BlynkSimpleEsp32.h>
#include <Arduino.h>
#include <Wire.h>
#include <ArtronShop_SHT3x.h>
#include <RTClib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <WiFi.h>
#include <Update.h>        // Per OTA updates
#include <HTTPClient.h>    // Per HTTP requests
#include <PubSubClient.h>  // Per MQTT

// Configurazione hardware Ethernet (specifica per OLIMEX POE ISO)
#ifndef ETH_PHY_TYPE
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 0
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN
#endif
#include <ETH.h>

// Variabili globali e oggetti
static bool eth_connected = false;   // Stato connessione Ethernet
static bool wifi_connected = false;  // Stato connessione WiFi
static bool rtc_connected = false;   // Stato connessione modulo RTC

ArtronShop_SHT3x sht3x(0x44, &Wire);  // Sensore SHT35 (indirizzo I2C 0x44)
RTC_DS3231 rtc;                       // Orologio RTC
Adafruit_BMP280 bmp;                  // Sensore pressione BMP280
#define RAINGAUGE_PIN 4               //Pluviometro

float WaterMm = 0;
float WaterMmDaily = 0;

WiFiUDP ntpUDP;  // Client NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600);

// Oggetti per MQTT (utilizziamo WiFiClient per entrambe le connessioni)
WiFiClient mqttWiFiClient;
PubSubClient mqttClient(mqttWiFiClient);

// Variabili sensori
float pressure, temperature, humidity;
int RdLastMinutes, LastDay;  // Ultimo minuto di lettura e ultimo giorno
DateTime now;                // Data/Ora corrente
float GRAD_TERMICO = 0.0065;


#define uS_TO_S_FACTOR 1000000

// Prototipi funzioni
void onEvent(arduino_event_id_t event);
void updateRTC();
int lastSundayOfMonth(int year, int month);
void sendDataViaHTTP();
void sendDataViaMQTT();
void enterDeepSleep();
void updateLocalTime();
void timeStamp();
void reboot();

/*==============================================================================
  GESTIONE EVENTI DI RETE
==============================================================================*/
void onEvent(arduino_event_id_t event) {
  switch (event) {
    // Eventi Ethernet
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] Inizializzazione interfaccia");
      ETH.setHostname("esp32-ethernet");  // Imposta hostname
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] Collegamento fisico attivo");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("[ETH] Indirizzo IP assegnato");
      Serial.println(ETH.fullDuplex() ? "Full Duplex" : "Half Duplex");
      Serial.print("Speed: ");
      Serial.println(ETH.linkSpeed());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] Collegamento fisico perso");
      eth_connected = false;
      break;

    // Eventi WiFi
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifi_connected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnesso");
      wifi_connected = false;
      break;

    default: break;
  }
}

/*==============================================================================
  SINCRONIZZAZIONE ORARIO
==============================================================================*/
void updateRTC() {
  if (!timeClient.update()) {
    Serial.println("[NTP] Aggiornamento orario fallito");
    return;
  }

  time_t rawTime = timeClient.getEpochTime();
  struct tm timeInfo;
  localtime_r(&rawTime, &timeInfo);


  // Gestione ora legale (Italia)
  int currentYear = timeInfo.tm_year + 1900;
  int currentMonth = timeInfo.tm_mon + 1;
  int currentDay = timeInfo.tm_mday;

  Serial.printf("[NTP] %04d-%02d-%02d %02d:%02d:%02d\n",
                currentYear, currentMonth, currentDay,
                timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

  if ((currentMonth > 3 && currentMonth < 10) ||                                  // È tra aprile e settembre, quindi l'ora legale è attiva
      (currentMonth == 3 && currentDay > lastSundayOfMonth(currentYear, 3)) ||    // È dopo l'ultima domenica di marzo
      (currentMonth == 10 && currentDay < lastSundayOfMonth(currentYear, 10))) {  // È prima dell'ultima domenica di ottobre
    timeInfo.tm_hour += 1;                                                        // Aggiungi 1 ora per l'ora legale
  }

  // Aggiorna RTC
  rtc.adjust(DateTime(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec));

  timeStamp();
}

// Calcola l'ultima domenica del mese per DST
int lastSundayOfMonth(int year, int month) {
  int daysInMonth = 31;
  if (month == 4 || month == 6 || month == 9 || month == 11) daysInMonth = 30;
  else if (month == 2) daysInMonth = (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0)) ? 29 : 28;

  struct tm tm = { 0 };
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = daysInMonth;
  time_t t = mktime(&tm);

  while (localtime(&t)->tm_wday != 0) {
    t -= 86400;  // Decrementa di un giorno
  }
  return localtime(&t)->tm_mday;
}

/*==============================================================================
  INVIO DATI A SERVIZI ESTERNI
==============================================================================*/
void sendDataViaHTTP() {
  if (!(eth_connected || wifi_connected)) {
    Serial.println("[HTTP] Nessuna connessione disponibile");
    return;
  }

  // Costruzione URL per PWSWeather
  String url = String(serverUrl);
  url += "&dateutc=" + String(now.year()) + "-"
         + String(now.month()) + "-"
         + String(now.day()) + "+"
         + String(now.hour()) + ":"
         + String(now.minute()) + ":00";

  if (temperature != 999) url += "&tempf=" + String(temperature * 9 / 5 + 32, 1);
  url += "&rainin=" + String(WaterMm, 3);
  url += "&dailyrainin=" + String(WaterMmDaily, 3);
  if (pressure > 0) url += "&baromin=" + String(pressure * 0.02952998, 4);
  if (humidity != 999) url += "&humidity=" + String(humidity, 1);
  url += "&softwaretype=WStation&action=updateraw";

  // Invio con ritentativi
  HTTPClient http;
  int retry = 0;
  bool success = false;

  do {
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[HTTP] Dati inviati correttamente");
      success = true;
    } else {
      Serial.print("[HTTP] Errore: ");
      Serial.println(http.errorToString(httpCode));
      delay(1000);
      retry++;
    }

    http.end();
    delay(1000);
  } while (!success && retry < 5);
}

// Nuova funzione per invio dati via MQTT in formato JSON
void sendDataViaMQTT() {
  if (!(eth_connected || wifi_connected)) {
    Serial.println("[MQTT] Nessuna connessione disponibile");
    return;
  }

  // Imposta il server MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);

  // Connessione se non già connessi
  if (!mqttClient.connected()) {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("[MQTT] Connesso al broker");
    } else {
      Serial.print("[MQTT] Connessione fallita, rc=");
      Serial.println(mqttClient.state());
      return;
    }
  }

  // Creazione del payload in formato JSON
  String payload = "{";
  payload += "\"temperature\":" + String(temperature, 1) + ",";
  payload += "\"humidity\":" + String(humidity, 1) + ",";
  payload += "\"pressure\":" + String(pressure, 1) + ",";
  payload += "\"watermm\":" + String(WaterMm, 3) + ",";
  payload += "\"watermmdaily\":" + String(WaterMmDaily, 3);
  payload += "}";

  // Pubblica i dati sul topic definito
  if (mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("[MQTT] Dati pubblicati correttamente");
  } else {
    Serial.println("[MQTT] Errore pubblicazione dati");
  }

  mqttClient.disconnect();
}

/*==============================================================================
  GESTIONE RISPARMIO ENERGETICO
==============================================================================*/
void enterDeepSleep() {
  updateLocalTime();

  // Calcola secondi fino alla prossima lettura
  int secondsToNextRead = (DATA_READ_DELTA_MINUTES - (now.minute() % DATA_READ_DELTA_MINUTES)) * 60 - now.second();

  if (secondsToNextRead <= 0) {
    secondsToNextRead += DATA_READ_DELTA_MINUTES * 60;
  }

  // Formattazione tempo rimanente
  int hours = secondsToNextRead / 3600;
  int minutes = (secondsToNextRead % 3600) / 60;
  int seconds = secondsToNextRead % 60;

  Serial.print("\n[Sleep] Entro in deep sleep per ");
  if (hours > 0) {
    Serial.print(hours);
    Serial.print("h ");
  }
  if (minutes > 0) {
    Serial.print(minutes);
    Serial.print("m ");
  }
  if (seconds > 0) {
    Serial.print(seconds);
    Serial.print("s");
  }
  Serial.println();

  Serial.flush();

  // Configura e avvia deep sleep
  esp_sleep_enable_timer_wakeup(secondsToNextRead * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

/*==============================================================================
  UTILITIES
==============================================================================*/
void updateLocalTime() {
  if (rtc_connected) {
    now = rtc.now();
  } else {
    if (!timeClient.update()) {
      Serial.println("[NTP] Aggiornamento orario fallito");
      return;
    }

    time_t rawTime = timeClient.getEpochTime();
    struct tm timeInfo;
    localtime_r(&rawTime, &timeInfo);

    // Estrae anno, mese e giorno
    int currentYear = timeInfo.tm_year + 1900;
    int currentMonth = timeInfo.tm_mon + 1;
    int currentDay = timeInfo.tm_mday;

    // Regole per l'ora legale in Italia:
    // - Tra aprile e settembre, l'ora legale è attiva.
    // - In marzo, se siamo dopo l'ultima domenica, si applica l'ora legale.
    // - In ottobre, se siamo prima dell'ultima domenica, si applica l'ora legale.
    if ((currentMonth > 3 && currentMonth < 10) || (currentMonth == 3 && currentDay > lastSundayOfMonth(currentYear, 3)) || (currentMonth == 10 && currentDay < lastSundayOfMonth(currentYear, 10))) {
      timeInfo.tm_hour += 1;  // Aggiunge 1 ora per l'ora legale
    }

    // Converte nuovamente la struttura tm in time_t
    rawTime = mktime(&timeInfo);
    now = DateTime(rawTime);
  }
}

void timeStamp() {
  updateLocalTime();
  Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
}

void reboot() {
  Serial.println("\n[System] Riavvio in corso...");
  delay(100);
  ESP.restart();
}

/*==============================================================================
  SETUP INIZIALE
==============================================================================*/
void setup() {
  Serial.begin(115200);
  delay(2000);  // Attesa stabilizzazione seriale

  Serial.println("\n\n==== INIZIALIZZAZIONE SISTEMA ====");

  // Configurazione interfacce di rete
  Network.onEvent(onEvent);
  WiFi.onEvent(onEvent);
  Wire.begin();

  // Tentativo connessione Ethernet
  unsigned long startTime = millis();
  do {
    ETH.begin();
    delay(2000);
    if (eth_connected) break;
  } while (millis() - startTime < 10000);

  // Fallback a WiFi se Ethernet non disponibile
  if (!eth_connected) {
    Serial.print("[NET] Fallback a WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    startTime = millis();
    while (!wifi_connected && (millis() - startTime < 20000)) {
      delay(500);
      Serial.print(".");
    }
    if (wifi_connected) {
      Serial.println("\n[WiFi] Connesso");
      Serial.println("[WiFi] IP assegnato: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n[WiFi] Fallito");
    }
  }

  // Inizializzazione sensori
  if (!sht3x.begin()) {
    Serial.println("[SHT35] Sensore non rilevato!");
    delay(1000);
    reboot();
  } else {
    Serial.println("[SHT35] Sensore configurato");
  }

  if (!bmp.begin(0x76)) {
    Serial.println("[BMP280] Sensore non rilevato!");
    delay(1000);
    reboot();
  } else {
    Serial.println("[BMP280] Sensore configurato");
  }

  // Configurazione RTC
  if (!rtc.begin()) {
    Serial.println("[RTC] Sensore non rilevato!");
    rtc_connected = false;
    if (!wifi_connected && !eth_connected) {
      Serial.println("[RTC] Impossbile passare all'orario NTP, internet assente!");
      delay(1000);
      reboot();
    }
  } else {
    Serial.println("[RTC] Sensore configurato");
    rtc_connected = true;
  }
  timeClient.begin();

  if ((eth_connected || wifi_connected) && rtc_connected) {
    if (rtc.lostPower() && now.year() <= 2018) {
      Serial.println("[RTC] Batteria scarica, sincronizzo con NTP");
      do {
        updateRTC();
        now = rtc.now();
        delay(2000);
      } while (now.year() <= 2018);
    } else {
      updateRTC();
      now = rtc.now();
    }
  } else if (!rtc_connected) {
    Serial.println("[RTC] Passaggio all'orario NTP");
  } else {
    Serial.println("[RTC] Impossibile aggiornare l'ora, connessione a internet assente!");
  }


  // Connessione a Blynk
  if (eth_connected || wifi_connected) {
    Blynk.config(BLYNK_AUTH_TOKEN);
    if (!Blynk.connect(3000)) {
      Serial.println("[Blynk] Connessione fallita");
    }
  }

  LastDay = now.day();
  RdLastMinutes = now.minute() - 1;  // Forza prima lettura
  Serial.println("\n==== SISTEMA PRONTO ====");
}

/*==============================================================================
  LOOP PRINCIPALE
==============================================================================*/
void loop() {

  Blynk.run();

  if (digitalRead(RAINGAUGE_PIN)) {
    WaterMm += equivalentH;
    WaterMmDaily += equivalentH;
    delay(100);
  }

  updateLocalTime();

  if ((now.minute() % DATA_READ_DELTA_MINUTES == 0) && (RdLastMinutes != now.minute())) {
    Serial.println("\n\n==== NUOVA LETTURA ====");
    timeStamp();
    Serial.println("Firmware: " + String(BLYNK_FIRMWARE_VERSION));

    // Lettura sensori
    if (sht3x.measure()) {
      temperature = sht3x.temperature();
      humidity = sht3x.humidity();

      if (temperature < 100) {
        Serial.printf("Temperatura: %.1f °C\n", temperature);
      } else {
        Serial.printf("Temperatura: Err");
      }

      if (humidity <= 100) {
        Serial.printf("Umidità: %.1f %%\n", humidity);
      } else {
        Serial.printf("Umidità: Err");
      }

      int i = 0;
      do {
        if (i != 0 && !bmp.begin(0x76)) {
          Serial.println("[BMP280] Sensore non rilevato!");
          delay(1000);
          reboot();
        } else {
          Serial.println("[BMP280] Sensore ri-configurato");
        }
        pressure = bmp.readPressure() / 100.0F;                                                                                         // Converti a hPa
        pressure = pressure * pow((1 - ((GRAD_TERMICO * ALTITUDINE) / (temperature + 273.15 + (GRAD_TERMICO * ALTITUDINE)))), -5.257);  // Compensazione altitudine
        i++;
      } while ((pressure < 900 || pressure > 1100) && i <= 5);

      if (i <= 5) {
        Serial.printf("Pressione: %.1f hPa\n", pressure);
      } else {
        Serial.println("Pressione: Err");
      }

      Serial.printf("Pioggia: %.3f mm\n", WaterMm);

      Serial.printf("Pioggia giornaliera: %.3f mm\n", WaterMmDaily);


      Serial.println("------------------------");

      // Invio dati ai vari servizi
      if ((eth_connected || wifi_connected) && !OFFLINE_MOD) {
        Blynk.virtualWrite(V0, temperature);
        Blynk.virtualWrite(V1, humidity);
        Blynk.virtualWrite(V2, pressure);
        sendDataViaHTTP();
        sendDataViaMQTT();
      } else if (OFFLINE_MOD) {
        Serial.println("[NET] Offline mode attiva, dati non inviati");
      } else {
        Serial.println("[NET] Nessuna connessione per l'invio");
      }

      RdLastMinutes = now.minute();
    } else {
      Serial.println("[SHT35] Errore lettura");
    }

    WaterMm = 0;
    if (now.day() != LastDay) {
      WaterMmDaily = 0;
      LastDay = now.day();
    }

    Serial.println("========================");

  } else {
    //enterDeepSleep();
  }
}

/*==============================================================================
  GESTIONE OTA
==============================================================================*/
BLYNK_WRITE(InternalPinOTA) {
  Serial.println("\n[OTA] Avvio aggiornamento firmware");
  String url = param.asString();

  HTTPClient http;
  http.begin(url);

  if (http.GET() == HTTP_CODE_OK) {
    int len = http.getSize();
    if (len > 0 && Update.begin(len)) {
      Client& client = http.getStream();

      if (Update.writeStream(client) == len && Update.end()) {
        Serial.println("[OTA] Aggiornamento completato!");
        reboot();
      }
    }
  }
  http.end();
  Serial.println("[OTA] Aggiornamento fallito");
}
