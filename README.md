# Portable ESP32 Captive-Portal Wi-Fi Cloner

Tiny ESP32 app that scans nearby Wi-Fi networks on a 1.5" SPI OLED (128×128, SH1107), lets you pick one (or a custom preset), and spins up a **captive-portal SoftAP** with that SSID. Android/iOS/laptops will get a login prompt and see a simple “Hello” page where users can submit a name that shows on the screen.


### Features
- 128×128 **SH1107 OLED** UI (u8g2) with **smooth marquee** for long SSIDs  
- **Non-blocking Wi-Fi scan** (async) with periodic refresh  
- **Captive portal** (AsyncWebServer + DNSServer) with Android probes handled  
- Three buttons: **Up / Down / OK** (with debounced repeat/hold)  
- **Custom Wi-Fi** submenu (editable list)  
- Works on common **ESP32-WROOM-32** dev boards

## Hardware

### Display module
GME128128091-SPI v3.1 (or any 1.5" **SH1107** SPI OLED, 128×128)

> [!WARNING]
> Many of these OLED boards want **VCC = 5V**, not 3V3.  
> They often include a 3.3 V regulator and level shifting, but check your module.

### Buttons
3× momentary tact switches (e.g., **6×6×8 mm**; any 6×6 should work).  
One leg to the ESP32 GPIO, the opposite leg to **GND** (we use INPUT_PULLUP).

### Bill of Materials (minimal)
- ESP32-WROOM-32 dev board  
- 1.5" SH1107 128×128 SPI OLED  
- 3× tact switches (6×6)  
- Breadboard / wires (keep SPI wires ≤10 cm to avoid noise)

## Pinout

### OLED (SPI) → ESP32
| OLED Pin | Function     | ESP32 Pin  |
| -------- | ------------ | ---------- |
| VCC      | Power        | **3v3**    |
| GND      | Ground       | GND        |
| SCL      | SPI CLK      | **GPIO18** |
| SDA      | SPI MOSI     | **GPIO23** |
| CS       | Chip Select  | **GPIO5**  |
| DC       | Data/Command | **GPIO16** |
| RST      | Reset        | **GPIO17** |

Note: SCL=SCK/CLK; SDA=MOSI (this OLED doesn’t use MISO).

### Buttons → ESP32
Each button: one leg to the GPIO, the other leg to **GND**.  
| Button | Function | ESP32 Pin  |
| ------ | -------- | ---------- |
| Up     | Input    | **GPIO4**  |
| Down   | Input    | **GPIO19** |
| OK     | Input    | **GPIO21** |

## Libraries / IDE

Tested with Arduino Core for ESP32 3.x.

Install (Library Manager or GitHub):
- U8g2 by olikraus
- ESPAsyncWebServer (latest dev)
- AsyncTCP (latest dev)
- (bundled) DNSServer and WiFi from ESP32 core

## Configuration

- Custom SSIDs (the preset list):  
 ```cpp
  const char* CUSTOM_SSIDS[] = {
    "ESP32 Wifi", "Free WiFi", "Guest AP", "My Portal", "Cafe Hotspot"
  };
```

- Pins: change the `#define` mappings near the top if your board differs.
- Contrast / rotation: `u8g2.setContrast(220)` and `ROTATION U8G2_R0`.
- Scan cadence: `RESCAN_MS` (default 10s).

## Known quirks / troubleshooting

- Garbage or shifted screen:  
  Confirm it’s an SH1107 128×128 (not SSD1351, not SH1106).  
  Keep SPI wires short (≤10 cm).  
  Ensure DC/RST are not swapped.  
  Many 1.5" modules require 5V VCC.

- No captive-portal prompt on Android:  
  Make sure you joined the AP and mobile data is off.  
  Try toggling Wi-Fi off/on.  
  Paths like `/generate_204` and `/google/generate_204` are implemented to trigger captive helper.  
  Some vendors cache—forget the AP and re-join.

- SSID contains emojis / non-ASCII:  
  The OLED font is ASCII. Unsupported glyphs show as ?.  
  Long names are trimmed with `"..."`.


## Build

Arduino IDE: open .ino and Upload  
PlatformIO: `pio run -t upload`



