/*
  Stazione Meteo ESP32 con Ethernet/WiFi
  Autore: Anesi Christian
  
  Funzionalità principali:
  - Lettura sensori SHT35 (Temp/Umidità) e BMP280 (Pressione)
  - Connessione duale Ethernet/WiFi con fallback automatico
  - Sincronizzazione orario via NTP
  - Invio dati a Blynk e servizio PWSWeather
  - Aggiornamento OTA via Blynk Air
  - Risparmio energetico con deep sleep
*/

#include "config.h"         // File di configurazione sensibile
#define BLYNK_PRINT Serial  // Abilita debug Blynk su Serial

#define BLYNK_FIRMWARE_VERSION "0.0.4"  // Versione firmware (incrementare ad ogni release)

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
#include <Update.h>      // Per OTA updates
#include <HTTPClient.h>  // Per HTTP requests

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
static bool OFFLINE_MOD = true; 

ArtronShop_SHT3x sht3x(0x44, &Wire);  // Sensore SHT35 (indirizzo I2C 0x44)
RTC_DS3231 rtc;                       // Orologio RTC
Adafruit_BMP280 bmp;                  // Sensore pressione BMP280

WiFiUDP ntpUDP;  // Client NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600);

// Variabili sensori
float pressure, temperature, humidity;
int RdLastMinutes;                       // Ultimo minuto di lettura
DateTime now;                            // Data/Ora corrente
const int DATA_READ_DELTA_MINUTES = 10;  // Intervallo letture (minuti)
float GRAD_TERMICO = 0.0065;
int ALTITUDINE = 1050;

#define uS_TO_S_FACTOR 1000000

// Prototipi funzioni
void onEvent(arduino_event_id_t event);
void updateRTC();
int lastSundayOfMonth(int year, int month);
void sendDataViaHTTP();
void enterDeepSleep();
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
      Serial.print("[WiFi] IP assegnato: ");
      Serial.println(WiFi.localIP());
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
  struct tm* timeInfo = localtime(&rawTime);

  // Gestione ora legale (Italia)
  int currentYear = timeInfo->tm_year + 1900;
  int currentMonth = timeInfo->tm_mon + 1;
  int currentDay = timeInfo->tm_mday;

  bool isDST = (currentMonth > 3 && currentMonth < 10) || (currentMonth == 3 && currentDay > lastSundayOfMonth(currentYear, 3)) || (currentMonth == 10 && currentDay <= lastSundayOfMonth(currentYear, 10));

  if (isDST) {
    timeInfo->tm_hour += 1;  // Aggiungi 1 ora per CEST
  }

  // Aggiorna RTC
  rtc.adjust(DateTime(
    timeInfo->tm_year + 1900,
    timeInfo->tm_mon + 1,
    timeInfo->tm_mday,
    timeInfo->tm_hour,
    timeInfo->tm_min,
    timeInfo->tm_sec));
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
  } while (!success && retry < 3);
}

/*==============================================================================
  GESTIONE RISPARMIO ENERGETICO
==============================================================================*/
void enterDeepSleep() {
  now = rtc.now();

  // Calcola secondi fino alla prossima lettura
  int secondsToNextRead = (DATA_READ_DELTA_MINUTES - (now.minute() % DATA_READ_DELTA_MINUTES)) * 60;
  secondsToNextRead -= now.second();  // Sottrai secondi già passati

  if (secondsToNextRead <= 0) {
    secondsToNextRead += DATA_READ_DELTA_MINUTES * 60;
  }

  // Formattazione tempo rimanente
  int hours = secondsToNextRead / 3600;
  int minutes = (secondsToNextRead % 3600) / 60;
  int seconds = secondsToNextRead % 60;

  Serial.print("\n[Sleep] Entro in deep sleep per ");
  if (hours > 0) Serial.print(hours + "h ");
  if (minutes > 0) Serial.print(minutes + "m ");
  if (seconds > 0) Serial.print(seconds + "s");
  Serial.println();

  // Configura e avvia deep sleep
  esp_sleep_enable_timer_wakeup(secondsToNextRead * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

/*==============================================================================
  UTILITIES
==============================================================================*/
void timeStamp() {
  now = rtc.now();
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
  delay(1000);  // Attesa stabilizzazione seriale

  Serial.println("\n\n=== INIZIALIZZAZIONE SISTEMA ===");

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
    Serial.println("[NET] Fallback a WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    startTime = millis();
    while (!wifi_connected && (millis() - startTime < 10000)) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(wifi_connected ? "\n[WiFi] Connesso" : "\n[WiFi] Fallito");
  }

  // Inizializzazione sensori
  while (!sht3x.begin()) {
    Serial.println("[SHT35] Sensore non rilevato!");
    delay(1000);
  }

  if (!bmp.begin(0x76)) {
    Serial.println("[BMP280] Sensore non rilevato!");
    while (1) delay(1000);
  }

  // Configurazione RTC
  rtc.begin();
  timeClient.begin();

  if (rtc.lostPower()) {
    Serial.println("[RTC] Batteria scarica, sincronizzo con NTP");
    do {
      updateRTC();
      now = rtc.now();
      delay(2000);
    } while (now.year() <= 2018);
  }

  // Connessione a Blynk
  if (eth_connected || wifi_connected) {
    Blynk.config(BLYNK_AUTH_TOKEN);
    if (!Blynk.connect(3000)) {
      Serial.println("[Blynk] Connessione fallita");
    }
  }

  timeStamp();
  RdLastMinutes = now.minute() - 1;  // Forza prima lettura
  Serial.println("\n=== SISTEMA PRONTO ===");
}

/*==============================================================================
  LOOP PRINCIPALE
==============================================================================*/
void loop() {
  now = rtc.now();

  if ((now.minute() % DATA_READ_DELTA_MINUTES == 0) && (RdLastMinutes != now.minute())) {
    Serial.println("\n\n=== NUOVA LETTURA ===");
    timeStamp();
    Serial.println("Firmware: " + String(BLYNK_FIRMWARE_VERSION));

    // Lettura sensori
    if (sht3x.measure()) {
      temperature = sht3x.temperature();
      humidity = sht3x.humidity();
      pressure = bmp.readPressure() / 100.0F;  // Converti a hPa

      // Compensazione altitudine
      pressure = pressure * pow((1 - ((GRAD_TERMICO * ALTITUDINE) / (temperature + 273.15 + (GRAD_TERMICO * ALTITUDINE)))), -5.257);

      // Stampa valori
      Serial.printf("Temperatura: %.1f °C\n", temperature);
      Serial.printf("Umidità: %.1f %%\n", humidity);
      Serial.printf("Pressione: %.1f hPa\n", pressure);

      // Invio dati
      if ((eth_connected || wifi_connected) && !OFFLINE_MOD) {
        Blynk.virtualWrite(V0, temperature);
        Blynk.virtualWrite(V1, humidity);
        Blynk.virtualWrite(V2, pressure);
        sendDataViaHTTP();
      } else {
        Serial.println("[NET] Nessuna connessione per l'invio");
      }

      RdLastMinutes = now.minute();
    } else {
      Serial.println("[SHT35] Errore lettura");
    }

    Serial.println("======================");
  } else {
    enterDeepSleep();
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