/*
  FRC (First Robotics Competition) - Pit Terminal (Web Ayarlı Sürüm)
  ESP8266 (NodeMCU) + 6 pinli SPI OLED (SSD1306, 128x64)

  Artık WiFi adı/şifresi, TBA API key, event key ve takım bilgileri
  KOD İÇİNE YAZILMIYOR. Bunun yerine:

  1) İlk açılışta ESP8266 kendi WiFi ağını yayınlar: "GoldenHorn-Setup"
  2) Telefon/bilgisayarla o ağa bağlan (şifre yok)
  3) Otomatik açılan sayfada (açılmazsa tarayıcıdan 192.168.4.1'e git)
     gerçek WiFi adını/şifresini ve TBA bilgilerini gir, kaydet
  4) Cihaz yeniden başlar, gerçek WiFi a bağlanır, ekran çalışmaya başlar

  Ayarları SONRADAN değiştirmek için:
  - Cihaz WiFi a bağlıyken, ekrandaki "WIFI DURUMU" sayfasında yazan
    IP adresini tarayıcına yaz (örn: http://192.168.1.213)
  - Açılan sayfadan TBA key / event key / takım bilgilerini güncelle

  WiFi ağını SIFIRDAN değiştirmek için (yanlış ağ girildiyse):
  - Aynı IP adresine /reset ekleyerek git (örn: http://192.168.1.213/reset)
  - Cihaz WiFi ayarlarını siler ve "GoldenHorn-Setup" moduna geri döner

  Kütüphaneler (Arduino IDE > Library Manager'dan yükle):
    - U8g2 by olikraus
    - WiFiManager by tzapu          <-- YENİ
    - ArduinoJson by Benoit Blanchon
    - NTPClient by Fabrice Weinberg
    - ESP8266WiFi, ESP8266HTTPClient, ESP8266WebServer, LittleFS (dahili)

  Pin bağlantısı (6 pinli SPI ekrana göre):
    GND->GND  VCC->3V3  SCK/SCL->D5  SDA->D7  RES->D0  DC->D8
    Buzzer: + -> D1   - -> GND
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
#include <WiFiManager.h>   // tzapu/WiFiManager

// ----------- SABİT DEFAULT DEĞERLER (ilk açılış için) -----------------
String cfgTbaKey    = "";
String cfgEventKey  = "";
String cfgTeamName  = "GOLDEN HORN";
String cfgTeamNum   = "#8159";
const char* TBA_TEAM_KEY = "frc8159";
long utcOffsetSeconds = 3 * 3600; // başlangıç değeri (İstanbul), IP den tespit edilince güncellenir otomatik
unsigned long lastTzFetch = 0;
const unsigned long TZ_FETCH_INTERVAL = 30UL * 60UL * 1000UL; // 30 dakikada bir yeniden kontrol et
// ----------------------------------------------------------------------------

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
  U8G2_R0, /*clock=*/D5, /*data=*/D7, /*cs=*/U8X8_PIN_NONE, /*dc=*/D8, /*reset=*/D0
);

ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetSeconds);

unsigned long lastScreenSwitch = 0;
int screenIndex = 0;
const int SCREEN_INTERVAL = 5000;

String nextMatchText = "Mac bilgisi yok";
unsigned long lastTbaFetch = 0;
const unsigned long TBA_FETCH_INTERVAL = 5UL * 60UL * 1000UL;

String lastMatchLine1 = "Henuz mac yok";
String lastMatchLine2 = "";

const int BUZZER_PIN = D1; // aktif buzzer (D4/GPIO2 boot pini olduğu için kullanılmadı)
int lastKnownMatchNumber = -1; // yeni maç tespit etmek için

// ---------------- ÖNCEDEN TANIMLAMALAR (prototipler) ----------------
void drawMessage(const char* line1, const char* line2, const char* line3 = "");
void drawTeamScreen();
void drawClockScreen();
void drawWifiScreen();
void drawMatchScreen();
void drawLastMatchScreen();
void fetchNextMatch();
void fetchTimezone();
void beep(int times, int durationMs = 120);

// -------- AYAR DOSYASI (LittleFS) ----------------

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

// ---------- WEB AYAR SAYFASI ---------

void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Golden Horn Ayarlari</title>"
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
    "<p><label>TBA Event Key (orn: 2026flor)</label><br><input name='event_key' style='width:100%' value='" + cfgEventKey + "'></p>"
    "<p><label>Takim Adi</label><br><input name='team_name' style='width:100%' value='" + cfgTeamName + "'></p>"
    "<p><label>Takim No</label><br><input name='team_num' style='width:100%' value='" + cfgTeamNum + "'></p>"
    "<p style='text-align:center'><button type='submit'>KAYDET</button></p>"
    "</form>"
    "<hr><p style='text-align:center'><a href='/reset'>WiFi ayarlarini sifirla</a></p>"
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
    "<html><body style='font-family:sans-serif'><h3>Kaydedildi.</h3>"
    "<a href='/'>Geri don</a></body></html>");

  lastTbaFetch = 0; // bir sonraki döngüde TBA yı yeniden çek
}

void handleWifiReset() {
  server.send(200, "text/html", "<html><body>WiFi sıfırlanıyor, yeniden başlatılıyor...</body></html>");
  delay(1000);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// ------------- SETUP / LOOP -------

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  drawMessage("Baslatiliyor...", "");

  loadConfig();

  WiFiManager wm;
  WiFiManagerParameter p_tba("tba_key", "TBA API Key", cfgTbaKey.c_str(), 80);
  WiFiManagerParameter p_event("event_key", "TBA Event Key (orn: 2026flor)", cfgEventKey.c_str(), 40);
  WiFiManagerParameter p_name("team_name", "Takim Adi", cfgTeamName.c_str(), 40);
  WiFiManagerParameter p_num("team_num", "Takim No", cfgTeamNum.c_str(), 20);
  wm.addParameter(&p_tba);
  wm.addParameter(&p_event);
  wm.addParameter(&p_name);
  wm.addParameter(&p_num);

  drawMessage("WiFi kurulumu:", "GoldenHorn-Setup", "-by Deipedra");
  bool connected = wm.autoConnect("GoldenHorn-Setup");

  if (connected) {
    cfgTbaKey   = p_tba.getValue();
    cfgEventKey = p_event.getValue();
    cfgTeamName = p_name.getValue();
    cfgTeamNum  = p_num.getValue();
    saveConfig();
    beep(1); // bağlantı başarılı sinyali
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

// ------- EKRAN ÇİZİMLERİ ----------

void drawTeamScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawUTF8(10, 28, cfgTeamName.c_str());
  u8g2.setFont(u8g2_font_helvB18_tr);
  u8g2.drawUTF8(30, 55, cfgTeamNum.c_str());
  u8g2.sendBuffer();
}

void drawClockScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawUTF8(20, 15, "SAAT (Oto TZ)");
  u8g2.setFont(u8g2_font_logisoso24_tn);
  u8g2.drawUTF8(10, 50, timeClient.getFormattedTime().c_str());
  u8g2.sendBuffer();
}

void drawWifiScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawUTF8(0, 15, "WIFI DURUMU");
  u8g2.setFont(u8g2_font_6x12_tr);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = "IP: " + WiFi.localIP().toString();
    u8g2.drawUTF8(0, 35, ip.c_str());
    u8g2.drawUTF8(0, 50, "Ayar: yukaridaki IP");
  } else {
    u8g2.drawUTF8(0, 35, "Baglanti yok");
  }
  u8g2.sendBuffer();
}

void drawMatchScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawUTF8(0, 15, "SIRADAKI MAC");
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawUTF8(0, 35, nextMatchText.c_str());
  u8g2.sendBuffer();
}

void drawLastMatchScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawUTF8(0, 15, "SON MAC");
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawUTF8(0, 35, lastMatchLine1.c_str());
  u8g2.drawUTF8(0, 50, lastMatchLine2.c_str());
  u8g2.sendBuffer();
}

void drawMessage(const char* line1, const char* line2, const char* line3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawUTF8(0, 25, line1);
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawUTF8(0, 45, line2);
  u8g2.drawUTF8(0, 60, line3);
  u8g2.sendBuffer();
}

// ------- TBA API --------------

void fetchNextMatch() {
  if (cfgTbaKey.length() == 0 || cfgEventKey.length() == 0) {
    nextMatchText = "TBA ayarlari eksik";
    lastMatchLine1 = "TBA ayarlari eksik";
    lastMatchLine2 = "";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://www.thebluealliance.com/api/v3/team/" + String(TBA_TEAM_KEY) +
               "/event/" + cfgEventKey + "/matches";

  http.begin(client, url);
  http.addHeader("X-TBA-Auth-Key", cfgTbaKey);

  int httpCode = http.GET();
  Serial.print("TBA HTTP kod: ");
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
        nextMatchText = "Mac #" + String(matchNum) + " - Sirada";

        if (matchNum != lastKnownMatchNumber) {
          if (lastKnownMatchNumber != -1) beep(3); // yeni maç tespit edildi(BUZZER İLE UYARI)
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

    if (!foundNext) nextMatchText = "Bekleyen mac yok";

    if (bestActualTime > 0) {
      // takım hangi ittifakta (kırmızı/mavi) oynamış onu bul
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
      if (winningAll == "") result = "BERABERE";
      else if (winningAll == allianceColor) result = "KAZANDI";
      else result = "KAYBETTI";

      String rpText = "";
      JsonVariant rpVal = bestCompleted["score_breakdown"][allianceColor]["rp"];
      if (!rpVal.isNull()) {
        rpText = " RP:" + String(rpVal.as<int>());
      }

      int matchNum = bestCompleted["match_number"];
      String colorTr = (allianceColor == "red") ? "Kirmizi" : "Mavi";

      lastMatchLine1 = "#" + String(matchNum) + " " + colorTr + " " + String(myScore) + "-" + String(oppScore);
      lastMatchLine2 = result + rpText;
    } else {
      lastMatchLine1 = "Henuz mac yok";
      lastMatchLine2 = "";
    }
  } else {
    nextMatchText = "TBA verisi alinamadi";
    lastMatchLine1 = "TBA verisi alinamadi";
    lastMatchLine2 = "";
  }
  http.end();
}

// -------- SAAT DİLİMİ (IP den otomatik tespit) -----

void fetchTimezone() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // worldtimeapi.org isteği gönderen cihazın IP sine göre saat dilimini
  // otomatik tespit ediyor. ekstra API key gerekmiyor o yüzcden
  http.begin(client, "https://worldtimeapi.org/api/ip");
  int httpCode = http.GET();

  Serial.print("Saat dilimi HTTP kod: ");
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
      Serial.print("Tespit edilen saat dilimi: ");
      Serial.print(tzName);
      Serial.print(" (offset: ");
      Serial.print(utcOffsetSeconds);
      Serial.println("s)");
    }
  }
  http.end();
}

// ------- BUZZER ----------------

void beep(int times, int durationMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(durationMs);
  }
}