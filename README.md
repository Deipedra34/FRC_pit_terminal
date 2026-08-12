# FRC Pit Terminal

An ESP8266 + OLED pit display built for FRC (FIRST Robotics Competition) teams. It sits in the pit area and continuously cycles through team info, the current time, WiFi status, the next scheduled match, and the result of the last completed match (score, alliance color, win/loss, and Ranking Points) pulled live from The Blue Alliance (TBA) API. An active buzzer sounds a short alert whenever a new match is announced, so the team doesn't have to watch the screen constantly.

The project is team-agnostic: nothing is hardcoded. On first boot, the device opens its own configuration portal so any team can set their WiFi credentials, TBA API key, event key, and team name/number without ever touching the code.

## Features

- Live match data via The Blue Alliance (TBA) API v3
- Automatic timezone detection based on the network's public IP (no manual UTC offset needed, even if you travel to a different event location)
- Fully web-based setup - WiFi credentials and all project settings are entered through a page the device serves itself, no reflashing required to change them later
- Active buzzer alert on new match announcements
- Settings persist across reboots (stored in the ESP8266's onboard flash filesystem)
- Supports both SPI (6-pin) and I2C (4-pin) OLED display wiring
- Two language variants of the firmware: Turkish and English on-screen/web text

## Hardware Required

- ESP8266 development board (NodeMCU 1.0 / ESP-12E recommended)
- 128x64 OLED display, SSD1306 or SH1106 controller (SPI or I2C variant)
- An active buzzer (2 or 3 pin)
- A data-capable USB cable (charge-only cables will not work for flashing)

## Quick Start

1. Open `FRC_terminal_tr.ino` (Turkish UI) or `FRC_terminal_en.ino` (English UI) in Arduino IDE.
2. Install the required libraries via Library Manager: **U8g2**, **WiFiManager**, **ArduinoJson**, **NTPClient**.
3. Select your board: `Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)`.
4. Wire the OLED and buzzer as described in [DOCUMENTATION.md](./DOCUMENTATION.md).
5. Upload the sketch.
6. On first boot, the ESP8266 broadcasts a WiFi network named something like `GoldenHorn-Setup` (or your chosen name). Connect to it with a phone or laptop.
7. A setup page should open automatically. If it doesn't, browse to `192.168.4.1` manually. Enter:
   - Your real WiFi network name and password
   - A TBA **Read** API key (get one for free at [thebluealliance.com/account](https://www.thebluealliance.com/account))
   - The TBA event key for your competition (e.g. `2026flor` - the last segment of the event's TBA URL)
   - Your team name and number
8. Save. The device restarts, connects to your real WiFi, and starts cycling through its screens.

To change any of these settings later, no reflashing is needed - see [DOCUMENTATION.md](./DOCUMENTATION.md) for how to reach the settings page again.

## What It Shows

The OLED rotates through five screens every 5 seconds:

1. **Team** - team name and number
2. **Clock** - current time, timezone auto-detected from IP
3. **WiFi Status** - connection state and local IP address
4. **Next Match** - upcoming match number from TBA (buzzer beeps 3 times when a new match is detected)
5. **Last Match** - most recently completed match: number, alliance color, score, result, and RP if available

## Documentation

Full wiring diagrams (SPI vs I2C displays), buzzer pin notes, library details, troubleshooting, and known limitations are in [DOCUMENTATION.md](./DOCUMENTATION.md).

## License

No license specified yet - add one before sharing publicly if you want to control reuse.

---

-by Deipedra
