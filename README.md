# Victron Solar Display ESP32

> 🙏 **Fork di [wytr/VictronSolarDisplayEsp](https://github.com/wytr/VictronSolarDisplayEsp/tree/dev)**  
> Progetto originale di [@wytr](https://github.com/wytr) per ESP32-S3 + LVGL.  
> Questa versione è stata semplificata e portata su ESP32 standard (Freenove FNK0103S) con driver display diretto.
>
> ⚠️ **Nota:** Le chiavi AES di decodifica sono hardcoded nel sorgente per semplicità.  
> Non è elegante, ma è dannatamente comodo! 😎

Display compatto per monitoraggio dispositivi Victron Energy via Bluetooth Low Energy (BLE).

## 📋 Panoramica

Questo progetto implementa un display standalone per visualizzare in tempo reale i dati dei dispositivi Victron Energy:

- **SmartSolar MPPT** - Regolatore di carica solare
- **SmartBatterySense** - Sensore di tensione e temperatura batteria  
- **SmartShunt** - Monitor batteria completo (opzionale, chiave da configurare)

### Caratteristiche

- ✅ Display diretto ST7796 via SPI (no LVGL)
- ✅ Scansione BLE passiva continua
- ✅ Decodifica AES-CTR dei dati Victron
- ✅ Chiavi AES hardcoded (no WiFi/captive portal)
- ✅ Layout ottimizzato per 3 dispositivi
- ✅ Aggiornamento ogni secondo
- ✅ Footprint memoria ridotto (~200KB app)

## 🔧 Hardware Supportato

### Display Target
- **Freenove ESP32 Display** (FNK0103S)
- MCU: ESP32-WROOM-32E
- Display: 4.0" ST7796S 320x480 SPI
- Touch: XPT2046 (non utilizzato)
- Flash: 4MB (no PSRAM)

### Pinout Display

| Funzione | GPIO |
|----------|------|
| MOSI | 13 |
| MISO | 12 |
| SCLK | 14 |
| CS (LCD) | 15 |
| DC | 2 |
| Backlight | 27 |
| RST | - (non connesso) |

## 📡 Dispositivi Victron Supportati

### 1. SmartSolar MPPT (Record Type 0x01)

Regolatori di carica solari della serie SmartSolar.

**Dati visualizzati:**
- Potenza PV (W) - valore principale
- Corrente di carica (A)
- Tensione batteria (V)
- Stato (OFF/BULK/ABSORB/FLOAT/etc.)
- Yield giornaliero (kWh)

### 2. SmartBatterySense (Record Type 0x02, PID 0xA3A4/0xA3A5)

Sensore wireless di tensione e temperatura batteria.

**Dati visualizzati:**
- Temperatura batteria (°C) - valore principale
- Tensione batteria (V)

⚠️ **Nota:** Il SmartBatterySense usa lo stesso record type del Battery Monitor (0x02) ma trasmette solo tensione e temperatura. Gli altri campi (SOC, corrente, TTG) sono N/A e vengono ignorati.

### 3. SmartShunt (Record Type 0x02, PID 0xA389-0xA38B)

Monitor batteria completo con shunt di corrente.

**Dati visualizzati:**
- SOC (%) - valore principale
- Tensione batteria (V)
- Corrente (A)
- Time To Go (h:mm)
- Ah consumati

## 🔑 Configurazione Chiavi AES

Le chiavi AES sono hardcoded nel file `components/victron_ble/victron_ble.c`:

```c
// MPPT SmartSolar
static uint8_t aes_key_mppt[16] = {
    0xf2, 0xdc, 0xc3, 0xba, 0x40, 0xed, 0xb8, 0xde,
    0x7e, 0x07, 0xd7, 0x63, 0x8f, 0x13, 0xf9, 0x71
};

// SmartBatterySense
static uint8_t aes_key_batt[16] = {
    0xb7, 0xab, 0xe1, 0x9c, 0x00, 0x32, 0x40, 0xbe,
    0x9d, 0xae, 0x89, 0xb8, 0xc3, 0x72, 0xdd, 0x43
};

// SmartShunt (placeholder - inserire chiave reale)
static uint8_t aes_key_smartshunt[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static bool has_smartshunt_key = false;  // Impostare a true quando si aggiunge la chiave
```

### Come ottenere le chiavi

1. Aprire l'app **VictronConnect** sul telefono
2. Selezionare il dispositivo
3. Andare in ⚙️ **Impostazioni** → **Info prodotto**
4. Abilitare **Instant Readout**
5. Copiare la **Encryption Key** (32 caratteri hex)

### Formato chiave

La chiave nell'app è in formato hex string (es. `f2dcc3ba40edb8de7e07d7638f13f971`).

Convertire in array di byte:
```
f2dcc3ba... → 0xf2, 0xdc, 0xc3, 0xba, ...
```

## 🏗️ Struttura Progetto

```
VictronSolarDisplayEsp/
├── main/
│   ├── main_simple.c      # Entry point e logica UI
│   ├── simple_display.c   # Driver display ST7796 SPI
│   ├── simple_display.h   # API display e colori
│   ├── idf_component.yml  # Dipendenze componenti
│   └── CMakeLists.txt     # Build configuration
├── components/
│   └── victron_ble/
│       ├── victron_ble.c      # Scanner BLE e decoder AES
│       ├── victron_ble.h      # API pubblica
│       ├── victron_products.c # Database nomi prodotti
│       ├── victron_products.h # Product IDs
│       └── victron_records.h  # Strutture dati record
├── docs/
│   └── extra-manufacturer-data-2022-12-14.txt  # Spec Victron BLE
├── CMakeLists.txt         # Root build file
├── partitions.csv         # Partition table
└── sdkconfig              # ESP-IDF configuration
```

## 🔨 Compilazione

### Prerequisiti

- ESP-IDF v5.5.x
- Python 3.11+

### Build

```bash
# Dalla ESP-IDF Command Prompt
cd VictronSolarDisplayEsp
idf.py build
```

### Flash

```bash
idf.py -p COM6 flash monitor
```

### Pulizia build

```bash
idf.py fullclean
```

## 📺 Layout Display

Il display è diviso in 3 sezioni verticali (~160px ciascuna):

```
┌─────────────────────────────────────┐
│ MPPT SOLAR CHARGER           (--)   │
│   ████ W          FLOAT             │
│   ██.█ A          13.32V            │
│   Today: 0.45 kWh                   │
├─────────────────────────────────────┤
│ BATTERY SENSE                (--)   │
│   ██.█°C                            │
│   ██.██ V                           │
│   Battery OK                        │
├─────────────────────────────────────┤
│ SMARTSHUNT               (no key)   │
│   ███ %           13.32V            │
│   +█.██ A         TTG:--h--m        │
│   Used: 0.0Ah                       │
└─────────────────────────────────────┘
```

**Legenda indicatori:**
- `(--)` = Nessun dato ricevuto
- `(no key)` = Chiave AES non configurata

## 🎨 Schema Colori

| Elemento | Colore | Hex RGB565 |
|----------|--------|------------|
| Titoli sezione | Giallo | 0xFFE0 |
| Valori principali | Verde | 0x07E0 |
| Tensione | Cyan | 0x07FF |
| Corrente negativa | Arancione | 0xFD20 |
| Errori/warning | Rosso | 0xF800 |
| Testo normale | Bianco | 0xFFFF |
| Sfondo | Nero | 0x0000 |

## 🔍 Debug

### Log seriale

Il firmware produce log dettagliati sulla porta seriale (115200 baud):

```
I (1234) VICTRON: MPPT: 13.32V 2.1A 28W
I (2345) victron_ble: === Battery Monitor (PID=0xA3A4) ===
I (2346) victron_ble: Vbat=13.32V Ibat=-0.001A SOC=102.3% TTG=65535 min
I (2347) victron_ble: Aux_mode=2 Aux_val=29565 (295.65K = 22.50C)
```

### Abilitare debug verbose BLE

In `main_simple.c`, dopo `victron_ble_init()`:
```c
victron_ble_set_debug(true);
```

## 📝 Note Tecniche

### Protocollo Victron BLE

I dispositivi Victron trasmettono advertisement BLE con:
- Vendor ID: `0x02E1` (Victron Energy)
- Record Type: `0x10` (Product Advertisement)
- Dati crittografati AES-CTR con nonce a 16 bit

### Parsing SmartBatterySense

Il SmartBatterySense usa il record type Battery Monitor (0x02) ma:
- Product ID: `0xA3A4` o `0xA3A5`
- Solo `voltage` e `aux_value` (temperatura) sono validi
- `aux_input` deve essere `2` per temperatura
- Temperatura in Kelvin × 100: `temp_C = (aux_value / 100.0) - 273.15`
- Tutti gli altri campi (SOC, current, TTG, consumed) sono N/A

### Selezione chiave automatica

Il sistema seleziona la chiave AES corretta confrontando il byte `encryptKeyMatch` nell'advertisement con il primo byte di ogni chiave configurata:
- Se `encryptKeyMatch == aes_key_mppt[0]` → usa chiave MPPT
- Se `encryptKeyMatch == aes_key_batt[0]` → usa chiave Battery
- Se `encryptKeyMatch == aes_key_smartshunt[0]` → usa chiave SmartShunt

## 📜 Licenza

MIT License - Vedi file LICENSE

## � Risorse

- Documentazione protocollo BLE: Victron Energy
- Libreria Python victron-ble: [keshavdv/victron-ble](https://github.com/keshavdv/victron-ble)

---

**Versione:** 2.0.0-simple  
**Data:** Dicembre 2024  
**Target:** Freenove ESP32 Display (FNK0103S)
