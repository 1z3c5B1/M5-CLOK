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

// === ЦВЕТА СОВРЕМЕННОГО ИНТЕРФЕЙСА ===
#define COLOR_BG       0x0000   // Чёрный фон
#define COLOR_BG2      0x18E3   // Тёмно-серый для карточек
#define COLOR_ACCENT   0x07FF   // Голубой акцент
#define COLOR_ACCENT2  0xF81F   // Розовый акцент
#define COLOR_GREEN    0x07E0   // Зелёный
#define COLOR_YELLOW   0xFFE0   // Жёлтый
#define COLOR_ORANGE   0xFD20   // Оранжевый
#define COLOR_RED      0xF800   // Красный
#define COLOR_WHITE    0xFFFF   // Белый
#define COLOR_GREY     0x7BEF   // Серый
#define COLOR_DARKGREY 0x4208   // Тёмно-серый

// === ПИЩАЛКА ===
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

// === КРАСИВАЯ ОТРИСОВКА БАТАРЕИ ===
void drawBattery(int x, int y) {
  float vbat = M5.Axp.GetBatVoltage();
  int pct = (vbat - 3.2) / (4.2 - 3.2) * 100;
  pct = constrain(pct, 0, 100);
  
  // Выбираем цвет по уровню заряда
  uint16_t batColor = COLOR_GREEN;
  if (pct < 50) batColor = COLOR_YELLOW;
  if (pct < 20) batColor = COLOR_RED;
  
  // Корпус батареи
  M5.Lcd.drawRect(x, y, 26, 12, COLOR_GREY);
  M5.Lcd.fillRect(x + 26, y + 3, 2, 6, COLOR_GREY);
  
  // Заполнение
  int fillWidth = map(pct, 0, 100, 0, 22);
  M5.Lcd.fillRect(x + 2, y + 2, fillWidth, 8, batColor);
  
  // Процент рядом
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(x - 25, y + 3);
  M5.Lcd.printf("%d%%", pct);
}

// === КРАСИВЫЙ ЦИФЕРБЛАТ ===
void drawWatchFace() {
  if (screenSleep) return;
  M5.Lcd.fillScreen(COLOR_BG);
  
  // Верхняя панель с временем и батареей
  M5.Lcd.fillRect(0, 0, 240, 20, COLOR_BG2);
  
  // Дата вверху слева
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, 6);
  M5.Lcd.print(realDate);
  
  // Батарея справа вверху
  drawBattery(210, 4);
  
  // Индикатор будильника (если включен)
  if (alarmEnabled) {
    M5.Lcd.setTextColor(COLOR_ORANGE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(120, 6);
    M5.Lcd.printf("ALM %02d:%02d", alarmHour, alarmMinute);
  }
  
  // ОГРОМНОЕ ВРЕМЯ по центру
  M5.Lcd.setTextSize(6);
  M5.Lcd.setTextColor(COLOR_WHITE);
  // Вычисляем ширину текста для центрирования
  int timeWidth = realTime.length() * 36; // примерная ширина для размера 6
  int timeX = (240 - timeWidth) / 2;
  if (timeX < 0) timeX = 5;
  M5.Lcd.setCursor(timeX, 30);
  M5.Lcd.print(realTime);
  
  // Декоративная линия
  M5.Lcd.drawLine(20, 95, 220, 95, COLOR_ACCENT);
  
  // Карточка с погодой
  M5.Lcd.fillRoundRect(20, 100, 200, 30, 5, COLOR_BG2);
  M5.Lcd.drawRoundRect(20, 100, 200, 30, 5, COLOR_ACCENT);
  
  // Иконка погоды
  M5.Lcd.setTextColor(COLOR_YELLOW);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(30, 107);
  M5.Lcd.print("*"); // звёздочка как "иконка"
  
  // Город и погода
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(50, 103);
  M5.Lcd.print(myCity);
  
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(COLOR_YELLOW);
  M5.Lcd.setCursor(50, 113);
  M5.Lcd.print(realWeather);
  
  // Подсказка внизу
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DARKGREY);
  M5.Lcd.setCursor(80, 127);
  M5.Lcd.print("Hold M5+Bok = Menu");
}

// === КРАСИВОЕ МЕНЮ ===
void drawMenu() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  // Заголовок
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ACCENT);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(80, 4);
  M5.Lcd.print("MENU");
  
  // Пункты меню
  M5.Lcd.setTextSize(1);
  int startIdx = (menuItem / 5) * 5;
  for(int i=0; i<5 && (startIdx+i)<menuCount; i++) {
    int idx = startIdx + i;
    int y = 28 + i*20;
    
    if (idx == menuItem) {
      // Выбранный пункт - подсветка
      M5.Lcd.fillRoundRect(10, y, 220, 18, 3, COLOR_ACCENT2);
      M5.Lcd.setTextColor(COLOR_WHITE);
      M5.Lcd.setCursor(20, y+4);
      M5.Lcd.printf("> %s", menuItems[idx]);
    } else {
      M5.Lcd.setTextColor(COLOR_GREY);
      M5.Lcd.setCursor(20, y+4);
      M5.Lcd.printf("  %s", menuItems[idx]);
    }
  }
  
  // Подсказки внизу
  M5.Lcd.fillRect(0, 125, 240, 10, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_DARKGREY);
  M5.Lcd.setCursor(10, 127);
  M5.Lcd.print("Bok:Next  M5:OK  Hold:Bk");
}

// === КРАСИВЫЙ СЕКУНДОМЕР ===
void drawStopwatch() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  // Заголовок
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_GREEN);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(60, 4);
  M5.Lcd.print("STOPWATCH");
  
  unsigned long elapsed = stopwatchRunning ? (millis() - stopwatchStart + stopwatchElapsed) : stopwatchElapsed;
  int hours = elapsed / 3600000;
  int minutes = (elapsed % 3600000) / 60000;
  int seconds = (elapsed % 60000) / 1000;
  int ms = (elapsed % 1000) / 10;
  
  // Большой дисплей
  M5.Lcd.fillRoundRect(10, 30, 220, 50, 8, COLOR_BG2);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(COLOR_GREEN);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.printf("%02d:%02d:%02d", hours, minutes, seconds);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(170, 50);
  M5.Lcd.printf(".%02d", ms);
  
  // Статус
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(stopwatchRunning ? COLOR_GREEN : COLOR_GREY);
  M5.Lcd.setCursor(90, 90);
  M5.Lcd.print(stopwatchRunning ? "* RUNNING *" : "-- STOPPED --");
  
  // Подсказки
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("M5:Start/Stop Bok:Reset");
}

// === КРАСИВЫЙ ТАЙМЕР ===
void drawTimer() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_YELLOW);
  M5.Lcd.setTextColor(COLOR_BG);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(85, 4);
  M5.Lcd.print("TIMER");
  
  if (timerRunning) {
    unsigned long elapsed = millis() - myTimerStart;
    long remaining = (timerMinutes * 60L + timerSeconds) * 1000L - elapsed;
    
    if (remaining <= 0) {
      timerRunning = false;
      M5.Lcd.fillRoundRect(10, 30, 220, 60, 8, COLOR_RED);
      M5.Lcd.setTextSize(4);
      M5.Lcd.setTextColor(COLOR_WHITE);
      M5.Lcd.setCursor(50, 45);
      M5.Lcd.print("DONE!");
      beep(1000, 200); delay(200);
      beep(1000, 200); delay(200);
      beep(1000, 200);
    } else {
      int min = remaining / 60000;
      int sec = (remaining % 60000) / 1000;
      M5.Lcd.fillRoundRect(10, 30, 220, 60, 8, COLOR_BG2);
      M5.Lcd.setTextSize(5);
      M5.Lcd.setTextColor(COLOR_YELLOW);
      M5.Lcd.setCursor(50, 40);
      M5.Lcd.printf("%02d:%02d", min, sec);
    }
  } else {
    M5.Lcd.fillRoundRect(10, 30, 220, 60, 8, COLOR_BG2);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GREY);
    M5.Lcd.setCursor(90, 35);
    M5.Lcd.print("Set time:");
    M5.Lcd.setTextSize(5);
    M5.Lcd.setTextColor(COLOR_YELLOW);
    M5.Lcd.setCursor(50, 45);
    M5.Lcd.printf("%02d:%02d", timerMinutes, timerSeconds);
  }
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print(timerRunning ? "M5:Stop Bok:+1min" : "M5:Start Bok:+1min");
}

// === ФОНАРИК ===
void drawFlashlight() {
  if (flashlightOn) {
    M5.Lcd.fillScreen(COLOR_WHITE);
    M5.Axp.ScreenBreath(100);
  } else {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Axp.ScreenBreath(80);
    
    M5.Lcd.fillRect(0, 0, 240, 22, COLOR_YELLOW);
    M5.Lcd.setTextColor(COLOR_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(60, 4);
    M5.Lcd.print("FLASHLIGHT");
    
    M5.Lcd.fillRoundRect(40, 40, 160, 50, 10, COLOR_BG2);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(COLOR_YELLOW);
    M5.Lcd.setCursor(90, 50);
    M5.Lcd.print("OFF");
    
    M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(60, 120);
    M5.Lcd.print("M5: Toggle ON/OFF");
  }
}

// === БУДИЛЬНИК ===
void drawAlarm() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ORANGE);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(85, 4);
  M5.Lcd.print("ALARM");
  
  // Статус
  M5.Lcd.fillRoundRect(10, 30, 220, 25, 5, alarmEnabled ? COLOR_GREEN : COLOR_RED);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(70, 35);
  M5.Lcd.print(alarmEnabled ? "ENABLED" : "DISABLED");
  
  // Время
  M5.Lcd.fillRoundRect(10, 60, 220, 40, 8, COLOR_BG2);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(COLOR_YELLOW);
  M5.Lcd.setCursor(60, 67);
  M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute);
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("M5:On/Off Bok:Set Time");
}

// === КАЛЬКУЛЯТОР ===
void drawCalculator() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ACCENT2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(55, 4);
  M5.Lcd.print("CALCULATOR");
  
  // Дисплей
  M5.Lcd.fillRoundRect(10, 28, 220, 40, 5, COLOR_BG2);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(COLOR_GREEN);
  M5.Lcd.setCursor(15, 35);
  String disp = calcDisplay;
  if (disp.length() > 10) disp = disp.substring(disp.length()-10);
  M5.Lcd.print(disp);
  
  // Операция
  if (calcOp != ' ') {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_YELLOW);
    M5.Lcd.setCursor(15, 60);
    M5.Lcd.printf("Op: %s %c ?", calcFirst.c_str(), calcOp);
  }
  
  // Текущий символ
  const char* calcChars = "0123456789+-*/=C";
  M5.Lcd.fillRoundRect(10, 75, 220, 30, 5, COLOR_ACCENT);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(15, 80);
  M5.Lcd.print("Next: ");
  M5.Lcd.setTextSize(3);
  M5.Lcd.print(calcChars[calcIdx]);
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("M5:Next Bok:Type");
}

// === ЗМЕЙКА ===
void drawSnake() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  if (snakeGameOver) {
    M5.Lcd.fillRoundRect(20, 20, 200, 90, 10, COLOR_RED);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(45, 30);
    M5.Lcd.print("GAME OVER");
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(55, 65);
    M5.Lcd.printf("Score: %d", snakeScore);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(60, 95);
    M5.Lcd.print("M5:Restart");
    return;
  }
  
  // Игровое поле
  M5.Lcd.drawRect(0, 0, GRID_W*CELL, GRID_H*CELL, COLOR_GREY);
  
  // Яблоко
  M5.Lcd.fillRoundRect(apple.x*CELL+1, apple.y*CELL+1, CELL-2, CELL-2, 2, COLOR_RED);
  
  // Змейка
  for(int i=0; i<snakeLen; i++) {
    uint16_t col = (i == 0) ? COLOR_GREEN : 0x07E0;
    M5.Lcd.fillRoundRect(snake[i].x*CELL+1, snake[i].y*CELL+1, CELL-2, CELL-2, 2, col);
  }
  
  // Счёт и управление
  M5.Lcd.fillRect(0, GRID_H*CELL + 2, 240, 15, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, GRID_H*CELL + 5);
  M5.Lcd.printf("Score:%d Bok:Turn Hold:Bk", snakeScore);
}

// === ВЕБ НАСТРОЙКА ===
void drawWebSetup() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ACCENT);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(60, 4);
  M5.Lcd.print("WEB SETUP");
  
  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.fillRoundRect(10, 30, 220, 80, 8, COLOR_BG2);
    
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GREY);
    M5.Lcd.setCursor(20, 38);
    M5.Lcd.print("Open in browser:");
    
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(COLOR_YELLOW);
    M5.Lcd.setCursor(20, 55);
    M5.Lcd.print(WiFi.localIP().toString());
    
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GREEN);
    M5.Lcd.setCursor(20, 85);
    M5.Lcd.print("* Server running *");
  } else {
    M5.Lcd.fillRoundRect(10, 40, 220, 50, 8, COLOR_RED);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(50, 55);
    M5.Lcd.print("NO WiFi!");
  }
  
  M5.Lcd.fillRect(0, 125, 240, 10, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_DARKGREY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(70, 127);
  M5.Lcd.print("Hold M5+Bok: Back");
}

// === НАСТРОЙКИ ===
void drawSettings() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_GREY);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(70, 4);
  M5.Lcd.print("SETTINGS");
  
  M5.Lcd.fillRoundRect(10, 28, 220, 80, 5, COLOR_BG2);
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 33);
  M5.Lcd.print("CITY:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(70, 33);
  M5.Lcd.print(myCity);
  
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 48);
  M5.Lcd.print("WIFI:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(70, 48);
  String ssid = mySSID;
  if (ssid.length() > 15) ssid = ssid.substring(0, 15) + "...";
  M5.Lcd.print(ssid);
  
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 63);
  M5.Lcd.print("GMT:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(70, 63);
  M5.Lcd.printf("%+d hours", gmtOffset/3600);
  
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 78);
  M5.Lcd.print("ALARM:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(70, 78);
  M5.Lcd.printf("%02d:%02d %s", alarmHour, alarmMinute, alarmEnabled?"ON":"OFF");
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("M5:City Bok:WiFi Hold:Bk");
}

// === ВЫБОР WIFI ===
void drawSetupWifi() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ACCENT);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(50, 4);
  M5.Lcd.print("SELECT WIFI");
  
  if (scannedNetworkCount > 0) {
    M5.Lcd.fillRoundRect(10, 30, 220, 30, 5, COLOR_ACCENT2);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(15, 40);
    String ssid = WiFi.SSID(selectedNetworkIndex);
    if (ssid.length() > 25) ssid = ssid.substring(0, 25) + "...";
    M5.Lcd.printf("> %s", ssid.c_str());
    
    M5.Lcd.setTextColor(COLOR_GREY);
    M5.Lcd.setCursor(15, 70);
    if (scannedNetworkCount > 1) {
        int nextIdx = (selectedNetworkIndex + 1) % scannedNetworkCount;
        String nextSsid = WiFi.SSID(nextIdx);
        if (nextSsid.length() > 25) nextSsid = nextSsid.substring(0, 25) + "...";
        M5.Lcd.printf("  %s", nextSsid.c_str());
    }
  } else {
    M5.Lcd.setTextColor(COLOR_YELLOW);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(60, 50);
    M5.Lcd.print("Scanning...");
  }
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(30, 120);
  M5.Lcd.print("Bok: Next   M5: Select");
}

// === КЛАВИАТУРА ===
void drawKeyboard() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  // Заголовок
  M5.Lcd.fillRect(0, 0, 240, 18, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, 4);
  if (!numberMode) M5.Lcd.printf("SSID: %s", mySSID.c_str());
  else M5.Lcd.print("INPUT MODE");
  
  // Индикатор SHIFT
  M5.Lcd.setCursor(215, 4);
  if (shiftMode) {
    M5.Lcd.fillRoundRect(205, 2, 25, 14, 3, COLOR_ORANGE);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.print("A");
  } else {
    M5.Lcd.setTextColor(COLOR_GREY);
    M5.Lcd.print("a");
  }
  
  // Поле ввода
  M5.Lcd.fillRoundRect(5, 20, 230, 20, 4, COLOR_BG2);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(10, 23);
  String display = numberMode ? inputText : inputPass;
  if (display.length() > 12) M5.Lcd.print("..." + display.substring(display.length()-10) + "_");
  else M5.Lcd.print(display + "_");
  
  // Клавиатура
  M5.Lcd.setTextSize(1);
  const char* rows[] = {row1, row2, row3, row4, row5};
  int rowLens[] = {10, 9, 7, 10, 4};
  
  for(int r=0; r<5; r++) {
    int y = 45 + r*13;
    for(int i=0; i<rowLens[r]; i++) {
       int x = 8 + i*14;
       if (currentRow == r && currentCol == i) {
         M5.Lcd.fillRoundRect(x-2, y-2, 12, 12, 2, COLOR_RED);
         M5.Lcd.setTextColor(COLOR_WHITE);
       } else {
         M5.Lcd.setTextColor(COLOR_GREY);
       }
       char c = rows[r][i];
       if (shiftMode && c >= 'a' && c <= 'z') c = c - 32;
       M5.Lcd.setCursor(x, y);
       M5.Lcd.print(c);
    }
  }
  
  // Нижняя панель управления
  M5.Lcd.fillRect(0, 112, 240, 23, COLOR_BG2);
  
  int x = 10;
  // SHIFT
  if (currentRow==5 && currentCol==0) {
    M5.Lcd.fillRoundRect(x, 114, 50, 18, 3, COLOR_ORANGE);
    M5.Lcd.setTextColor(COLOR_WHITE);
  } else {
    M5.Lcd.setTextColor(COLOR_GREY);
  }
  M5.Lcd.setCursor(x+5, 117);
  M5.Lcd.print(numberMode ? "DONE" : "SHIFT");
  
  // DEL
  x = 90;
  if (currentRow==5 && currentCol==1) {
    M5.Lcd.fillRoundRect(x, 114, 40, 18, 3, COLOR_ORANGE);
    M5.Lcd.setTextColor(COLOR_WHITE);
  } else {
    M5.Lcd.setTextColor(COLOR_GREY);
  }
  M5.Lcd.setCursor(x+5, 117);
  M5.Lcd.print("DEL");
  
  // OK
  x = 170;
  if (currentRow==5 && currentCol==2) {
    M5.Lcd.fillRoundRect(x, 114, 50, 18, 3, COLOR_GREEN);
    M5.Lcd.setTextColor(COLOR_WHITE);
  } else {
    M5.Lcd.setTextColor(COLOR_GREY);
  }
  M5.Lcd.setCursor(x+15, 117);
  M5.Lcd.print("OK");
}

// === ПОДКЛЮЧЕНИЕ ===
void drawConnecting() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ACCENT);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(60, 4);
  M5.Lcd.print("CONNECTING");
  
  M5.Lcd.fillRoundRect(10, 30, 220, 60, 8, COLOR_BG2);
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_GREY);
  M5.Lcd.setCursor(20, 38);
  M5.Lcd.print("Network:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(80, 38);
  M5.Lcd.print(mySSID);
  
  M5.Lcd.setTextColor(COLOR_GREY);
  M5.Lcd.setCursor(20, 55);
  M5.Lcd.print("Password:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(80, 55);
  M5.Lcd.printf("%d chars", myPASS.length());
  
  M5.Lcd.setTextColor(COLOR_GREY);
  M5.Lcd.setCursor(20, 72);
  M5.Lcd.print("Status:");
  M5.Lcd.setTextColor(COLOR_YELLOW);
  M5.Lcd.setCursor(80, 72);
  M5.Lcd.print("Connecting...");
  
  M5.Lcd.fillRoundRect(10, 100, 220, 25, 5, COLOR_RED);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(30, 108);
  M5.Lcd.print("Hold M5+Bok: Cancel");
}

// === ОШИБКА ===
void drawError() {
  M5.Lcd.fillScreen(COLOR_BG);
  
  M5.Lcd.fillRect(0, 0, 240, 22, COLOR_RED);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(65, 4);
  M5.Lcd.print("WIFI ERROR");
  
  M5.Lcd.fillRoundRect(10, 28, 220, 60, 5, COLOR_BG2);
  
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 33);
  M5.Lcd.print("SSID:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(60, 33);
  String ssid = mySSID;
  if (ssid.length() > 20) ssid = ssid.substring(0, 20) + "...";
  M5.Lcd.print(ssid);
  
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.setCursor(15, 48);
  M5.Lcd.print("PASS:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(60, 48);
  if (showPassword) M5.Lcd.print(myPASS);
  else {
    String hidden = "";
    for(int i=0; i<myPASS.length(); i++) hidden += "*";
    M5.Lcd.print(hidden);
  }
  
  M5.Lcd.setTextColor(COLOR_RED);
  M5.Lcd.setCursor(15, 63);
  M5.Lcd.print("Error:");
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(60, 63);
  M5.Lcd.print(errorMessage);
  
  M5.Lcd.fillRect(0, 115, 240, 20, COLOR_BG2);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(5, 120);
  M5.Lcd.print("M5:Show Bok:Retry Hold:Bk");
}

// === БУДИЛЬНИК ЗВЕнит ===
void drawAlarmRinging() {
  M5.Lcd.fillScreen(COLOR_RED);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(40, 20);
  M5.Lcd.print("ALARM!");
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(50, 65);
  M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(50, 110);
  M5.Lcd.print("Press any button: Stop");
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
    M5.Lcd.setCursor(80, 72);
    M5.Lcd.setTextColor(COLOR_YELLOW);
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
  M5.Axp.ScreenBreath(80); // ЯРКОСТЬ 80%!
  beepInit();
  
  M5.update();
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    unsigned long holdStart = millis();
    while(M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
      M5.update();
      if (millis() - holdStart > 2000) {
        M5.Lcd.fillScreen(RED); 
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(50, 50); 
        M5.Lcd.print("RESETTING...");
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
          if (flashlightOn) { flashlightOn = false; M5.Axp.ScreenBreath(80); }
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
    M5.Axp.ScreenBreath(80);
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
      M5.Lcd.fillScreen(COLOR_BG);
      M5.Lcd.fillRect(0, 0, 240, 22, COLOR_ORANGE);
      M5.Lcd.setTextColor(COLOR_WHITE);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(60, 4);
      M5.Lcd.print("SET ALARM");
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(COLOR_WHITE);
      M5.Lcd.setCursor(20, 40);
      M5.Lcd.print("Bok: +1 hour | M5: +1 min");
      
      M5.Lcd.fillRoundRect(40, 60, 160, 40, 8, COLOR_BG2);
      M5.Lcd.setTextSize(4);
      M5.Lcd.setTextColor(COLOR_YELLOW);
      M5.Lcd.setCursor(70, 67);
      M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute);
      
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(50, 115);
      M5.Lcd.print("Hold M5+Bok: Save & Back");
      
      static int alarmSetStep = 0;
      alarmSetStep = 1;
      while(alarmSetStep) {
        M5.update();
        if (M5.BtnA.wasPressed()) { 
          alarmMinute = (alarmMinute + 1) % 60; 
          M5.Lcd.setCursor(70, 67); 
          M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute); 
        }
        if (M5.BtnB.wasPressed()) { 
          alarmHour = (alarmHour + 1) % 24; 
          M5.Lcd.setCursor(70, 67); 
          M5.Lcd.printf("%02d:%02d", alarmHour, alarmMinute); 
        }
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