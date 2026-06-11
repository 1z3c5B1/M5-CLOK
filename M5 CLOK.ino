#include <M5StickCPlus.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "time.h"

Preferences prefs;
WebServer webServer(80);

String mySSID = "";
String myPASS = "";
String myCity = "Moscow"; 
String realWeather = "--"; 
String realTime = "00:00";
String realDate = "Mon, 01 Jan";
long gmtOffset = 10800;

String backupSSID = "";
String backupPASS = "";

enum State { SETUP_WIFI, PASS_INPUT, CONNECTING, WATCH_FACE, ERROR_SCREEN, MENU, 
             STOPWATCH, TIMER, FLASHLIGHT, SETTINGS, ALARM, CALCULATOR, SNAKE, WEB_SETUP, ALARM_RINGING };
State currentState = SETUP_WIFI;

String inputPass = "";
String inputText = "";
const char* row1 = "qwertyuiop";
const char* row2 = "asdfghjkl";
const char* row3 = "zxcvbnm"; 
const char* row4 = "0123456789";
const char* row5 = "@._-";

int currentRow = 0; 
int currentCol = 0; 
bool shiftMode = false;
bool numberMode = false;

int scannedNetworkCount = 0;
int selectedNetworkIndex = 0;
unsigned long lastActivity = 0;
bool screenSleep = false;

String errorMessage = "";
bool showPassword = false;

int menuItem = 0;
const char* menuItems[] = {"Stopwatch", "Timer", "Flashlight", "Alarm", "Calculator", "Snake", "Web Setup", "Settings", "Back"};
int menuCount = 9;

unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;
bool stopwatchRunning = false;

int timerMinutes = 5;
int timerSeconds = 0;
unsigned long myTimerStart = 0;
bool timerRunning = false;

bool flashlightOn = false;

int alarmHour = 7;
int alarmMinute = 0;
bool alarmEnabled = false;
int alarmSnoozeCount = 0;

String calcDisplay = "0";
String calcFirst = "";
char calcOp = ' ';
bool calcNewNumber = true;
int calcIdx = 0;

#define GRID_W 24
#define GRID_H 13
#define CELL 6
struct Point { int x; int y; };
Point snake[100];
int snakeLen = 3;
int snakeDir = 0;
Point apple;
int snakeScore = 0;
unsigned long lastSnakeMove = 0;
bool snakeGameOver = false;

// === ПИЩАЛКА (исправлено для ESP32 Core 3.x) ===
#define BEEP_PIN 25

void beep(int freq, int duration) {
  ledcWriteTone(BEEP_PIN, freq);
  delay(duration);
  ledcWriteTone(BEEP_PIN, 0);
}

void beepInit() {
  ledcAttach(BEEP_PIN, 1000, 8);
  ledcWriteTone(BEEP_PIN, 0);
}

// === ПОГОДА И ВРЕМЯ ===
void getInternetData() {
  if (WiFi.status() != WL_CONNECTED) return;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 3000)) {
    char timeBuffer[16]; 
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
    realTime = String(timeBuffer);
    char dateBuffer[20];
    strftime(dateBuffer, sizeof(dateBuffer), "%a, %d %b", &timeinfo);
    realDate = String(dateBuffer);
  }
  HTTPClient http;
  String url = "http://wttr.in/" + myCity + "?format=%t&lang=ru";
  http.begin(url);
  http.setUserAgent("ESP32");
  int code = http.GET();
  if (code > 0) {
    String res = http.getString();
    res.replace("\n", "");
    res.replace("+", "");
    if(res.length() > 0 && res.length() < 10) realWeather = res + "C";
  }
  http.end();
}

String trimString(String str) {
  int start = 0, end = str.length() - 1;
  while(start <= end && (str[start]==' '||str[start]=='\t'||str[start]=='\n')) start++;
  while(end >= start && (str[end]==' '||str[end]=='\t'||str[end]=='\n')) end--;
  if(start > end) return "";
  return str.substring(start, end + 1);
}

// === ВЕБ-ИНТЕРФЕЙС ===
String getWebPage() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;background:#111;color:#fff;padding:20px;}";
  html += "input,select{width:100%;padding:12px;margin:8px 0;background:#222;color:#fff;border:1px solid #444;border-radius:6px;box-sizing:border-box;}";
  html += "button{width:100%;padding:14px;background:#ff9800;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;margin:5px 0;}";
  html += ".card{background:#1a1a1a;padding:15px;margin:10px 0;border-radius:8px;}</style></head><body>";
  html += "<h2>M5 Watch Setup</h2>";
  
  html += "<div class='card'><b>WiFi:</b> " + mySSID + "<br><b>Status:</b> ";
  html += (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  if(WiFi.status() == WL_CONNECTED) html += " | IP: " + WiFi.localIP().toString();
  html += "</div>";
  
  html += "<div class='card'><h3>WiFi</h3><form action='/wifi' method='POST'>";
  html += "<label>SSID:</label><input name='ssid' value='" + mySSID + "'>";
  html += "<label>Password:</label><input type='text' name='pass' value='" + myPASS + "'>";
  html += "<button>Save WiFi (reboot)</button></form></div>";
  
  html += "<div class='card'><h3>City & Time</h3><form action='/city' method='POST'>";
  html += "<label>City (English):</label><input name='city' value='" + myCity + "'>";
  html += "<label>GMT offset (hours):</label><input type='number' name='tz' value='" + String(gmtOffset/3600) + "'>";
  html += "<button>Save</button></form></div>";
  
  html += "<div class='card'><h3>Alarm</h3><form action='/alarm' method='POST'>";
  html += "<label>Hour (0-23):</label><input type='number' name='h' min='0' max='23' value='" + String(alarmHour) + "'>";
  html += "<label>Minute (0-59):</label><input type='number' name='m' min='0' max='59' value='" + String(alarmMinute) + "'>";
  html += "<label>Enabled:</label><select name='en'><option value='1'" + String(alarmEnabled?" selected":"") + ">Yes</option><option value='0'" + String(!alarmEnabled?" selected":"") + ">No</option></select>";
  html += "<button>Save Alarm</button></form></div>";
  
  html += "<div class='card'><b>Time:</b> " + realTime + " | <b>Weather:</b> " + realWeather + "</div>";
  html += "</body></html>";
  return html;
}

void setupWebServer() {
  webServer.on("/", []() { webServer.send(200, "text/html", getWebPage()); });
  
  webServer.on("/wifi", []() {
    String newSSID = trimString(webServer.arg("ssid"));
    String newPASS = trimString(webServer.arg("pass"));
    if(newSSID == "") {
      webServer.send(400, "text/html", "<html><body style='background:#111;color:#fff'><h2>SSID cannot be empty!</h2><a href='/'>Back</a></body></html>");
      return;
    }
    prefs.begin("nemo", false);
    prefs.putString("ssid", newSSID);
    prefs.putString("pass", newPASS);
    prefs.end();
    webServer.send(200, "text/html", "<html><body style='background:#111;color:#fff'><h2>Saved! Rebooting...</h2></body></html>");
    delay(2000);
    ESP.restart();
  });
  
  webServer.on("/city", []() {
    myCity = trimString(webServer.arg("city"));
    gmtOffset = webServer.arg("tz").toInt() * 3600;
    prefs.begin("nemo", false);
    prefs.putString("city", myCity);
    prefs.putLong("tz", gmtOffset);
    prefs.end();
    configTime(gmtOffset, 0, "pool.ntp.org");
    getInternetData();
    webServer.send(200, "text/html", "<html><body style='background:#111;color:#fff'><h2>Saved! <a href='/'>Back</a></h2></body></html>");
  });
  
  webServer.on("/alarm", []() {
    alarmHour = webServer.arg("h").toInt();
    alarmMinute = webServer.arg("m").toInt();
    alarmEnabled = webServer.arg("en") == "1";
    prefs.begin("nemo", false);
    prefs.putInt("alarmH", alarmHour);
    prefs.putInt("alarmM", alarmMinute);
    prefs.putBool("alarmEn", alarmEnabled);
    prefs.end();
    webServer.send(200, "text/html", "<html><body style='background:#111;color:#fff'><h2>Alarm saved! <a href='/'>Back</a></h2></body></html>");
  });
  
  webServer.begin();
}

// === ОТРИСОВКА ===
void drawWatchFace() {
  if (screenSleep) return;
  M5.Lcd.fillScreen(BLACK);
  float vbat = M5.Axp.GetBatVoltage();
  int pct = (vbat - 3.2) / (4.2 - 3.2) * 100;
  pct = constrain(pct, 0, 100);
  M5.Lcd.setTextColor(WHITE); M5.Lcd.setTextSize(1); M5.Lcd.setCursor(130, 5);
  M5.Lcd.printf("%d%%", pct);
  
  if (alarmEnabled) {
    M5.Lcd.setTextColor(RED); M5.Lcd.setTextSize(1); M5.Lcd.setCursor(10, 5);
    M5.Lcd.printf("ALM %02d:%02d", alarmHour, alarmMinute);
  }
  
  M5.Lcd.setTextSize(4); M5.Lcd.setTextColor(GREEN); M5.Lcd.setCursor(20, 50);
  M5.Lcd.print(realTime);
  
  M5.Lcd.setTextSize(2); M5.Lcd.setTextColor(WHITE); M5.Lcd.setCursor(20, 100);
  M5.Lcd.print(realDate);
  
  M5.Lcd.setTextSize(2); M5.Lcd.setTextColor(YELLOW); M5.Lcd.setCursor(20, 125);
  M5.Lcd.print(realWeather);
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 145);
  M5.Lcd.print("Hold M5+Bok: Menu");
}

void drawMenu() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 5); 
  M5.Lcd.print("MENU");
  
  M5.Lcd.setTextSize(1);
  int startIdx = (menuItem / 6) * 6;
  for(int i=0; i<6 && (startIdx+i)<menuCount; i++) {
    int idx = startIdx + i;
    M5.Lcd.setCursor(10, 30 + i*18);
    if (idx == menuItem) {
      M5.Lcd.setTextColor(RED);
      M5.Lcd.printf("> %s", menuItems[idx]);
    } else {
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.printf("  %s", menuItems[idx]);
    }
  }
  
  M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 145);
  M5.Lcd.print("Bok: Next | M5: OK");
}

void drawStopwatch() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("STOPWATCH");
  
  unsigned long elapsed = stopwatchRunning ? (millis() - stopwatchStart + stopwatchElapsed) : stopwatchElapsed;
  int hours = elapsed / 3600000;
  int minutes = (elapsed % 3600000) / 60000;
  int seconds = (elapsed % 60000) / 1000;
  int ms = (elapsed % 1000) / 10;
  
  M5.Lcd.setTextSize(3); M5.Lcd.setTextColor(GREEN); M5.Lcd.setCursor(10, 50);
  M5.Lcd.printf("%02d:%02d:%02d", hours, minutes, seconds);
  M5.Lcd.setTextSize(2); M5.Lcd.setCursor(110, 55);
  M5.Lcd.printf(".%02d", ms);
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 100);
  M5.Lcd.print(stopwatchRunning ? "M5: Stop | Bok: Reset" : "M5: Start | Bok: Reset");
  M5.Lcd.setCursor(10, 115);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawTimer() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("TIMER");
  
  if (timerRunning) {
    unsigned long elapsed = millis() - myTimerStart;
    long remaining = (timerMinutes * 60L + timerSeconds) * 1000L - elapsed;
    
    if (remaining <= 0) {
      timerRunning = false;
      M5.Lcd.setTextSize(3); M5.Lcd.setTextColor(RED); M5.Lcd.setCursor(30, 50);
      M5.Lcd.print("DONE!");
      beep(1000, 200); delay(200);
      beep(1000, 200); delay(200);
      beep(1000, 200);
    } else {
      int min = remaining / 60000;
      int sec = (remaining % 60000) / 1000;
      M5.Lcd.setTextSize(4); M5.Lcd.setTextColor(YELLOW); M5.Lcd.setCursor(30, 50);
      M5.Lcd.printf("%02d:%02d", min, sec);
    }
  } else {
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE); M5.Lcd.setCursor(20, 40);
    M5.Lcd.print("Set time:");
    M5.Lcd.setTextSize(4); M5.Lcd.setTextColor(YELLOW); M5.Lcd.setCursor(30, 60);
    M5.Lcd.printf("%02d:%02d", timerMinutes, timerSeconds);
  }
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 100);
  M5.Lcd.print(timerRunning ? "M5: Stop | Bok: +1min" : "M5: Start | Bok: +1min");
  M5.Lcd.setCursor(10, 115);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawFlashlight() {
  if (flashlightOn) {
    M5.Lcd.fillScreen(WHITE);
    M5.Axp.ScreenBreath(100);
  } else {
    M5.Lcd.fillScreen(BLACK);
    M5.Axp.ScreenBreath(10);
    M5.Lcd.setTextColor(WHITE); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10);
    M5.Lcd.print("FLASHLIGHT");
    M5.Lcd.setTextSize(1); M5.Lcd.setCursor(10, 60);
    M5.Lcd.print("M5: ON/OFF");
    M5.Lcd.setCursor(10, 80);
    M5.Lcd.print("Hold M5+Bok: Back");
  }
}

void drawAlarm() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("ALARM");
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE); M5.Lcd.setCursor(10, 40);
  M5.Lcd.printf("Status: %s", alarmEnabled ? "ON" : "OFF");
  
  M5.Lcd.setTextSize(4); M5.Lcd.setTextColor(YELLOW); M5.Lcd.setCursor(30, 60);
  M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute);
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 100);
  M5.Lcd.print("M5: On/Off | Bok: Set Time");
  M5.Lcd.setCursor(10, 115);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawCalculator() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("CALCULATOR");
  
  M5.Lcd.setTextSize(3); M5.Lcd.setTextColor(GREEN); M5.Lcd.setCursor(10, 40);
  M5.Lcd.print(calcDisplay);
  
  if (calcOp != ' ') {
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(YELLOW); M5.Lcd.setCursor(10, 80);
    M5.Lcd.printf("Op: %s %c", calcFirst.c_str(), calcOp);
  }
  
  const char* calcChars = "0123456789+-*/=C";
  M5.Lcd.setTextSize(2); M5.Lcd.setTextColor(RED); M5.Lcd.setCursor(10, 100);
  M5.Lcd.print("Next: "); M5.Lcd.print(calcChars[calcIdx]);
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 125);
  M5.Lcd.print("M5: Next | Bok: Type");
  M5.Lcd.setCursor(10, 140);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawSnake() {
  M5.Lcd.fillScreen(BLACK);
  
  if (snakeGameOver) {
    M5.Lcd.setTextColor(RED); M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 50); M5.Lcd.print("GAME OVER");
    M5.Lcd.setCursor(30, 80); M5.Lcd.printf("Score: %d", snakeScore);
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(DARKGREY);
    M5.Lcd.setCursor(20, 110); M5.Lcd.print("M5: Restart");
    M5.Lcd.setCursor(20, 125); M5.Lcd.print("Hold M5+Bok: Back");
    return;
  }
  
  M5.Lcd.fillRect(apple.x*CELL, apple.y*CELL, CELL, CELL, RED);
  
  for(int i=0; i<snakeLen; i++) {
    M5.Lcd.fillRect(snake[i].x*CELL, snake[i].y*CELL, CELL-1, CELL-1, i==0 ? GREEN : 0x07E0);
  }
  
  M5.Lcd.setTextColor(WHITE); M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, GRID_H*CELL + 2);
  M5.Lcd.printf("Score:%d Bok:Turn Hold:Bk", snakeScore);
}

void drawWebSetup() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("WEB SETUP");
  
  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(10, 40); M5.Lcd.print("Open in browser:");
    M5.Lcd.setTextSize(2); M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.setCursor(10, 60); M5.Lcd.print(WiFi.localIP().toString());
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setCursor(10, 90); M5.Lcd.print("Server running...");
  } else {
    M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(RED);
    M5.Lcd.setCursor(10, 50); M5.Lcd.print("No WiFi!");
  }
  
  M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 130);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawSettings() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); 
  M5.Lcd.print("SETTINGS");
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(10, 40); M5.Lcd.printf("City: %s", myCity.c_str());
  M5.Lcd.setCursor(10, 55); M5.Lcd.printf("WiFi: %s", mySSID.c_str());
  M5.Lcd.setCursor(10, 70); M5.Lcd.printf("GMT: %+d", gmtOffset/3600);
  M5.Lcd.setCursor(10, 85); M5.Lcd.printf("Alarm: %02d:%02d %s", alarmHour, alarmMinute, alarmEnabled?"ON":"OFF");
  
  M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 110);
  M5.Lcd.print("M5: City | Bok: WiFi");
  M5.Lcd.setCursor(10, 125);
  M5.Lcd.print("Hold M5+Bok: Back");
}

void drawSetupWifi() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10); M5.Lcd.print("SELECT WIFI");
  M5.Lcd.setTextSize(1);
  if (scannedNetworkCount > 0) {
    M5.Lcd.setTextColor(WHITE); M5.Lcd.setCursor(10, 40);
    M5.Lcd.printf("> %s", WiFi.SSID(selectedNetworkIndex).c_str());
    M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 60);
    if (scannedNetworkCount > 1) {
        int nextIdx = (selectedNetworkIndex + 1) % scannedNetworkCount;
        M5.Lcd.printf("  %s", WiFi.SSID(nextIdx).c_str());
    }
    M5.Lcd.setCursor(10, 100); M5.Lcd.print("Bok: Next | M5: Select");
  } else {
    M5.Lcd.setCursor(10, 40); M5.Lcd.print("Scanning...");
  }
}

void drawKeyboard() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(GREEN); M5.Lcd.setTextSize(1); M5.Lcd.setCursor(10, 5); 
  if (!numberMode) M5.Lcd.printf("SSID: %s", mySSID.c_str());
  else M5.Lcd.print("INPUT");
  
  M5.Lcd.setCursor(120, 5);
  if (shiftMode) { M5.Lcd.setTextColor(ORANGE); M5.Lcd.print("A"); }
  else { M5.Lcd.setTextColor(DARKGREY); M5.Lcd.print("a"); }
  
  M5.Lcd.setTextSize(2); M5.Lcd.setTextColor(WHITE); M5.Lcd.setCursor(10, 25);
  String display = numberMode ? inputText : inputPass;
  if (display.length() > 10) M5.Lcd.print("..." + display.substring(display.length()-8) + "_");
  else M5.Lcd.print(display + "_");
  
  M5.Lcd.setTextSize(1);
  const char* rows[] = {row1, row2, row3, row4, row5};
  int rowLens[] = {10, 9, 7, 10, 4};
  
  for(int r=0; r<5; r++) {
    M5.Lcd.setCursor(10, 50 + r*15);
    for(int i=0; i<rowLens[r]; i++) {
       if (currentRow == r && currentCol == i) M5.Lcd.setTextColor(RED); 
       else M5.Lcd.setTextColor(WHITE);
       char c = rows[r][i];
       if (shiftMode && c >= 'a' && c <= 'z') c = c - 32;
       M5.Lcd.print(c); M5.Lcd.print(" ");
    }
  }
  
  M5.Lcd.setCursor(10, 125);
  M5.Lcd.setTextColor(currentRow==5&&currentCol==0 ? ORANGE : DARKGREY);
  M5.Lcd.print("[SH] ");
  M5.Lcd.setTextColor(currentRow==5&&currentCol==1 ? ORANGE : DARKGREY);
  M5.Lcd.print("[DEL] ");
  M5.Lcd.setTextColor(currentRow==5&&currentCol==2 ? GREEN : DARKGREY);
  M5.Lcd.print("[OK]");
  
  M5.Lcd.setTextColor(DARKGREY); M5.Lcd.setCursor(10, 140);
  M5.Lcd.print("Bok: Row | M5: Move");
}

void drawConnecting() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); 
  M5.Lcd.setCursor(10, 10); M5.Lcd.print("CONNECTING");
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE); 
  M5.Lcd.setCursor(10, 40); M5.Lcd.printf("SSID: %s", mySSID.c_str());
  M5.Lcd.setCursor(10, 55); M5.Lcd.printf("Pass: %d chars", myPASS.length());
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setCursor(10, 130);
  M5.Lcd.print("Hold M5+Bok: Cancel");
}

void drawError() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(RED); M5.Lcd.setTextSize(2); 
  M5.Lcd.setCursor(10, 10); M5.Lcd.print("WIFI ERROR");
  
  M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(10, 35); M5.Lcd.printf("SSID: %s", mySSID.c_str());
  M5.Lcd.setCursor(10, 50);
  if (showPassword) M5.Lcd.printf("Pass: %s", myPASS.c_str());
  else {
    String hidden = "";
    for(int i=0; i<myPASS.length(); i++) hidden += "*";
    M5.Lcd.printf("Pass: %s", hidden.c_str());
  }
  
  M5.Lcd.setCursor(10, 65); M5.Lcd.setTextColor(RED);
  M5.Lcd.print(errorMessage);
  
  M5.Lcd.setCursor(10, 95); M5.Lcd.setTextColor(DARKGREY);
  M5.Lcd.print("M5: Show | Bok: Retry");
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.print("Hold M5+Bok: Change WiFi");
}

void drawAlarmRinging() {
  M5.Lcd.fillScreen(RED);
  M5.Lcd.setTextColor(WHITE); M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(20, 40); M5.Lcd.print("ALARM!");
  M5.Lcd.setTextSize(2); M5.Lcd.setCursor(30, 80);
  M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute);
  M5.Lcd.setTextSize(1); M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("Any button: Stop");
}

// === ПОДКЛЮЧЕНИЕ WIFI ===
bool connectToWiFi(const char* ssid, const char* password) {
  drawConnecting();
  
  WiFi.mode(WIFI_OFF); delay(100);
  WiFi.mode(WIFI_STA); delay(100);
  WiFi.disconnect(true); delay(500);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500); attempts++;
    M5.Lcd.setCursor(10, 80);
    M5.Lcd.printf("Attempt %d/60", attempts);
    
    M5.update();
    if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
      unsigned long holdStart = millis();
      while(M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
        M5.update();
        if (millis() - holdStart > 1500) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          prefs.begin("nemo", false);
          prefs.remove("ssid");
          prefs.remove("pass");
          prefs.end();
          mySSID = "";
          myPASS = "";
          beep(300, 200);
          return false;
        }
        delay(50);
      }
    }
    
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECT_FAILED) { errorMessage = "Wrong password!"; return false; }
    if (status == WL_NO_SSID_AVAIL) { errorMessage = "Network not found!"; return false; }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    backupSSID = String(ssid);
    backupPASS = String(password);
    prefs.begin("nemo", false);
    prefs.putString("backupSSID", backupSSID);
    prefs.putString("backupPASS", backupPASS);
    prefs.end();
    return true;
  }
  
  errorMessage = "Timeout (30s)";
  return false;
}

void initSnake() {
  snakeLen = 3;
  snakeDir = 0;
  snakeScore = 0;
  snakeGameOver = false;
  for(int i=0; i<snakeLen; i++) {
    snake[i].x = 5 - i;
    snake[i].y = GRID_H/2;
  }
  apple.x = GRID_W - 3;
  apple.y = GRID_H/2;
}

// === SETUP ===
void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); 
  M5.Axp.ScreenBreath(10); 
  beepInit();
  
  M5.update();
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    unsigned long holdStart = millis();
    while(M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
      M5.update();
      if (millis() - holdStart > 2000) {
        M5.Lcd.fillScreen(RED); M5.Lcd.setCursor(10, 40); M5.Lcd.print("RESETTING...");
        beep(500, 200);
        prefs.clear(); delay(1000); ESP.restart();
      }
    }
  }

  prefs.begin("nemo", true);
  mySSID = prefs.getString("ssid", "");
  myPASS = prefs.getString("pass", "");
  myCity = prefs.getString("city", "Moscow");
  gmtOffset = prefs.getLong("tz", 10800);
  alarmHour = prefs.getInt("alarmH", 7);
  alarmMinute = prefs.getInt("alarmM", 0);
  alarmEnabled = prefs.getBool("alarmEn", false);
  backupSSID = prefs.getString("backupSSID", "");
  backupPASS = prefs.getString("backupPASS", "");
  prefs.end();

  if (mySSID == "") {
    currentState = SETUP_WIFI;
    WiFi.mode(WIFI_STA);
    scannedNetworkCount = WiFi.scanNetworks();
    drawSetupWifi();
  } else {
    if (connectToWiFi(mySSID.c_str(), myPASS.c_str())) {
      configTime(gmtOffset, 0, "pool.ntp.org");
      getInternetData();
      setupWebServer();
      currentState = WATCH_FACE;
      drawWatchFace();
    } else {
      showPassword = false;
      currentState = ERROR_SCREEN;
      drawError();
    }
  }
}

// === LOOP ===
void loop() {
  M5.update();
  
  bool btnTop = M5.BtnB.wasPressed();
  bool btnM5 = M5.BtnA.wasPressed();

  if (currentState == WEB_SETUP && WiFi.status() == WL_CONNECTED) {
    webServer.handleClient();
  }

  // Проверка будильника
  if (currentState == WATCH_FACE && alarmEnabled) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute && timeinfo.tm_sec == 0) {
        currentState = ALARM_RINGING;
        drawAlarmRinging();
        beep(1000, 500);
      }
    }
  }
  
  if (currentState == ALARM_RINGING) {
    static unsigned long lastBeep = 0;
    if (millis() - lastBeep > 800) {
      beep(1000, 300);
      lastBeep = millis();
    }
    if (btnM5 || btnTop) {
      currentState = WATCH_FACE;
      alarmSnoozeCount = 0;
      drawWatchFace();
    }
    return;
  }

  // Долгое нажатие для меню/назад
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    unsigned long holdStart = millis();
    while(M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
      M5.update();
      if (millis() - holdStart > 2000) {
        if (currentState == WATCH_FACE) {
          currentState = MENU; menuItem = 0; drawMenu();
        } else if (currentState == ERROR_SCREEN) {
          currentState = SETUP_WIFI;
          WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(100);
          WiFi.mode(WIFI_STA);
          scannedNetworkCount = WiFi.scanNetworks();
          drawSetupWifi();
        } else if (currentState != SETUP_WIFI && currentState != PASS_INPUT && currentState != CONNECTING) {
          if (flashlightOn) { flashlightOn = false; M5.Axp.ScreenBreath(10); }
          currentState = MENU; drawMenu();
        }
        beep(800, 50);
        while(M5.BtnA.isPressed() || M5.BtnB.isPressed()) { M5.update(); delay(10); }
        return;
      }
    }
  }

  if (millis() - lastActivity > 30000 && !screenSleep && currentState == WATCH_FACE) {
    M5.Axp.ScreenBreath(0); 
    screenSleep = true;
  }
  
  if (screenSleep && (btnM5 || btnTop)) {
    M5.Axp.ScreenBreath(10);
    screenSleep = false;
    lastActivity = millis();
    drawWatchFace();
    return;
  }

  // --- SETUP_WIFI ---
  if (currentState == SETUP_WIFI) {
    if (btnTop && scannedNetworkCount > 0) {
      selectedNetworkIndex = (selectedNetworkIndex + 1) % scannedNetworkCount;
      drawSetupWifi();
      lastActivity = millis();
    }
    if (btnM5 && scannedNetworkCount > 0) {
      mySSID = WiFi.SSID(selectedNetworkIndex);
      inputPass = "";
      currentRow = 0; currentCol = 0;
      shiftMode = false; numberMode = false;
      currentState = PASS_INPUT;
      drawKeyboard();
      lastActivity = millis();
    }
    return;
  }

  // --- PASS_INPUT ---
  if (currentState == PASS_INPUT) {
    if (btnTop) {
       currentRow++;
       if (currentRow > 5) currentRow = 0; 
       currentCol = 0; 
       drawKeyboard();
       lastActivity = millis();
    }
    if (btnM5) {
       if (currentRow == 5) {
         currentCol = (currentCol + 1) % 3;
       } else {
         int rowLens[] = {10, 9, 7, 10, 4};
         currentCol = (currentCol + 1) % rowLens[currentRow];
       }
       drawKeyboard();
       lastActivity = millis();
    }
    
    static unsigned long lastKeyPress = 0;
    if (btnM5 || btnTop) lastKeyPress = millis();
    
    if (millis() - lastKeyPress > 1200 && lastKeyPress > 0) {
       lastKeyPress = 0;
       
       if (currentRow == 5) {
         if (currentCol == 0) { // SHIFT
           shiftMode = !shiftMode;
           beep(1200, 30);
           drawKeyboard();
         } else if (currentCol == 1) { // DEL
           if (inputPass.length() > 0) { inputPass.remove(inputPass.length() - 1); drawKeyboard(); }
         } else if (currentCol == 2) { // OK
           myPASS = inputPass;
           prefs.begin("nemo", false);
           prefs.putString("ssid", mySSID);
           prefs.putString("pass", myPASS);
           prefs.putString("city", myCity);
           prefs.putLong("tz", gmtOffset);
           prefs.end();
           if (connectToWiFi(mySSID.c_str(), myPASS.c_str())) {
             configTime(gmtOffset, 0, "pool.ntp.org");
             getInternetData();
             setupWebServer();
             currentState = WATCH_FACE;
             drawWatchFace();
           } else {
             showPassword = false;
             currentState = ERROR_SCREEN;
             drawError();
           }
         }
       } else {
         const char* rows[] = {row1, row2, row3, row4, row5};
         char c = rows[currentRow][currentCol];
         if (shiftMode && c >= 'a' && c <= 'z') c = c - 32;
         inputPass += c;
         beep(1500, 20);
         drawKeyboard();
       }
    }
    return;
  }

  // --- ERROR_SCREEN ---
  if (currentState == ERROR_SCREEN) {
    if (btnM5) { showPassword = !showPassword; drawError(); lastActivity = millis(); }
    if (btnTop) {
      if (connectToWiFi(mySSID.c_str(), myPASS.c_str())) {
        configTime(gmtOffset, 0, "pool.ntp.org");
        getInternetData();
        setupWebServer();
        currentState = WATCH_FACE;
        drawWatchFace();
      } else { showPassword = false; drawError(); }
      lastActivity = millis();
    }
    return;
  }

  // --- MENU ---
  if (currentState == MENU) {
    if (btnTop) {
      menuItem = (menuItem + 1) % menuCount;
      drawMenu();
      lastActivity = millis();
    }
    if (btnM5) {
      beep(1000, 30);
      if (menuItem == 0) { currentState = STOPWATCH; stopwatchElapsed = 0; stopwatchRunning = false; drawStopwatch(); }
      else if (menuItem == 1) { currentState = TIMER; timerRunning = false; drawTimer(); }
      else if (menuItem == 2) { currentState = FLASHLIGHT; flashlightOn = false; drawFlashlight(); }
      else if (menuItem == 3) { currentState = ALARM; drawAlarm(); }
      else if (menuItem == 4) { currentState = CALCULATOR; calcDisplay="0"; calcFirst=""; calcOp=' '; calcNewNumber=true; calcIdx=0; drawCalculator(); }
      else if (menuItem == 5) { currentState = SNAKE; initSnake(); drawSnake(); }
      else if (menuItem == 6) { currentState = WEB_SETUP; drawWebSetup(); }
      else if (menuItem == 7) { currentState = SETTINGS; drawSettings(); }
      else if (menuItem == 8) { currentState = WATCH_FACE; drawWatchFace(); }
      lastActivity = millis();
    }
    return;
  }

  // --- STOPWATCH ---
  if (currentState == STOPWATCH) {
    if (btnM5) {
      if (stopwatchRunning) { stopwatchElapsed += millis() - stopwatchStart; stopwatchRunning = false; }
      else { stopwatchStart = millis(); stopwatchRunning = true; }
      beep(1000, 30);
      drawStopwatch();
      lastActivity = millis();
    }
    if (btnTop) { stopwatchElapsed = 0; stopwatchRunning = false; drawStopwatch(); lastActivity = millis(); }
    if (stopwatchRunning) drawStopwatch();
    return;
  }

  // --- TIMER ---
  if (currentState == TIMER) {
    if (btnTop) {
      if (!timerRunning) { timerMinutes++; if (timerMinutes > 99) timerMinutes = 0; drawTimer(); }
      lastActivity = millis();
    }
    if (btnM5) {
      if (timerRunning) { timerRunning = false; }
      else { myTimerStart = millis(); timerRunning = true; }
      beep(1000, 30);
      drawTimer();
      lastActivity = millis();
    }
    if (timerRunning) drawTimer();
    return;
  }

  // --- FLASHLIGHT ---
  if (currentState == FLASHLIGHT) {
    if (btnM5) { flashlightOn = !flashlightOn; drawFlashlight(); lastActivity = millis(); }
    return;
  }

  // --- ALARM ---
  if (currentState == ALARM) {
    if (btnM5) {
      alarmEnabled = !alarmEnabled;
      prefs.begin("nemo", false);
      prefs.putBool("alarmEn", alarmEnabled);
      prefs.end();
      if (alarmEnabled) beep(1500, 100);
      drawAlarm();
      lastActivity = millis();
    }
    if (btnTop) {
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextColor(CYAN); M5.Lcd.setTextSize(2); M5.Lcd.setCursor(10, 10);
      M5.Lcd.print("SET ALARM");
      M5.Lcd.setTextSize(1); M5.Lcd.setTextColor(WHITE);
      M5.Lcd.setCursor(10, 40);
      M5.Lcd.print("Use Web Setup or");
      M5.Lcd.setCursor(10, 55);
      M5.Lcd.print("Bok: +1 hour");
      M5.Lcd.setCursor(10, 70);
      M5.Lcd.print("M5: +1 minute");
      M5.Lcd.setCursor(10, 100);
      M5.Lcd.printf("Current: %02d:%02d", alarmHour, alarmMinute);
      M5.Lcd.setCursor(10, 130);
      M5.Lcd.print("Hold M5+Bok: Done");
      
      static int alarmSetStep = 0;
      alarmSetStep = 1;
      while(alarmSetStep) {
        M5.update();
        if (M5.BtnA.wasPressed()) { alarmMinute = (alarmMinute + 1) % 60; M5.Lcd.setCursor(10, 100); M5.Lcd.printf("Current: %02d:%02d", alarmHour, alarmMinute); }
        if (M5.BtnB.wasPressed()) { alarmHour = (alarmHour + 1) % 24; M5.Lcd.setCursor(10, 100); M5.Lcd.printf("Current: %02d:%02d", alarmHour, alarmMinute); }
        if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
          delay(1500);
          M5.update();
          if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
            prefs.begin("nemo", false);
            prefs.putInt("alarmH", alarmHour);
            prefs.putInt("alarmM", alarmMinute);
            prefs.end();
            alarmSetStep = 0;
            beep(1500, 100);
            drawAlarm();
          }
        }
        delay(50);
      }
      lastActivity = millis();
    }
    return;
  }

  // --- CALCULATOR ---
  if (currentState == CALCULATOR) {
    if (btnM5) {
      calcIdx = (calcIdx + 1) % 16;
      drawCalculator();
      lastActivity = millis();
    }
    if (btnTop) {
      const char* calcChars = "0123456789+-*/=C";
      char c = calcChars[calcIdx];
      
      if (c == 'C') {
        calcDisplay = "0"; calcFirst = ""; calcOp = ' '; calcNewNumber = true;
      } else if (c >= '0' && c <= '9') {
        if (calcNewNumber) { calcDisplay = String(c); calcNewNumber = false; }
        else { if (calcDisplay == "0") calcDisplay = String(c); else calcDisplay += c; }
      } else if (c == '=') {
        if (calcOp != ' ' && calcFirst != "") {
          double a = calcFirst.toDouble();
          double b = calcDisplay.toDouble();
          double r = 0;
          if (calcOp == '+') r = a + b;
          else if (calcOp == '-') r = a - b;
          else if (calcOp == '*') r = a * b;
          else if (calcOp == '/') r = (b != 0) ? a / b : 0;
          calcDisplay = String(r, 4);
          while(calcDisplay.endsWith("0") && calcDisplay.indexOf('.') >= 0) calcDisplay.remove(calcDisplay.length()-1);
          if (calcDisplay.endsWith(".")) calcDisplay.remove(calcDisplay.length()-1);
          calcFirst = ""; calcOp = ' '; calcNewNumber = true;
        }
      } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        calcFirst = calcDisplay;
        calcOp = c;
        calcNewNumber = true;
      }
      beep(1200, 20);
      drawCalculator();
      lastActivity = millis();
    }
    return;
  }

  // --- SNAKE ---
  if (currentState == SNAKE) {
    if (snakeGameOver) {
      if (btnM5) { initSnake(); }
      drawSnake();
      lastActivity = millis();
      return;
    }
    
    if (btnTop) {
      snakeDir = (snakeDir + 1) % 4;
    }
    
    if (millis() - lastSnakeMove > 150) {
      lastSnakeMove = millis();
      Point newHead = snake[0];
      if (snakeDir == 0) newHead.x++;
      else if (snakeDir == 1) newHead.y++;
      else if (snakeDir == 2) newHead.x--;
      else if (snakeDir == 3) newHead.y--;
      
      if (newHead.x < 0 || newHead.x >= GRID_W || newHead.y < 0 || newHead.y >= GRID_H) {
        snakeGameOver = true;
        beep(200, 500);
      } else {
        for(int i=0; i<snakeLen; i++) {
          if (snake[i].x == newHead.x && snake[i].y == newHead.y) {
            snakeGameOver = true;
            beep(200, 500);
            break;
          }
        }
        if (!snakeGameOver) {
          if (newHead.x == apple.x && newHead.y == apple.y) {
            snakeLen++;
            snakeScore += 10;
            beep(1500, 50);
            apple.x = random(0, GRID_W);
            apple.y = random(0, GRID_H);
          }
          for(int i=snakeLen-1; i>0; i--) snake[i] = snake[i-1];
          snake[0] = newHead;
        }
      }
      drawSnake();
    }
    return;
  }

  // --- WEB_SETUP ---
  if (currentState == WEB_SETUP) {
    return;
  }

  // --- SETTINGS ---
  if (currentState == SETTINGS) {
    if (btnM5) {
      myCity = (myCity == "Moscow") ? "Kaliningrad" : "Moscow";
      prefs.begin("nemo", false);
      prefs.putString("city", myCity);
      prefs.end();
      getInternetData();
      drawSettings();
      lastActivity = millis();
    }
    if (btnTop) {
      currentState = SETUP_WIFI;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF); delay(100);
      WiFi.mode(WIFI_STA);
      scannedNetworkCount = WiFi.scanNetworks();
      drawSetupWifi();
      lastActivity = millis();
    }
    return;
  }

  // --- WATCH_FACE ---
  if (currentState == WATCH_FACE) {
    lastActivity = millis();
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 600000) {
      getInternetData();
      drawWatchFace();
      lastUpdate = millis();
    }
    static int lastSec = -1;
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) {
      if(timeinfo.tm_sec != lastSec) {
        char timeBuffer[16]; 
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
        if(realTime != String(timeBuffer)) {
           realTime = String(timeBuffer);
           drawWatchFace();
        }
        lastSec = timeinfo.tm_sec;
      }
    }
  }
}