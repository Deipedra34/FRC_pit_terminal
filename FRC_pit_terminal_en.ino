/*
  FRC(First Robotics Competition) - Pit Terminal (Web Config Version)
  ESP8266 (NodeMCU) + 6-pin SPI OLED (SSD1306, 128x64)

  WiFi name/password, TBA API key, event key and team info are
  NOT hardcoded in this file. Instead:

  1) On first boot, the ESP8266 broadcasts its own WiFi network: "GoldenHorn-Setup"
  2) Connect to that network with your phone/computer (no password)
  3) A setup page should open automatically (if not, go to 192.168.4.1
     in your browser) - enter your real WiFi name(if u want just click the WIFI name/password and TBA info there)
  4) The device restarts, connects to your real WiFi, and the screen starts working

  To change settings LATER:
  - While connected to WiFi, check the IP address shown on the OLED's
    "WIFI STATUS" screen and type it into a browser (e.g. http://192.168.1.213)
  - Update the TBA key / event key / team info from the page that opens

  To reset the WiFi network completely (if the wrong network was entered):
  - go to the same IP address with /reset added (e.g. http://192.168.1.213/reset)
  - The device will erase its WiFi settings and go back into "GoldenHorn-Setup" mode

  Libraries (install via Arduino IDE > Library Manager):
    - U8g2 by olikraus
    - WiFiManager by tzapu
    - ArduinoJson by Benoit Blanchon
    - NTPClient by Fabrice Weinberg
    - ESP8266WiFi, ESP8266HTTPClient, ESP8266WebServer, LittleFS (built-in)

  Wiring:
    GND->GND  VCC->3V3  SCK/SCL->D5  SDA->D7  RES->D0  DC->D8 (for SPI oled)
*/

#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiManager.h>

// ---------------------------- DEF. VALUES (used on first boot) ----------------------------------
String cfgTbaKey    = "";
String cfgEventKey  = "";
String cfgTeamName  = "GOLDEN HORN";
String cfgTeamNum   = "#8159";
const char* TBA_TEAM_KEY = "frc8159";
long utcOffsetSeconds = 3 * 3600; // starting value (Istanbul)(!auto updated once IP based detection runs!)
unsigned long lastTzFetch = 0;
const unsigned long TZ_FETCH_INTERVAL = 30UL * 60UL * 1000UL; // re check every 30 minutes
// ---------------------------------------------------------------------------------------------------------------------------------

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
  U8G2_R0, /*clock=*/D5, /*data=*/D7, /*cs=*/U8X8_PIN_NONE, /*dc=*/D8, /*reset=*/D0
);

ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetSeconds);

unsigned long lastScreenSwitch = 0;
int screenIndex = 0;
const int SCREEN_INTERVAL = 5000;

String nextMatchText = "No match info";
unsigned long lastTbaFetch = 0;
const unsigned long TBA_FETCH_INTERVAL = 5UL * 60UL * 1000UL; // every 5 minutes

String lastMatchLine1 = "No match yet";
String lastMatchLine2 = "";

const int BUZZER_PIN = D1; // active buzzer (D4/GPIO2 avoided its a boot strapping pin)
int lastKnownMatchNumber = -1; // used to detect a newly announced match

// -------------------------- FORWARD DECLARATIONS ---------------------------------
void drawMessage(const char* line1, const char* line2, const char* line3 = "");
void drawTeamScreen();
void drawClockScreen();
void drawWifiScreen();
void drawMatchScreen();
void drawLastMatchScreen();
void fetchNextMatch();
void fetchTimezone();
void beep(int times, int durationMs = 120);

// ------------------------------ CONFIG FILE (LittleFS) ----------------------------

void loadConfig() {
  if (!LittleFS.begin()) return;
  if (!LittleFS.exists("/config.json")) return;

  File f = LittleFS.open("/config.json", "r");
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, f);
  f.close();

  cfgTbaKey   = doc["tba_key"]   | "";
  cfgEventKey = doc["event_key"] | "";
  cfgTeamName = doc["team_name"] | "GOLDEN HORN";
  cfgTeamNum  = doc["team_num"]  | "#8159";
}

void saveConfig() {
  DynamicJsonDocument doc(1024);
  doc["tba_key"]   = cfgTbaKey;
  doc["event_key"] = cfgEventKey;
  doc["team_name"] = cfgTeamName;
  doc["team_num"]  = cfgTeamNum;

  File f = LittleFS.open("/config.json", "w");
  serializeJson(doc, f);
  f.close();
}

// --------------------------------- WEB SETTINGS PAGE ---------------------------------

void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Golden Horn Settings</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:420px;margin:0 auto;background:#0d0d0d;color:#eee;padding:20px}"
    "h2{color:#d4af37;text-align:center;letter-spacing:1px}"
    "input{background:#1a1a1a;border:1px solid #d4af37;color:#eee;padding:8px;border-radius:4px;box-sizing:border-box}"
    "label{color:#d4af37;font-size:14px}"
    "button{background:#d4af37;color:#000;border:none;padding:10px 24px;border-radius:4px;font-weight:bold;cursor:pointer}"
    "a{color:#d4af37}"
    "hr{border-color:#333}"
    "</style></head>"
    "<body>"
    "<svg viewBox='0 0 200 120' width='100%' height='110' xmlns='http://www.w3.org/2000/svg'>"
      "<defs><linearGradient id='g' x1='0' y1='0' x2='0' y2='1'>"
        "<stop offset='0%' stop-color='#f2d675'/><stop offset='100%' stop-color='#b8860b'/>"
      "</linearGradient></defs>"
      "<path d='M20 90 Q100 20 180 90' stroke='url(#g)' stroke-width='6' fill='none' stroke-linecap='round'/>"
      "<circle cx='20' cy='90' r='6' fill='#d4af37'/>"
      "<circle cx='180' cy='90' r='6' fill='#d4af37'/>"
      "<path d='M60 90 L75 55 L90 90 Z' fill='#d4af37' opacity='0.85'/>"
      "<path d='M110 90 L125 45 L140 90 Z' fill='#d4af37' opacity='0.85'/>"
      "<rect x='10' y='90' width='180' height='4' fill='#8a6d1f'/>"
    "</svg>"
    "<p style='text-align:center;color:#888;margin-top:-10px;font-size:12px;letter-spacing:2px'>ISTANBUL &middot; FRC</p>"
    "<h2>GOLDEN HORN #8159</h2>"
    "<form method='POST' action='/save'>"
    "<p><label>TBA API Key</label><br><input name='tba_key' style='width:100%' value='" + cfgTbaKey + "'></p>"
    "<p><label>TBA Event Key (e.g. 2026flor)</label><br><input name='event_key' style='width:100%' value='" + cfgEventKey + "'></p>"
    "<p><label>Team Name</label><br><input name='team_name' style='width:100%' value='" + cfgTeamName + "'></p>"
    "<p><label>Team Number</label><br><input name='team_num' style='width:100%' value='" + cfgTeamNum + "'></p>"
    "<p style='text-align:center'><button type='submit'>SAVE</button></p>"
    "</form>"
    "<hr><p style='text-align:center'><a href='/reset'>Reset WiFi settings</a></p>"
    "<p style='text-align:center;color:#555;font-size:11px'>-by Deipedra</p>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("tba_key"))   cfgTbaKey   = server.arg("tba_key");
  if (server.hasArg("event_key")) cfgEventKey = server.arg("event_key");
  if (server.hasArg("team_name")) cfgTeamName = server.arg("team_name");
  if (server.hasArg("team_num"))  cfgTeamNum  = server.arg("team_num");
  saveConfig();

  server.send(200, "text/html",
    "<html><body style='font-family:sans-serif'><h3>Saved.</h3>"
    "<a href='/'>Go back</a></body></html>");

  lastTbaFetch = 0; // refetch TBA data on next loop
}

void handleWifiReset() {
  server.send(200, "text/html", "<html><body>Resetting WiFi, restarting...</body></html>");
  delay(1000);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// ----------------- SETUP / LOOP -----------------------

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  drawMessage("Starting...", "");

  loadConfig();

  WiFiManager wm;
  WiFiManagerParameter p_tba("tba_key", "TBA API Key", cfgTbaKey.c_str(), 80);
  WiFiManagerParameter p_event("event_key", "TBA Event Key (e.g. 2026flor)", cfgEventKey.c_str(), 40);
  WiFiManagerParameter p_name("team_name", "Team Name", cfgTeamName.c_str(), 40);
  WiFiManagerParameter p_num("team_num", "Team Number", cfgTeamNum.c_str(), 20);
  wm.addParameter(&p_tba);
  wm.addParameter(&p_event);
  wm.addParameter(&p_name);
  wm.addParameter(&p_num);

  drawMessage("WiFi setup:", "GoldenHorn-Setup", "-by Deipedra");
  bool connected = wm.autoConnect("GoldenHorn-Setup");

  if (connected) {
    cfgTbaKey   = p_tba.getValue();
    cfgEventKey = p_event.getValue();
    cfgTeamName = p_name.getValue();
    cfgTeamNum  = p_num.getValue();
    saveConfig();
    beep(1); // connection successful signal 
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleWifiReset);
  server.begin();

  timeClient.begin();
  if (WiFi.status() == WL_CONNECTED) {
    fetchTimezone();
    lastTzFetch = millis();
    timeClient.update();
    fetchNextMatch();
  }
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    if (millis() - lastTbaFetch > TBA_FETCH_INTERVAL) {
      fetchNextMatch();
      lastTbaFetch = millis();
    }
    if (millis() - lastTzFetch > TZ_FETCH_INTERVAL) {
      fetchTimezone();
      lastTzFetch = millis();
    }
  }

  if (millis() - lastScreenSwitch > SCREEN_INTERVAL) {
    screenIndex = (screenIndex + 1) % 5;
    lastScreenSwitch = millis();
  }

  switch (screenIndex) {
    case 0: drawTeamScreen(); break;
    case 1: drawClockScreen(); break;
    case 2: drawWifiScreen(); break;
    case 3: drawMatchScreen(); break;
    case 4: drawLastMatchScreen(); break;
  }

  delay(100);
}

// ------------------------ SCREEN DRAWING -------------------------

void drawTeamScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawStr(10, 28, cfgTeamName.c_str());
  u8g2.setFont(u8g2_font_helvB18_tr);
  u8g2.drawStr(30, 55, cfgTeamNum.c_str());
  u8g2.sendBuffer();
}

void drawClockScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(20, 15, "TIME (Auto TZ)");
  u8g2.setFont(u8g2_font_logisoso24_tn);
  u8g2.drawStr(10, 50, timeClient.getFormattedTime().c_str());
  u8g2.sendBuffer();
}

void drawWifiScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(0, 15, "WIFI STATUS");
  u8g2.setFont(u8g2_font_6x12_tr);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = "IP: " + WiFi.localIP().toString();
    u8g2.drawStr(0, 35, ip.c_str());
    u8g2.drawStr(0, 50, "Settings: IP above");
  } else {
    u8g2.drawStr(0, 35, "Not connected");
  }
  u8g2.sendBuffer();
}

void drawMatchScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(0, 15, "NEXT MATCH");
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 35, nextMatchText.c_str());
  u8g2.sendBuffer();
}

void drawLastMatchScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(0, 15, "LAST MATCH");
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 35, lastMatchLine1.c_str());
  u8g2.drawStr(0, 50, lastMatchLine2.c_str());
  u8g2.sendBuffer();
}

void drawMessage(const char* line1, const char* line2, const char* line3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(0, 25, line1);
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 45, line2);
  u8g2.drawStr(0, 60, line3);
  u8g2.sendBuffer();
}

// -------------------- TBA API --------------------------

void fetchNextMatch() {
  if (cfgTbaKey.length() == 0 || cfgEventKey.length() == 0) {
    nextMatchText = "TBA settings missing";
    lastMatchLine1 = "TBA settings missing";
    lastMatchLine2 = "";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // skip certificate validation (practical workaround on ESP8266)
  HTTPClient http;

  String url = "https://www.thebluealliance.com/api/v3/team/" + String(TBA_TEAM_KEY) +
               "/event/" + cfgEventKey + "/matches";

  http.begin(client, url);
  http.addHeader("X-TBA-Auth-Key", cfgTbaKey);

  int httpCode = http.GET();
  Serial.print("TBA HTTP code: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(16384);
    deserializeJson(doc, payload);

    bool foundNext = false;
    long bestActualTime = -1;
    JsonObject bestCompleted;

    for (JsonObject match : doc.as<JsonArray>()) {
      String winningAlliance = match["winning_alliance"] | "";

      if (winningAlliance == "" && !foundNext) {
        int matchNum = match["match_number"];
        nextMatchText = "Match #" + String(matchNum) + " - Up next";

        if (matchNum != lastKnownMatchNumber) {
          if (lastKnownMatchNumber != -1) beep(3); // new match detected(ALERT FROM BUZZER)
          lastKnownMatchNumber = matchNum;
        }
        foundNext = true;
      }

      if (winningAlliance != "") {
        long actualTime = match["actual_time"] | 0L;
        if (actualTime > bestActualTime) {
          bestActualTime = actualTime;
          bestCompleted = match;
        }
      }
    }

    if (!foundNext) nextMatchText = "No pending matches";

    if (bestActualTime > 0) {
      // find which alliance (red/blue) our team played on
      String allianceColor = "blue";
      JsonArray redTeams = bestCompleted["alliances"]["red"]["team_keys"];
      for (JsonVariant t : redTeams) {
        if (t.as<String>() == String(TBA_TEAM_KEY)) { allianceColor = "red"; break; }
      }
      String oppColor = (allianceColor == "red") ? "blue" : "red";

      int myScore  = bestCompleted["alliances"][allianceColor]["score"] | 0;
      int oppScore = bestCompleted["alliances"][oppColor]["score"] | 0;

      String winningAll = bestCompleted["winning_alliance"] | "";
      String result;
      if (winningAll == "") result = "TIE";
      else if (winningAll == allianceColor) result = "WIN";
      else result = "LOSS";

      String rpText = "";
      JsonVariant rpVal = bestCompleted["score_breakdown"][allianceColor]["rp"];
      if (!rpVal.isNull()) {
        rpText = " RP:" + String(rpVal.as<int>());
      }

      int matchNum = bestCompleted["match_number"];
      String colorEn = (allianceColor == "red") ? "Red" : "Blue";

      lastMatchLine1 = "#" + String(matchNum) + " " + colorEn + " " + String(myScore) + "-" + String(oppScore);
      lastMatchLine2 = result + rpText;
    } else {
      lastMatchLine1 = "No match yet";
      lastMatchLine2 = "";
    }
  } else {
    nextMatchText = "Could not fetch TBA data";
    lastMatchLine1 = "Could not fetch TBA data";
    lastMatchLine2 = "";
  }
  http.end();
}

// --- TIMEZONE (auto detected from IP) -------

void fetchTimezone() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // worldtimeapi.org auto detects the timezone based on the requesting
  // devices IP address. no API key required
  http.begin(client, "https://worldtimeapi.org/api/ip");
  int httpCode = http.GET();

  Serial.print("Timezone HTTP code: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      long rawOffset = doc["raw_offset"] | (3 * 3600);
      long dstOffset = doc["dst_offset"] | 0;
      utcOffsetSeconds = rawOffset + dstOffset;
      timeClient.setTimeOffset(utcOffsetSeconds);

      const char* tzName = doc["timezone"] | "?";
      Serial.print("Detected timezone: ");
      Serial.print(tzName);
      Serial.print(" (offset: ");
      Serial.print(utcOffsetSeconds);
      Serial.println("s)");
    }
  }
  http.end();
}

// ----- BUZZER -----

void beep(int times, int durationMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(durationMs);
  }
}
