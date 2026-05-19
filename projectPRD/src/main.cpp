#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiUdp.h>

// ============================================================
// CONSTANTS
// ============================================================
const int SCROLL_INTERVAL = 8000;
const int FETCH_INTERVAL  = 1800000;
const int MARQUEE_SPEED   = 350;
const int BUZZER_PIN      = 23;
const int MUTE_PIN        = 18;
const int MUTE_DURATION   = 600000;
const int UDP_PORT        = 4210;
const int LCD_COLS        = 16;

// ============================================================
// GLOBALS
// ============================================================
LiquidCrystal_I2C lcd(0x27, LCD_COLS, 2);
WiFiUDP udp;

char serverURL[80] = "";
long serverTimeOffset = 0;

struct Assignment {
  String title;
  String deadline;
  String status;
  long   deadlineEpoch;
};

Assignment assignments[20];
int  totalAssignments = 0;
int  currentIndex     = 0;
bool hasUnanswered    = false;
bool isMuted          = false;

unsigned long lastScroll    = 0;
unsigned long lastFetch     = 0;
unsigned long lastBuzzer    = 0;
unsigned long lastLcdChaos  = 0;
unsigned long lastDebounce  = 0;
unsigned long lastMarquee   = 0;
unsigned long muteUntil     = 0;

int  chaosPhase    = 0;
int  chaosLcdIndex = 0;
bool showChaos     = false;

// Per-row marquee state
struct MarqueeState {
  int  offset    = 0;
  bool active    = false;
  String text    = "";
};

MarqueeState row0State;
MarqueeState row1State;

// ============================================================
// MARQUEE HELPERS
// ============================================================

// Set text for a row — auto-enables marquee if text > 16 chars
void setRow(int row, String text) {
  MarqueeState& s = (row == 0) ? row0State : row1State;
  s.text   = text;
  s.offset = 0;

  if (text.length() <= LCD_COLS) {
    // Short text — pad and print immediately, no scroll
    s.active = false;
    char buf[17];
    snprintf(buf, sizeof(buf), "%-16s", text.c_str());
    lcd.setCursor(0, row);
    lcd.print(buf);
  } else {
    s.active = true;
  }
}

// Advance marquee one step for a row
void tickRow(int row) {
  MarqueeState& s = (row == 0) ? row0State : row1State;
  if (!s.active) return;

  String scrollText = s.text + "    ";
  int    scrollLen  = scrollText.length();
  String visible    = "";

  for (int i = 0; i < LCD_COLS; i++) {
    visible += scrollText[(s.offset + i) % scrollLen];
  }

  lcd.setCursor(0, row);
  lcd.print(visible);
  s.offset = (s.offset + 1) % scrollLen;
}

// Tick both rows
void tickMarquee() {
  tickRow(0);
  tickRow(1);
}

// ============================================================
// BUZZER
// ============================================================
void beepSOS() {
  for (int i = 0; i < 3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(80); }
  delay(200);
  for (int i = 0; i < 3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(400); digitalWrite(BUZZER_PIN, LOW); delay(100); }
  delay(200);
  for (int i = 0; i < 3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(80); }
}

void beepPanic() {
  int pattern[] = {50, 30, 200, 50, 50, 300, 80, 40};
  int gaps[]    = {30, 20, 100, 30, 20, 150, 40, 30};
  for (int i = 0; i < 8; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(pattern[i]);
    digitalWrite(BUZZER_PIN, LOW);  delay(gaps[i]);
  }
}

void beepAlarm() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW);  delay(40);
    digitalWrite(BUZZER_PIN, HIGH); delay(160);
    digitalWrite(BUZZER_PIN, LOW);  delay(40);
  }
}

void beepAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(150);
    digitalWrite(BUZZER_PIN, LOW);  delay(100);
  }
}

void beepChaos() {
  switch (chaosPhase % 4) {
    case 0: beepPanic();              break;
    case 1: beepSOS();                break;
    case 2: beepAlarm();              break;
    case 3: beepPanic(); beepAlarm(); break;
  }
  chaosPhase++;
}

// ============================================================
// CHAOS LCD
// ============================================================
const char* chaosMessages[] = {
  "!! DEADLINE !!",
  "SUBMIT OR FAIL",
  "WHY R U READING",
  "GO DO UR TASK!!",
  "TUGAS MENUNGGU!",
  "WAKTU HABIS!!!",
  "PANIK SEKARANG!",
  "NO MORE MENFESS",
  "GG EZ NO REMATCH",
};
const int chaosCount = 9;

void flashChaosMessage() {
  setRow(0, String(chaosMessages[chaosLcdIndex % chaosCount]));
  chaosLcdIndex++;
  showChaos = true;
}

void restoreNormal() {
  showChaos = false;
}

// ============================================================
// TIME
// ============================================================
long getNowEpoch() {
  return (millis() / 1000) + serverTimeOffset;
}

bool syncTime() {
  String base    = String(serverURL);
  String timeURL = base.substring(0, base.lastIndexOf('/')) + "/time";
  Serial.println("Syncing time from: " + timeURL);

  HTTPClient http;
  http.begin(timeURL);
  http.setTimeout(10000);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.println("Time sync failed: " + String(code));
    http.end();
    return false;
  }

  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();

  if (error) { Serial.println("Time parse failed"); return false; }

  long serverNow   = doc["timestamp"];
  serverTimeOffset = serverNow - (long)(millis() / 1000);
  Serial.println("Time synced. Offset: " + String(serverTimeOffset));
  return true;
}

long parseDeadline(String deadline) {
  if (deadline.length() < 14) return 0;
  int dd = deadline.substring(0, 2).toInt();
  int mm = deadline.substring(3, 5).toInt();
  int yy = deadline.substring(6, 8).toInt() + 2000;
  int hh = deadline.substring(9, 11).toInt();
  int mn = deadline.substring(12, 14).toInt();

  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  long days = (yy - 1970) * 365L;
  for (int y = 1970; y < yy; y++) {
    if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days++;
  }
  for (int m = 0; m < mm - 1; m++) {
    days += daysInMonth[m];
    if (m == 1 && ((yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0))) days++;
  }
  days += dd - 1;
  return days * 86400L + hh * 3600L + mn * 60L;
}

String formatCountdown(long secondsLeft) {
  if (secondsLeft <= 0) return "OVERDUE!";
  long days  = secondsLeft / 86400;
  long hours = (secondsLeft % 86400) / 3600;
  long mins  = (secondsLeft % 3600) / 60;
  char buf[32];
  if (days > 0)       snprintf(buf, sizeof(buf), "%ldd %ldh remaining", days, hours);
  else if (hours > 0) snprintf(buf, sizeof(buf), "%ldh %ldm remaining", hours, mins);
  else                snprintf(buf, sizeof(buf), "%ldm remaining!", mins);
  return String(buf);
}

// ============================================================
// MUTE
// ============================================================
void checkMuteButton() {
  if (digitalRead(MUTE_PIN) == LOW && millis() - lastDebounce > 200) {
    lastDebounce = millis();
    isMuted      = true;
    muteUntil    = millis() + MUTE_DURATION;
    Serial.println("Muted for 10 minutes.");

    lcd.clear();
    setRow(0, "Muted 10 min!");
    setRow(1, "Enjoy the quiet");
    delay(1500);
    restoreNormal();
  }

  if (isMuted && millis() > muteUntil) {
    isMuted = false;
    Serial.println("Mute lifted. CHAOS RESUMES.");

    lcd.clear();
    setRow(0, "Mute lifted!");
    setRow(1, "Back to chaos...");
    delay(1000);
    restoreNormal();
    if (hasUnanswered) beepAlert();
  }
}

void showMuteStatus() {
  if (!isMuted) return;
  unsigned long remaining = (muteUntil - millis()) / 1000;
  unsigned long mins = remaining / 60;
  unsigned long secs = remaining % 60;

  char row0buf[32];
  snprintf(row0buf, sizeof(row0buf), "[MUTE] %02lu:%02lu left", mins, secs);
  setRow(0, String(row0buf));

  if (totalAssignments > 0) {
    setRow(1, assignments[currentIndex].title);
  }
}

// ============================================================
// UDP DISCOVERY
// ============================================================
bool discoverServer() {
  lcd.clear();
  setRow(0, "Finding server..");
  setRow(1, "Please wait...");

  udp.begin(UDP_PORT);
  Serial.println("Listening for server broadcast...");

  unsigned long start    = millis();
  unsigned long lastDot  = 0;
  int dotCount           = 0;

  while (millis() - start < 15000) {
    if (millis() - lastDot > 500) {
      String dots = "Searching";
      for (int i = 0; i < (dotCount % 4); i++) dots += ".";
      setRow(1, dots);
      dotCount++;
      lastDot = millis();
    }

    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buf[80];
      int  len = udp.read(buf, sizeof(buf) - 1);
      buf[len] = '\0';
      String msg = String(buf);
      Serial.println("UDP received: " + msg);

      if (msg.startsWith("DEADLINE_SERVER:")) {
        String rest  = msg.substring(16);
        int colonIdx = rest.indexOf(':');
        String ip    = rest.substring(0, colonIdx);
        String port  = rest.substring(colonIdx + 1);

        snprintf(serverURL, sizeof(serverURL), "http://%s:%s/data", ip.c_str(), port.c_str());
        Serial.println("Server found: " + String(serverURL));

        lcd.clear();
        setRow(0, "Server found!");
        setRow(1, ip);
        delay(1500);

        udp.stop();
        return true;
      }
    }
    delay(100);
  }

  udp.stop();
  Serial.println("Server not found!");
  lcd.clear();
  setRow(0, "No server found!");
  setRow(1, "Start server.py");
  delay(2000);
  return false;
}

// ============================================================
// WIFI
// ============================================================
void connectWifi() {
  lcd.clear();
  setRow(0, "Connecting WiFi");
  setRow(1, "Please wait...");

  WiFiManager wm;

  wm.setAPCallback([](WiFiManager* wm) {
    Serial.println("Config mode! Connect to ESP32-Setup");
    lcd.clear();
    setRow(0, "1.Join ESP32-Set");
    setRow(1, "2.Open 192.168.4.1");
  });

  wm.setConfigPortalTimeout(180);
  bool connected = wm.autoConnect("ESP32-Setup");

  if (!connected) {
    Serial.println("WiFiManager timeout, restarting...");
    lcd.clear();
    setRow(0, "WiFi timeout!");
    setRow(1, "Restarting...");
    delay(2000);
    ESP.restart();
  }

  Serial.println("Connected! IP: " + WiFi.localIP().toString());

  lcd.clear();
  setRow(0, "WiFi Connected!");
  setRow(1, WiFi.localIP().toString());
  delay(1500);
}

// ============================================================
// FETCH
// ============================================================
bool fetchAssignments() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    return false;
  }

  lcd.clear();
  setRow(0, "Fetching data...");

  HTTPClient http;
  http.begin(serverURL);
  http.setTimeout(60000);

  volatile bool fetchDone  = false;
  volatile int  httpResult = 0;
  String payload = "";

  struct FetchParams {
    HTTPClient*   http;
    volatile bool* done;
    volatile int*  code;
    String*        payload;
  };

  FetchParams params = { &http, &fetchDone, &httpResult, &payload };

  xTaskCreate([](void* arg) {
    FetchParams* p  = (FetchParams*)arg;
    *(p->code)      = p->http->GET();
    if (*(p->code) == HTTP_CODE_OK) *(p->payload) = p->http->getString();
    p->http->end();
    *(p->done) = true;
    vTaskDelete(NULL);
  }, "fetchTask", 8192, &params, 1, NULL);

  int dotCount = 0;
  while (!fetchDone) {
    String dots = "Scraping";
    for (int i = 0; i < (dotCount % 4) + 1; i++) dots += ".";
    setRow(1, dots);
    dotCount++;
    delay(400);
  }

  Serial.println("HTTP Code: " + String(httpResult));

  if (httpResult != HTTP_CODE_OK) {
    Serial.println("Fetch failed!");
    setRow(1, "Fetch failed!");
    delay(1000);
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("Parse failed: " + String(error.f_str()));
    setRow(1, "Parse error!");
    delay(1000);
    return false;
  }

  totalAssignments = 0;
  hasUnanswered    = false;
  JsonArray arr    = doc.as<JsonArray>();

  Serial.println("\n=== Parsed Assignments ===");
  int i = 1;
  for (JsonObject obj : arr) {
    if (totalAssignments >= 20) break;
    assignments[totalAssignments].title         = obj["title"].as<String>();
    assignments[totalAssignments].deadline      = obj["end_date"].as<String>();
    assignments[totalAssignments].status        = obj["status"].as<String>();
    assignments[totalAssignments].deadlineEpoch = parseDeadline(obj["end_date"].as<String>());
    if (assignments[totalAssignments].status == "Unanswered") hasUnanswered = true;
    Serial.println("--- #" + String(i++) + " ---");
    Serial.println("Title   : " + assignments[totalAssignments].title);
    Serial.println("Deadline: " + assignments[totalAssignments].deadline);
    Serial.println("Status  : " + assignments[totalAssignments].status);
    totalAssignments++;
  }
  Serial.println("==========================");
  Serial.println("Unanswered: " + String(hasUnanswered ? "YES - PANIC MODE" : "NO"));

  setRow(1, "Got " + String(totalAssignments) + " tasks!");
  delay(1000);
  return true;
}

// ============================================================
// DISPLAY
// ============================================================
void displayAssignment(int index) {
  if (totalAssignments == 0) {
    lcd.clear();
    setRow(0, "All done! (^_^)");
    setRow(1, "No pending tasks");
    return;
  }

  char prefix[7];
  snprintf(prefix, sizeof(prefix), "[%d/%d] ", index + 1, totalAssignments);
  setRow(0, String(prefix) + assignments[index].title);

  long secondsLeft = assignments[index].deadlineEpoch - getNowEpoch();
  setRow(1, formatCountdown(secondsLeft));

  Serial.println("Displaying [" + String(index + 1) + "/" + String(totalAssignments) + "] " + assignments[index].title);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(MUTE_PIN, INPUT_PULLUP);

  Wire.begin(21, 22);
  lcd.init();
  lcd.clear();
  lcd.backlight();

  connectWifi();

  if (!discoverServer()) {
    Serial.println("Retrying discovery...");
    setRow(0, "Retrying...");
    if (!discoverServer()) {
      lcd.clear();
      setRow(0, "No server found!");
      setRow(1, "Start server.py");
      delay(3000);
      ESP.restart();
    }
  }

  lcd.clear();
  setRow(0, "Syncing time...");
  setRow(1, "Please wait...");
  if (!syncTime()) Serial.println("Time sync failed, using fallback.");

  if (fetchAssignments()) {
    displayAssignment(0);
    if (hasUnanswered) {
      Serial.println("UNANSWERED DETECTED. INITIATING CHAOS.");
      beepAlert();
    }
  } else {
    lcd.clear();
    setRow(0, "Fetch failed!");
    setRow(1, "Check server...");
  }

  lastScroll   = millis();
  lastFetch    = millis();
  lastMarquee  = millis();
  lastBuzzer   = millis();
  lastLcdChaos = millis();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  checkMuteButton();

  // Marquee tick
  if (now - lastMarquee >= MARQUEE_SPEED) {
    if (isMuted) {
      showMuteStatus();
    } else {
      tickMarquee();
    }
    lastMarquee = now;
  }

  // Chaos mode
  if (hasUnanswered && !isMuted) {
    if (now - lastBuzzer >= 2500) {
      beepChaos();
      lastBuzzer = now;
    }

    if (now - lastLcdChaos >= 4000) {
      if (showChaos) {
        restoreNormal();
        displayAssignment(currentIndex);
      } else {
        flashChaosMessage();
        long secondsLeft = assignments[currentIndex].deadlineEpoch - getNowEpoch();
        setRow(1, formatCountdown(secondsLeft));
      }
      lastLcdChaos = now;
    }
  }

  // Scroll ke assignment berikutnya
  if (now - lastScroll >= SCROLL_INTERVAL && totalAssignments > 0) {
    currentIndex = (currentIndex + 1) % totalAssignments;
    showChaos    = false;
    displayAssignment(currentIndex);
    lastScroll = now;
  }

  // Re-fetch
  if (now - lastFetch >= FETCH_INTERVAL) {
    Serial.println("Re-fetching...");
    if (strlen(serverURL) == 0) discoverServer();
    syncTime();
    fetchAssignments();
    if (hasUnanswered && !isMuted) beepAlert();
    lastFetch = now;
  }
}