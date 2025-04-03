// Configurazione delle credenziali per Blynk
// Questi parametri sono necessari per connettere il dispositivo alla piattaforma Blynk
#define BLYNK_TEMPLATE_ID ""       // Inserire l'ID del template Blynk
#define BLYNK_TEMPLATE_NAME ""     // Inserire il nome del template Blynk
#define BLYNK_AUTH_TOKEN ""        // Inserire il token di autenticazione Blynk

// Configurazione delle credenziali WiFi
// Utilizzate per connettere il dispositivo alla rete WiFi
#define WIFI_SSID ""               // Inserire il nome della rete WiFi (SSID)
#define WIFI_PASS ""               // Inserire la password della rete WiFi

// Impostazioni per la connessione al broker MQTT
// Questi parametri permettono al dispositivo di comunicare tramite il protocollo MQTT
const char* mqtt_server = "";       // Inserire l'indirizzo del server MQTT
const int mqtt_port = ;              // Inserire la porta del server MQTT (tipicamente 1883 per connessioni non sicure)
const char* mqtt_topic = "";        // Inserire il topic MQTT su cui pubblicare/sottoscriversi

// Configurazione per l'invio dei dati a PWS Weather
// Permette di inviare dati meteorologici alla piattaforma PWS Weather
const char* serverUrl = "";        // Inserire l'URL del server PWS Weather (esempio: https://pwsupdate.pwsweather.com/api/v1/submitwx?ID=XXX&PASSWORD=token123)
