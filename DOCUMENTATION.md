# Documentation

Detailed hardware, software, and troubleshooting reference for the FRC Pit Terminal. For a quick overview and setup summary, see [README.md](./README.md).

## Table of Contents

- [Purpose](#purpose)
- [Wiring - OLED Display](#wiring---oled-display)
- [Wiring - Buzzer](#wiring---buzzer)
- [Enclosure & Assembly](#enclosure--assembly)
- [Software Setup](#software-setup)
- [First Boot Flow](#first-boot-flow)
- [Changing Settings Later](#changing-settings-later)
- [Getting a TBA API Key and Event Key](#getting-a-tba-api-key-and-event-key)
- [Screens](#screens)
- [Troubleshooting](#troubleshooting)
- [Known Limitations](#known-limitations)

---

## Purpose

This project turns an ESP8266 and a small OLED screen into a self-contained pit-area display for FRC teams. Instead of someone constantly checking a phone or laptop for match schedules and results, the terminal sits on the pit table and shows that information automatically, refreshing itself from The Blue Alliance API. It also gives an audible alert (via an active buzzer) when a new match involving the team is announced, so the crew can be doing something else and still not miss a call to the field.

Everything that's specific to a team - WiFi credentials, TBA API key, event key, team name and number - is entered through a configuration web page the device hosts itself. None of it lives in the source code, so the same `.ino` file works for any team without editing.

---

## Wiring - OLED Display

This repository's code is written for a **6-pin SPI OLED module** by default. If your display only has 4 pins, it's an **I2C module**, and both the wiring and one line of code are different. Check the back of your display or its labeled pins to determine which type you have.

### 6-Pin SPI OLED

| OLED Pin | ESP8266 (NodeMCU) | GPIO |
|---|---|---|
| GND | GND | - |
| VCC | 3V3 | - |
| SCK / SCL (Clock) | D5 | GPIO14 |
| SDA (MOSI / Data) | D7 | GPIO13 |
| RES (Reset) | D0 | GPIO16 |
| DC (Data/Command) | D8 | GPIO15 |

There is no CS (Chip Select) pin on 6-pin SPI OLED modules, so the code uses `U8X8_PIN_NONE` for it. The relevant line in the sketch:

```cpp
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
  U8G2_R0, /*clock=*/D5, /*data=*/D7, /*cs=*/U8X8_PIN_NONE, /*dc=*/D8, /*reset=*/D0
);
```

### 4-Pin I2C OLED

| OLED Pin | ESP8266 (NodeMCU) | GPIO |
|---|---|---|
| GND | GND | - |
| VCC | 3V3 | - |
| SCL | D1 | GPIO5 |
| SDA | D2 | GPIO4 |

I2C displays don't need separate reset or data/command pins - the module talks over a two-wire bus at a fixed address (usually `0x3C`). If you're using an I2C display, replace the display constructor line with:

```cpp
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/ U8X8_PIN_NONE);
```

If your chip is an SH1106 instead of an SSD1306 (see below), use `U8G2_SH1106_128X64_NONAME_F_HW_I2C` instead.

Switching to I2C frees up D5, D7, D0, and D8, which is useful if you plan to add more peripherals later.

### Identifying SSD1306 vs SH1106

The two most common 128x64 OLED controllers look identical but need different constructor names in code:

- Check the small chip on the back of the module for a printed part number.
- Check the PCB silkscreen text near the display for a model name.
- As a rule of thumb, 0.91" and 0.96" displays are usually SSD1306; 1.3" displays are usually SH1106.
- If unsure, try SSD1306 first. A blank or garbled screen usually means you need to switch to SH1106 in the constructor line.

---

## Wiring - Buzzer

| Buzzer Pin | ESP8266 (NodeMCU) | GPIO |
|---|---|---|
| + (VCC / signal) | D1 | GPIO5 |
| - (GND) | GND | - |

If your buzzer has three pins (VCC, GND, I/O) instead of two, connect VCC to 3V3, I/O to D1, and GND to GND.

The buzzer used here is an **active** buzzer - it has its own internal oscillator, so the code only needs to toggle the pin HIGH/LOW with `digitalWrite()`. A passive buzzer would instead require generating a tone with `tone()`.

### Why D1 and not D3, D4, or D8

ESP8266 boards have a handful of "boot strapping" pins - GPIO0 (D3), GPIO2 (D4), and GPIO15 (D8) - that must sit at a specific voltage level while the chip boots or is being flashed. A passive component (like the OLED's DC line on D8) generally doesn't disturb this, but an active component drawing current - like a buzzer - can pull the pin to the wrong level at exactly the wrong moment.

In practice, wiring the buzzer to D4 (GPIO2) causes uploads to fail with:

```
A fatal esptool.py error occurred: Failed to connect to ESP8266: Timed out waiting for packet header
```

D1 (GPIO5) has no such restriction and is otherwise unused in this project, so it's the safe choice. If you switch the OLED to I2C wiring (which frees D0/D5/D7/D8), you can move the buzzer to any of those except D8.

---

## Enclosure & Assembly

If you're 3D printing an enclosure for the terminal, here's the hardware and fastening approach used for the version pictured in the README:

- **Screws:** M2, 4mm length
- **Nuts:** M2 hex nuts, heat-set into the printed plastic rather than left as loose fasteners
- **Heat-setting method:** a soldering iron is used to melt each nut into its recess after printing - the iron tip presses the nut down flush while it softens the surrounding plastic, which then cools and grips the nut's hex flats
- **Nut trap sizing:** the hexagonal recesses in the CAD model are intentionally undersized relative to the nut's actual dimensions. This is deliberate, not a printing error - a slightly tight fit gives the heat-set nut something to bite into as it melts in, resulting in a much stronger hold than a recess sized to the nut's exact dimensions (which would leave the nut loose once the plastic cools)

This approach is a common practice for 3D-printed enclosures that need to be opened and closed repeatedly (e.g. to access the ESP8266's USB port) without stripping the plastic threads a screw would otherwise cut directly into.

---

## Software Setup

### Required Libraries

Install these through Arduino IDE's Library Manager (`Tools > Manage Libraries`):

| Library | Author | Purpose |
|---|---|---|
| U8g2 | olikraus | OLED display driver |
| WiFiManager | tzapu | WiFi provisioning + settings web page |
| ArduinoJson | Benoit Blanchon | Parsing TBA and worldtimeapi.org responses |
| NTPClient | Fabrice Weinberg | Fetching the current time over the network |

`ESP8266WiFi`, `ESP8266HTTPClient`, `ESP8266WebServer`, and `LittleFS` ship with the ESP8266 Arduino core and don't need separate installation.

### Board Manager Setup

If `ESP8266 Boards` doesn't appear under `Tools > Board`, the core isn't installed yet:

1. `File > Preferences` (Windows/Linux) or `Arduino IDE > Settings` (Mac)
2. Add this URL to "Additional Boards Manager URLs":
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. `Tools > Board > Boards Manager`, search `esp8266`, install **esp8266 by ESP8266 Community**
4. Restart Arduino IDE
5. Select `Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)`

### Uploading

1. Connect the ESP8266 with a data-capable USB cable (some cables are charge-only and won't work)
2. Select the correct port under `Tools > Port`. If none appears, you may need to install a USB-to-serial driver (commonly CH340 or CP2102, depending on the board) - check Device Manager (Windows) for an unrecognized device under "Ports"
3. `Sketch > Upload` (or Ctrl+U)
4. Open `Tools > Serial Monitor` at 115200 baud to watch boot and connection logs

---

## First Boot Flow

1. On first power-up (or after a settings reset), the ESP8266 starts its own open WiFi access point, e.g. `GoldenHorn-Setup`.
2. Connect a phone or computer to that network.
3. Most devices will show a "Sign in to network" notification and open the configuration page automatically (this relies on captive portal detection, which isn't 100% reliable on every phone/OS).
4. If it doesn't open automatically, open a browser and go to `192.168.4.1`.
5. On some Android phones (Samsung in particular), a feature like "Smart Network Switch" can prevent the captive portal from opening because the phone assumes a network with no internet isn't worth staying on. Turning that off, or manually navigating to `192.168.4.1`, resolves it.
6. On the setup page, select your real WiFi network, enter its password, and fill in the additional fields for TBA API key, event key, team name, and team number.
7. Save. The device restarts and connects to your real network.

---

## Changing Settings Later

You don't need to reflash the device to update TBA keys, the event key, or team info:

1. While the device is connected to your WiFi, check the "WiFi Status" screen on the OLED for its local IP address (e.g. `192.168.1.42`).
2. From any device on the same network, open that IP in a browser.
3. Update the fields and save.

To fully reset the WiFi network (e.g. you entered the wrong network by mistake), visit `<device-ip>/reset` (e.g. `192.168.1.42/reset`). This clears the saved WiFi credentials and puts the device back into setup-portal mode.

---

## Getting a TBA API Key and Event Key

### API Key

1. Go to [thebluealliance.com/account](https://www.thebluealliance.com/account)
2. Under **Read API Keys**, give it a description (e.g. "Pit Terminal") and click **Add New Key**
3. Copy the generated key into the setup page's "TBA API Key" field

Use a **Read** key, not a Write key - Write keys are for scoring/event-management tools and aren't needed here.

### Event Key

TBA event keys follow the pattern `<year><short event code>` (e.g. `2026flor`), but the short code doesn't always match the city name exactly. The reliable way to find it:

1. Go to [thebluealliance.com/events](https://www.thebluealliance.com/events) for the relevant year
2. Find your event and open its page
3. Read the event key from the end of the URL, e.g. `thebluealliance.com/event/2026flor` -> `2026flor`

---

## Screens

The display cycles through five screens, five seconds each:

1. **Team** - the team name and number entered during setup
2. **Clock** - current local time, using a timezone offset detected automatically from the network's public IP (via worldtimeapi.org), refreshed every 30 minutes so travelling to a different event/timezone updates it without reconfiguration
3. **WiFi Status** - connection state and the device's local IP (needed to reach the settings page)
4. **Next Match** - the next unplayed match number pulled from TBA for the configured event; the buzzer beeps three times whenever the reported next match number changes
5. **Last Match** - the most recently completed match: match number, which alliance (red/blue) the team played on, the final score, the result (win/loss/tie), and the RP earned if TBA's data includes it

---

## Troubleshooting

**`Failed to connect to ESP8266: Timed out waiting for packet header` during upload**
Something is interfering with the board's boot strapping pins during reset. The most common cause in this project is an active component (like a buzzer) wired to D3, D4, or D8. Move it to an unrestricted pin (D1, D2, or D6) and try again.

**`error: too many arguments to function 'drawMessage(...)'` or `'drawMessage' was not declared in this scope`**
Usually caused by a mismatch between a function's forward declaration and its actual definition further down the file (e.g. a default argument like `= ""` specified in both places, which C++ doesn't allow), or a stray/missing brace that nests one function inside another. Compare the function signature at the top of the file against the one where it's actually implemented - they should match exactly except that only the forward declaration carries the default value.

**Setup page (captive portal) doesn't open automatically after connecting to the `-Setup` WiFi network**
This is a phone/OS-level captive portal detection issue, not a firmware bug - it isn't 100% reliable on any platform. Try, in order:
1. Pull down the notification shade and look for a "Sign in to network" notification
2. Manually browse to `192.168.4.1`
3. On Android, especially Samsung: turn off "Smart Network Switch" under WiFi settings, which can cause the phone to abandon a network it thinks has no internet access before the captive portal check completes
4. Try navigating to a plain-HTTP page like `http://neverssl.com`, which can force the OS to notice the captive portal

**"TBA verisi alinamadi" / "Could not fetch TBA data" on the Next/Last Match screens**
1. Confirm the TBA API key and event key were entered correctly on the settings page
2. Open Serial Monitor (115200 baud) and check the printed HTTP status code:
   - `-1`: connection failed (WiFi issue, or HTTPS handshake failed)
   - `401`: invalid or missing API key
   - `404`: event key is wrong - double check it against the event's TBA URL
   - `200`: the request succeeded; if data still looks wrong, the issue is likely in how the response is being parsed rather than the request itself
3. TBA's API is HTTPS-only; requests made with plain `WiFiClient` over `http://` will fail. This project uses `WiFiClientSecure` with `client.setInsecure()` to skip certificate validation (a practical tradeoff on the limited resources of an ESP8266).

---

## Known Limitations

- RP (Ranking Points) data is read from TBA's `score_breakdown` field, which changes structure from one FRC game season to the next. If a season's format isn't recognized, the RP portion of the "Last Match" screen is simply left blank - the score and result still display correctly.
- HTTPS requests use `client.setInsecure()`, skipping certificate validation. Full certificate validation is possible on ESP8266 but adds complexity and memory overhead that isn't justified for this project's threat model.
- The captive portal / auto-redirect to the setup page depends on the connecting device's OS and isn't guaranteed to work on every phone - manually visiting `192.168.4.1` always works as a fallback.
