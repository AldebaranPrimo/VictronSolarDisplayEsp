# Victron Solar Display ESP32

> 🙏 **Fork of [wytr/VictronSolarDisplayEsp](https://github.com/wytr/VictronSolarDisplayEsp/tree/dev)**  
> Original project by [@wytr](https://github.com/wytr) for ESP32-S3 + LVGL.  
> This version has been simplified and ported to standard ESP32 (Freenove FNK0103S) with direct display driver.
>
> ⚠️ **Note:** AES decryption keys are hardcoded in the source for simplicity.  
> Not elegant, but damn convenient! 😎

Compact display for real-time monitoring of Victron Energy devices via Bluetooth Low Energy (BLE).

![Display Screenshot](docs/images/display_screenshot.jpg)

## 📋 Overview

This project implements a standalone display to show real-time data from Victron Energy devices:

- **SmartSolar MPPT** - Solar charge controller
- **SmartBatterySense** - Battery voltage and temperature sensor  
- **SmartShunt** - Full battery monitor (optional, key needs configuration)

### Features

- ✅ Direct ST7796 display via SPI (no LVGL)
- ✅ Continuous passive BLE scanning
- ✅ AES-CTR decryption of Victron data
- ✅ Hardcoded AES keys (no WiFi/captive portal)
- ✅ Optimized layout for 3 devices
- ✅ Updates every second
- ✅ Reduced memory footprint (~200KB app)

## 🔧 Supported Hardware

### Target Display
- **Freenove ESP32 Display** (FNK0103S)
- MCU: ESP32-WROOM-32E
- Display: 4.0" ST7796S 320x480 SPI
- Touch: XPT2046 (not used)
- Flash: 4MB (no PSRAM)

### Display Pinout

| Function | GPIO |
|----------|------|
| MOSI | 13 |
| MISO | 12 |
| SCLK | 14 |
| CS (LCD) | 15 |
| DC | 2 |
| Backlight | 27 |
| RST | - (not connected) |

## 📡 Supported Victron Devices

### 1. SmartSolar MPPT (Record Type 0x01)

Solar charge controllers from the SmartSolar series.

**Displayed data:**
- PV Power (W) - main value
- Charge current (A)
- Battery voltage (V)
- State (OFF/BULK/ABSORB/FLOAT/etc.)
- Daily yield (kWh)

### 2. SmartBatterySense (Record Type 0x02, PID 0xA3A4/0xA3A5)

Wireless battery voltage and temperature sensor.

**Displayed data:**
- Battery temperature (°C) - main value
- Battery voltage (V)

⚠️ **Note:** SmartBatterySense uses the same record type as Battery Monitor (0x02) but only transmits voltage and temperature. Other fields (SOC, current, TTG) are N/A and are ignored.

### 3. SmartShunt (Record Type 0x02, PID 0xA389-0xA38B)

Full battery monitor with current shunt.

**Displayed data:**
- SOC (%) - main value
- Battery voltage (V)
- Current (A)
- Time To Go (h:mm)
- Consumed Ah

## 🔑 AES Key Configuration

AES keys are hardcoded in `components/victron_ble/victron_ble.c`:

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

// SmartShunt (placeholder - insert real key)
static uint8_t aes_key_smartshunt[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static bool has_smartshunt_key = false;  // Set to true when adding the key
```

### How to get the keys

1. Open the **VictronConnect** app on your phone
2. Select the device
3. Go to ⚙️ **Settings** → **Product info**
4. Enable **Instant Readout**
5. Copy the **Encryption Key** (32 hex characters)

### Key format

The key in the app is in hex string format (e.g., `f2dcc3ba40edb8de7e07d7638f13f971`).

Convert to byte array:
```
f2dcc3ba... → 0xf2, 0xdc, 0xc3, 0xba, ...
```

## 🏗️ Project Structure

```
VictronSolarDisplayEsp/
├── main/
│   ├── main_simple.c      # Entry point and UI logic
│   ├── simple_display.c   # ST7796 SPI display driver
│   ├── simple_display.h   # Display API and colors
│   ├── idf_component.yml  # Component dependencies
│   └── CMakeLists.txt     # Build configuration
├── components/
│   └── victron_ble/
│       ├── victron_ble.c      # BLE scanner and AES decoder
│       ├── victron_ble.h      # Public API
│       ├── victron_products.c # Product name database
│       ├── victron_products.h # Product IDs
│       └── victron_records.h  # Record data structures
├── docs/
│   └── extra-manufacturer-data-2022-12-14.txt  # Victron BLE spec
├── CMakeLists.txt         # Root build file
├── partitions.csv         # Partition table
└── sdkconfig              # ESP-IDF configuration
```

## 🔨 Building

### Prerequisites

- ESP-IDF v5.5.x
- Python 3.11+

### Build

```bash
# From ESP-IDF Command Prompt
cd VictronSolarDisplayEsp
idf.py build
```

### Flash

```bash
idf.py -p COM6 flash monitor
```

### Clean build

```bash
idf.py fullclean
```

## 📺 Display Layout

The display is divided into 3 vertical sections (~160px each):

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

**Status indicators:**
- `(--)` = No data received
- `(no key)` = AES key not configured

## 🎨 Color Scheme

| Element | Color | Hex RGB565 |
|---------|-------|------------|
| Section titles | Yellow | 0xFFE0 |
| Main values | Green | 0x07E0 |
| Voltage | Cyan | 0x07FF |
| Negative current | Orange | 0xFD20 |
| Errors/warnings | Red | 0xF800 |
| Normal text | White | 0xFFFF |
| Background | Black | 0x0000 |

## 🔍 Debugging

### Serial log

The firmware produces detailed logs on the serial port (115200 baud):

```
I (1234) VICTRON: MPPT: 13.32V 2.1A 28W
I (2345) victron_ble: === Battery Monitor (PID=0xA3A4) ===
I (2346) victron_ble: Vbat=13.32V Ibat=-0.001A SOC=102.3% TTG=65535 min
I (2347) victron_ble: Aux_mode=2 Aux_val=29565 (295.65K = 22.50C)
```

### Enable verbose BLE debug

In `main_simple.c`, after `victron_ble_init()`:
```c
victron_ble_set_debug(true);
```

## 📝 Technical Notes

### Victron BLE Protocol

Victron devices transmit BLE advertisements with:
- Vendor ID: `0x02E1` (Victron Energy)
- Record Type: `0x10` (Product Advertisement)
- AES-CTR encrypted data with 16-bit nonce

### SmartBatterySense Parsing

SmartBatterySense uses Battery Monitor record type (0x02) but:
- Product ID: `0xA3A4` or `0xA3A5`
- Only `voltage` and `aux_value` (temperature) are valid
- `aux_input` must be `2` for temperature
- Temperature in Kelvin × 100: `temp_C = (aux_value / 100.0) - 273.15`
- All other fields (SOC, current, TTG, consumed) are N/A

### Automatic key selection

The system selects the correct AES key by comparing the `encryptKeyMatch` byte in the advertisement with the first byte of each configured key:
- If `encryptKeyMatch == aes_key_mppt[0]` → use MPPT key
- If `encryptKeyMatch == aes_key_batt[0]` → use Battery key
- If `encryptKeyMatch == aes_key_smartshunt[0]` → use SmartShunt key

## 📜 License

MIT License - See LICENSE file

## 🔗 Resources

- BLE protocol documentation: Victron Energy
- Python victron-ble library: [keshavdv/victron-ble](https://github.com/keshavdv/victron-ble)

---

**Version:** 2.0.0-simple  
**Date:** December 2024  
**Target:** Freenove ESP32 Display (FNK0103S)
