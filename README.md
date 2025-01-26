# Weather Station ESP32 OLIMEX POE ISO IND 🌦️

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Progetto di una stazione meteorologica IoT basata su ESP32 OLIMEX POE con:
- Lettura di temperatura, umidità e pressione
- Alimentazione PoE integrata
- Monitoraggio ambientale professionale alla portata di tutti

![OLIMEX POE ISO IND](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/resources/ESP32-POE-ISO-03.jpg)

## Caratteristiche Principali ✨
- 🔌 Alimentazione via PoE (IEEE 802.3af)
- 🛡️ Isolamento galvanico 2KV
- 🌡️ Sensore SHT35 per temperatura/umidità
- ⏲️ RTC DS3231 con batteria tampone
- 📡 Connettività Ethernet 10/100 Mbps
- 📊 Invio dati a Blynk e PWSWeather

## Hardware Necessario 🛠️
| Componente | Quantità | Note |
|------------|----------|------|
| OLIMEX ESP32-POE-ISO | 1 | [Scheda tecnica](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/) |
| Sensore SHT35 | 1 | Range: -40°C ~ +125°C |
| Modulo BMP280 | 1 | Range pressione: 300-1100 hPa |
| RTC DS3231 | 1 | Precisione ±2ppm |

## Installazione 💻
1. **Configurazione Arduino IDE:**
   - Aggiungi l'URL per ESP32 in *File > Preferenze*:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Installa il package "ESP32" via *Strumenti > Board > Board Manager*
   - Seleziona: `ESP32 Dev Module`

2. **Collegamenti HW:**
   ```plaintext
   OLIMEX POE ISO <-> Sensori
   ---------------------------
   GPIO13 (SDA2)  -> SDA Sensori
   GPIO16 (SCL2)  -> SCL Sensori
   3.3V           -> VCC Sensori
   GND            -> GND Sensori

## Configurazione ⚙️
  Crea config.h nella cartella principale con:
  
  ```
  #define BLYNK_TEMPLATE_ID "XXXX"
  #define BLYNK_AUTH_TOKEN "YYYY"
  #define WIFI_SSID ""
  #define WIFI_PASS ""
  const char* serverUrl = "https://pwsupdate.pwsweather.com/api/v1/submitwx?ID=TUO_ID&PASSWORD=TUA_PASS";
  ```

## Utilizzo 🖥️
1. **Alimentazione:**
    - Collegare cavo Ethernet con PoE
    - Verifica LED di alimentazione (LED rosso acceso)

2. **Monitor Seriale (115200 baud):**
    - Per verificare il corretto funzionamento

3. **Verifica Dati Online:**
    - Blynk: Controlla la dashboard associata al tuo template
    - PWSWeather: Verifica i dati sul portale
