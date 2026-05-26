#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiUdp.h>

// ============================================================
// CONFIG
// ============================================================
const int SCROLL_MS  = 8000;
const int FETCH_MS   = 1800000;
const int MARQUEE_MS = 350;
const int ALARM_MS   = 300000;
const int BUZZER_PIN = 23;
const int UDP_PORT   = 4210;

// ============================================================
// HARDWARE
// ============================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiUDP udp;

// ============================================================
// STATE
// ============================================================
char  serverURL[80] = "";
long  timeOffset    = 0;
bool  hasUnanswered = false;
bool  alarmActive   = false;
bool  showChaos     = false;
int   currentIndex  = 0;
int   chaosPhase    = 0;
int   chaosLcdIndex = 0;

unsigned long lastScroll   = 0;
unsigned long lastFetch    = 0;
unsigned long lastBuzzer   = 0;
unsigned long lastChaosLcd = 0;
unsigned long lastMarquee  = 0;
unsigned long alarmStarted = 0;

struct Assignment {
  String title, deadline, status;
  long   deadlineEpoch;
};
Assignment assignments[20];
int totalAssignments = 0;

struct MarqueeState { int offset = 0; bool active = false; String text = ""; };
MarqueeState row0, row1;

// ============================================================
// MARQUEE
// ============================================================
void setRow(int r, String text) {
  MarqueeState& s = r ? row1 : row0;
  s = {0, text.length() > 16, text};
  if (!s.active) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%-16s", text.c_str());
    lcd.setCursor(0, r);
    lcd.print(buf);
  }
}

void tickRow(int r) {
  MarqueeState& s = r ? row1 : row0;
  if (!s.active) return;
  String t = s.text + "    ";
  String v = "";
  for (int i = 0; i < 16; i++) v += t[(s.offset + i) % t.length()];
  lcd.setCursor(0, r);
  lcd.print(v);
  s.offset = (s.offset + 1) % t.length();
}

void showLoading(String label, int dot) {
  String s = label;
  for (int i = 0; i < (dot % 4) + 1; i++) s += ".";
  setRow(1, s);
}

// ============================================================
// BUZZER
// ============================================================
void beep(int ms) { digitalWrite(BUZZER_PIN, HIGH); delay(ms); digitalWrite(BUZZER_PIN, LOW); }

void beepPanic() {
  int p[] = {50,30,200,50,50,300,80,40}, g[] = {30,20,100,30,20,150,40,30};
  for (int i = 0; i < 8; i++) { beep(p[i]); delay(g[i]); }
}

void beepChaos() {
  beepPanic();
  if (chaosPhase++ % 2 == 0) beepPanic();
}

// ============================================================
// TIME
// ============================================================
long nowEpoch() { return millis() / 1000 + timeOffset; }

long parseDeadline(String d) {
  if (d.length() < 14) return 0;
  int dd = d.substring(0,2).toInt(), mm = d.substring(3,5).toInt();
  int yy = d.substring(6,8).toInt() + 2000;
  int hh = d.substring(9,11).toInt(), mn = d.substring(12,14).toInt();
  int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  long days = (yy-1970)*365L;
  for (int y = 1970; y < yy; y++) if ((y%4==0&&y%100!=0)||(y%400==0)) days++;
  for (int m = 0; m < mm-1; m++) { days += dim[m]; if (m==1&&((yy%4==0&&yy%100!=0)||(yy%400==0))) days++; }
  return (days+dd-1)*86400L + hh*3600L + mn*60L;
}

String formatCountdown(long s) {
  if (s <= 0) return "OVERDUE!";
  char buf[32];
  if      (s >= 86400) snprintf(buf, sizeof(buf), "%ldd %ldh left", s/86400, (s%86400)/3600);
  else if (s >= 3600)  snprintf(buf, sizeof(buf), "%ldh %ldm left", s/3600, (s%3600)/60);
  else                 snprintf(buf, sizeof(buf), "%ldm left!", s/60);
  return String(buf);
}

bool syncTime() {
  String url = String(serverURL);
  url = url.substring(0, url.lastIndexOf('/')) + "/time";
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, http.getString())) { http.end(); return false; }
  http.end();
  timeOffset = (long)doc["timestamp"] - (long)(millis()/1000);
  Serial.println("Time synced. Offset: " + String(timeOffset));
  return true;
}

// ============================================================
// DISPLAY
// ============================================================
void displayAssignment(int idx) {
  if (totalAssignments == 0) {
    lcd.clear();
    setRow(0, "All done! (^_^)");
    setRow(1, "No pending tasks");
    return;
  }
  char prefix[7];
  snprintf(prefix, sizeof(prefix), "[%d/%d] ", idx+1, totalAssignments);
  setRow(0, String(prefix) + assignments[idx].title);
  setRow(1, formatCountdown(assignments[idx].deadlineEpoch - nowEpoch()));
}

// ============================================================
// ALARM
// ============================================================
void startAlarm() {
  if (alarmActive) return;
  alarmActive = true; alarmStarted = millis();
  beep(150); delay(100); beep(150); delay(100); beep(150);
  Serial.println("Alarm started.");
}

void checkAlarm() {
  if (!alarmActive) return;
  if (millis() - alarmStarted >= ALARM_MS) {
    alarmActive = showChaos = false;
    Serial.println("Alarm stopped.");
    displayAssignment(currentIndex);
  }
}

// ============================================================
// CHAOS LCD
// ============================================================
const char* chaosMessages[] = {
  "SUBMIT OR FAIL",
  "GO DO UR TASK!!",
  "WAKTU HABIS!!!",
  "PANIK SEKARANG!",
  "GG EZ NO REMATCH",
};
const int CHAOS_COUNT = 5;

// ============================================================
// WIFI
// ============================================================
void connectWifi() {
  lcd.clear();
  setRow(0, "Connecting WiFi");
  setRow(1, "Please wait...");

  WiFiManager wm;
  wm.setAPCallback([](WiFiManager*) {
    const char* slides[][2] = {
      {"Setup WiFi",    "via hotspot"},
      {"1. Join WiFi:", "ESP32-Setup"},
      {"2. Open:",      "192.168.4.1"},
      {"3. Pilih WiFi", "& isi password"},
      {"4. Klik Save",  "lalu tunggu..."},
    };
    int idx = 0;
    while (WiFi.status() != WL_CONNECTED) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(slides[idx % 5][0]);
      lcd.setCursor(0, 1); lcd.print(slides[idx % 5][1]);
      idx++; delay(2500);
    }
  });

  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ESP32-Setup")) {
    lcd.clear(); setRow(0, "WiFi timeout!"); setRow(1, "Restarting...");
    delay(2000); ESP.restart();
  }

  lcd.clear();
  setRow(0, "WiFi Connected!");
  setRow(1, WiFi.localIP().toString());
  delay(1500);
  Serial.println("WiFi: " + WiFi.localIP().toString());
}

// ============================================================
// UDP DISCOVERY
// ============================================================
bool discoverServer() {
  lcd.clear();
  setRow(0, "Finding server..");

  udp.begin(UDP_PORT);
  unsigned long start   = millis();
  unsigned long lastDot = 0;
  int dot = 0;

  while (millis() - start < 15000) {
    if (millis() - lastDot >= 500) {
      showLoading("Searching", dot++);
      lastDot = millis();
    }

    int sz = udp.parsePacket();
    if (sz) {
      char buf[80]; int len = udp.read(buf, sizeof(buf)-1); buf[len] = '\0';
      String msg = String(buf);
      if (msg.startsWith("DEADLINE_SERVER:")) {
        String rest = msg.substring(16);
        int c = rest.indexOf(':');
        snprintf(serverURL, sizeof(serverURL), "http://%s:%s/data",
          rest.substring(0,c).c_str(), rest.substring(c+1).c_str());
        lcd.clear(); setRow(0, "Server found!"); setRow(1, rest.substring(0,c));
        delay(1500); udp.stop();
        Serial.println("Server: " + String(serverURL));
        return true;
      }
    }
    delay(100);
  }

  udp.stop();
  lcd.clear(); setRow(0, "No server found!"); setRow(1, "Start server.py");
  delay(2000);
  return false;
}

// ============================================================
// FETCH
// ============================================================
bool fetchAssignments() {
  if (WiFi.status() != WL_CONNECTED) { connectWifi(); return false; }

  lcd.clear();
  setRow(0, "Fetching data...");

  HTTPClient http;
  http.begin(serverURL);
  http.setTimeout(60000);

  volatile bool done = false;
  volatile int  code = 0;
  String payload = "";

  struct P { HTTPClient* h; volatile bool* d; volatile int* c; String* p; };
  P params = {&http, &done, &code, &payload};

  xTaskCreate([](void* arg) {
    P* p = (P*)arg;
    *(p->c) = p->h->GET();
    if (*(p->c) == HTTP_CODE_OK) *(p->p) = p->h->getString();
    p->h->end(); *(p->d) = true; vTaskDelete(NULL);
  }, "fetch", 8192, &params, 1, NULL);

  int dot = 0;
  while (!done) {
    showLoading("Scraping", dot++);
    delay(400);
  }

  if (code != HTTP_CODE_OK) { setRow(1, "Fetch failed!"); delay(1000); return false; }

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, payload)) { setRow(1, "Parse error!"); delay(1000); return false; }

  totalAssignments = 0;
  hasUnanswered    = false;

  for (JsonObject obj : doc.as<JsonArray>()) {
    if (totalAssignments >= 20) break;
    auto& a         = assignments[totalAssignments++];
    a.title         = obj["title"].as<String>();
    a.deadline      = obj["end_date"].as<String>();
    a.status        = obj["status"].as<String>();
    a.deadlineEpoch = parseDeadline(a.deadline);
    if (a.status == "Unanswered") hasUnanswered = true;
    Serial.println(String(totalAssignments) + ". " + a.title + " [" + a.status + "]");
  }

  setRow(1, "Got " + String(totalAssignments) + " tasks!");
  delay(1000);
  return true;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(21, 22);
  lcd.init(); lcd.clear(); lcd.backlight();

  connectWifi();

  if (!discoverServer()) {
    setRow(0, "Retrying...");
    if (!discoverServer()) {
      lcd.clear(); setRow(0, "No server found!"); setRow(1, "Start server.py");
      delay(3000); ESP.restart();
    }
  }

  lcd.clear(); setRow(0, "Syncing time..."); setRow(1, "Please wait...");
  if (!syncTime()) Serial.println("Time sync failed, using fallback.");

  if (fetchAssignments()) {
    displayAssignment(0);
    if (hasUnanswered) startAlarm();
  } else {
    lcd.clear(); setRow(0, "Fetch failed!"); setRow(1, "Check server...");
  }

  lastScroll = lastFetch = lastMarquee = lastBuzzer = lastChaosLcd = millis();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  checkAlarm();

  if (now - lastMarquee >= MARQUEE_MS) {
    tickRow(0); tickRow(1);
    lastMarquee = now;
  }

  if (hasUnanswered && alarmActive) {
    if (now - lastBuzzer >= 2500) {
      beepChaos();
      lastBuzzer = now;
    }
    if (now - lastChaosLcd >= 4000) {
      if (showChaos) {
        showChaos = false;
        displayAssignment(currentIndex);
      } else {
        setRow(0, String(chaosMessages[chaosLcdIndex++ % CHAOS_COUNT]));
        setRow(1, formatCountdown(assignments[currentIndex].deadlineEpoch - nowEpoch()));
        showChaos = true;
      }
      lastChaosLcd = now;
    }
  }

  if (now - lastScroll >= SCROLL_MS && totalAssignments > 0) {
    currentIndex = (currentIndex + 1) % totalAssignments;
    showChaos    = false;
    displayAssignment(currentIndex);
    lastScroll = now;
  }

  if (now - lastFetch >= FETCH_MS) {
    bool prev = hasUnanswered;
    if (strlen(serverURL) == 0) discoverServer();
    syncTime();
    fetchAssignments();
    if (hasUnanswered && (!prev || !alarmActive)) startAlarm();
    lastFetch = now;
  }
}