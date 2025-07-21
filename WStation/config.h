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
const char* mqtt_topic_data = "";        // Inserire il topic MQTT su cui pubblicare/sottoscriversi
const char* mqtt_topic_error = "";       // Inserire il topic MQTT su cui pubblicare/sottoscriversi


// Configurazione per l'invio dei dati a PWS Weather
// Permette di inviare dati meteorologici alla piattaforma PWS Weather
const char* serverUrl = "";        // Inserire l'URL del server PWS Weather (esempio: https://pwsupdate.pwsweather.com/api/v1/submitwx?ID=XXX&PASSWORD=token123)


// Parametri aggiuntivi per la configurazione
int ALTITUDINE = ;               // Altitudine del dispositivo (metri)
const int DATA_READ_DELTA_MINUTES = ;  // Intervallo tra le letture dei dati (minuti)
static bool OFFLINE_MOD = ;      // Modalità offline per testare il sistema senza connessione


// Paramtri gestione pluviometro
const long double mlXReversal = ;                                           //ml d'acqua per svuotamento della vaschetta
const long double rainGaugeArea = ;                                         //Area della vasca di raccolta del pluviometro in decimetri quadrati
const long double equivalentH = ((mlXReversal / 1000) / rainGaugeArea) * 100;  //mm di acqua caduti per svuotamento
