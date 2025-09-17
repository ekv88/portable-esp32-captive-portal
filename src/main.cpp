#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <SPI.h>

#define PIN_CLK   18
#define PIN_MOSI  23
#define PIN_CS     5
#define PIN_DC    16
#define PIN_RST   17
#define ROTATION  U8G2_R0
U8G2_SH1107_SEEED_128X128_F_4W_SW_SPI u8g2(ROTATION, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST);

#define BTN_UP     4
#define BTN_DOWN  19
#define BTN_OK    21

constexpr uint8_t  FONT_H = 12;
constexpr uint8_t  ROW_H  = 16;
constexpr uint8_t  LIST_TOP = 28;
constexpr uint8_t  VISIBLE_ROWS = 7;
constexpr uint8_t  UI_MAX_SSID_LEN = 18;

constexpr uint32_t RESCAN_MS   = 10000;
constexpr uint32_t DEBOUNCE_MS = 140;
constexpr uint32_t HOLD_MS     = 500;
constexpr uint32_t REPEAT_MS   = 120;
constexpr uint32_t DNS_INTERVAL = 10;

enum Mode { MODE_LIST_MAIN, MODE_LIST_CUSTOM, MODE_AP, MODE_HELLO };
Mode mode = MODE_LIST_MAIN;

int  netCount = 0, selected = 0, scrollTop = 0;
bool scanning = false;
uint32_t lastScanDoneAt = 0;

int prevUp = HIGH, prevDown = HIGH, prevOk = HIGH;
uint32_t lastEdgeUp = 0, lastEdgeDown = 0, lastEdgeOk = 0;
uint32_t lastRepUp = 0, lastRepDown = 0;

constexpr uint8_t MAX_CLIENTS = 4;
constexpr uint8_t WIFI_CHANNEL = 6;

const IPAddress localIP(4,3,2,1);
const IPAddress gatewayIP(4,3,2,1);
const IPAddress subnetMask(255,255,255,0);
const String    localIPURL = "http://4.3.2.1";

DNSServer dnsServer;
AsyncWebServer server(80);

String pickedSSID;
String helloName;

const char index_html[] PROGMEM = R"=====(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Captive Portal</title>
<style>body{background:#efefef;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:20px}form{margin-top:16px}input{padding:8px;font-size:16px}button{padding:8px 12px;font-size:16px}</style>
</head><body><h1>Hello!</h1><form action="/hello" method="GET"><label>Name: <input name="name" maxlength="32" autofocus></label> <button type="submit">Submit</button></form></body></html>)=====";

const int RSSI_THRESHOLDS[4] = { -85, -75, -67, -60 };
const char* CUSTOM_SSIDS[] = { "ESP32 Wifi", "Free WiFi", "Guest AP", "My Portal", "Cafe Hotspot" };
constexpr int CUSTOM_COUNT = sizeof(CUSTOM_SSIDS)/sizeof(CUSTOM_SSIDS[0]);

String trimSSID(const String& s){
  return (s.length() <= UI_MAX_SSID_LEN) ? s : (s.substring(0, UI_MAX_SSID_LEN - 1) + "…");
}

void drawHeader(const char* t){
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.setCursor((128 - u8g2.getStrWidth(t))/2, 12);
  u8g2.print(t);
  u8g2.drawHLine(0, 16, 128);
}

void drawSignalBars(int x, int yBaseline, int rssi) {
  int bars = 0;
  for (int i = 0; i < 4; i++) if (rssi > RSSI_THRESHOLDS[i]) bars++;
  int yBottom = yBaseline - 1;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    int xx = x + i * 4;
    int yy = yBottom - h + 1;
    if (i < bars) u8g2.drawBox(xx, yy, 3, h);
    else          u8g2.drawFrame(xx, yy, 3, h);
  }
}

void startAsyncScan() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  netCount = 0;
  scanning = true;
}

inline bool scanReady() {
  return !scanning && WiFi.scanComplete() >= 0 && netCount > 0;
}

void updateScanState() {
  if(mode != MODE_LIST_MAIN) return;
  int s = WiFi.scanComplete();
  if(s >= 0){
    netCount = s;
    if(selected >= netCount+1) selected = max(0, netCount);
    if(scrollTop > selected) scrollTop = selected;
    lastScanDoneAt = millis();
    scanning = false;
  } else if(s == WIFI_SCAN_FAILED){
    scanning = false;
    lastScanDoneAt = millis();
  }
  if(!scanning && millis() - lastScanDoneAt >= RESCAN_MS){
    startAsyncScan();
  }
}

void moveSelection(int d, int items){
  if(items <= 0) return;
  selected = (selected + d + items) % items;
  if(selected < scrollTop) scrollTop = selected;
  if(selected >= scrollTop + VISIBLE_ROWS) scrollTop = selected - VISIBLE_ROWS + 1;
}

void setUpDNSServer(DNSServer &dns, const IPAddress &ip) {
  dns.setTTL(3600);
  dns.start(53, "*", ip);
}

void startSoftAPCore(const char *ssid, const char *pwd, const IPAddress &ip, const IPAddress &gw) {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(ip, gw, subnetMask);
  WiFi.softAP(ssid, pwd, WIFI_CHANNEL, 0, MAX_CLIENTS);
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.ampdu_rx_enable = false;
  esp_wifi_init(&cfg);
  esp_wifi_start();
  vTaskDelay(100 / portTICK_PERIOD_MS);
}

void setUpWebserver(AsyncWebServer &srv, const IPAddress &ip) {
  const char* redirectPaths[] = {
    "/redirect",
    "/hotspot-detect.html",
    "/canonical.html",
    "/ncsi.txt",
    "/generate_204",
    "/gen_204",
    "/google/generate_204",
    "/connectivitycheck/generate_204"
  };

  for (auto path : redirectPaths) {
    srv.on(path, [=](AsyncWebServerRequest *r){ r->redirect(localIPURL); });
  }

  srv.on("/connecttest.txt", [](AsyncWebServerRequest *r){ r->redirect("http://logout.net"); });
  srv.on("/wpad.dat",        [](AsyncWebServerRequest *r){ r->send(404); });
  srv.on("/success.txt",     [](AsyncWebServerRequest *r){ r->send(200); });
  srv.on("/favicon.ico",     [](AsyncWebServerRequest *r){ r->send(404); });

  srv.on("/", HTTP_ANY, [](AsyncWebServerRequest *r){
    auto *resp = r->beginResponse(200, "text/html", index_html);
    resp->addHeader("Cache-Control", "public,max-age=31536000");
    r->send(resp);
  });

  srv.on("/hello", HTTP_ANY, [](AsyncWebServerRequest *r){
    String n = r->hasParam("name") ? r->getParam("name")->value() : "";
    n.trim(); if(n.length() > 32) n = n.substring(0,32);
    helloName = n;
    mode = MODE_HELLO;
    r->send(200, "text/html",
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><h3>Thanks!</h3>");
  });

  srv.onNotFound([](AsyncWebServerRequest *r){ r->redirect(localIPURL); });
}

void startPortalWithName(const String& name){
  String ap = name.length() ? name : String("ESP32_AP");
  if(ap.length()>32) ap = ap.substring(0,32);
  startSoftAPCore(ap.c_str(), nullptr, localIP, gatewayIP);
  setUpDNSServer(dnsServer, localIP);
  setUpWebserver(server, localIP);
  server.begin();
}

void stopPortal(){
  dnsServer.stop();
  server.end();
  WiFi.softAPdisconnect(true);
}

void handleButtons(){
  uint32_t now = millis();
  int curUp   = digitalRead(BTN_UP);
  int curDown = digitalRead(BTN_DOWN);
  int curOk   = digitalRead(BTN_OK);

  bool edgeUp   = (prevUp==HIGH   && curUp==LOW)   && (now - lastEdgeUp   >= DEBOUNCE_MS);
  bool edgeDown = (prevDown==HIGH && curDown==LOW) && (now - lastEdgeDown >= DEBOUNCE_MS);
  bool edgeOk   = (prevOk==HIGH   && curOk==LOW)   && (now - lastEdgeOk   >= DEBOUNCE_MS);

  if(mode == MODE_LIST_MAIN){
    int total = netCount + 1; // row 0 = "Make Custom WiFi"
    if(edgeUp){   lastEdgeUp=now;   lastRepUp=now;   moveSelection(-1, total); }
    if(edgeDown){ lastEdgeDown=now; lastRepDown=now; moveSelection(+1, total); }
    bool heldUp   = (curUp==LOW)   && (now - lastEdgeUp   >= HOLD_MS) && (now - lastRepUp   >= REPEAT_MS);
    bool heldDown = (curDown==LOW) && (now - lastEdgeDown >= HOLD_MS) && (now - lastRepDown >= REPEAT_MS);
    if(heldUp)   { lastRepUp=now;   moveSelection(-1, total); }
    if(heldDown) { lastRepDown=now; moveSelection(+1, total); }
    if(edgeOk){
      if(selected == 0){
        mode = MODE_LIST_CUSTOM;
        selected = 0; scrollTop = 0;
      }else{
        pickedSSID = WiFi.SSID(selected - 1);
        WiFi.scanDelete();
        WiFi.disconnect(true, true);
        helloName = "";
        startPortalWithName(pickedSSID);
        mode = MODE_AP;
      }
    }
  }
  else if(mode == MODE_LIST_CUSTOM){
    int total = CUSTOM_COUNT + 1; // row 0 = "Back to WiFi list"
    if(edgeUp){   lastEdgeUp=now;   lastRepUp=now;   moveSelection(-1, total); }
    if(edgeDown){ lastEdgeDown=now; lastRepDown=now; moveSelection(+1, total); }
    bool heldUp   = (curUp==LOW)   && (now - lastEdgeUp   >= HOLD_MS) && (now - lastRepUp   >= REPEAT_MS);
    bool heldDown = (curDown==LOW) && (now - lastEdgeDown >= HOLD_MS) && (now - lastRepDown >= REPEAT_MS);
    if(heldUp)   { lastRepUp=now;   moveSelection(-1, total); }
    if(heldDown) { lastRepDown=now; moveSelection(+1, total); }

    if(edgeOk){
      if(selected == 0){
        mode = MODE_LIST_MAIN;
        selected = 0; scrollTop = 0;
        startAsyncScan();
      }else{
        pickedSSID = CUSTOM_SSIDS[selected - 1];
        helloName = "";
        startPortalWithName(pickedSSID);
        mode = MODE_AP;
      }
    }
    // NOTE: Down no longer acts as "back" here.
  }
  else if(mode == MODE_AP || mode == MODE_HELLO){
    if(edgeDown){ stopPortal(); helloName = ""; pickedSSID = ""; mode = MODE_LIST_MAIN; selected = 0; scrollTop = 0; startAsyncScan(); }
    if(edgeOk && mode == MODE_AP){ stopPortal(); helloName = ""; pickedSSID = ""; mode = MODE_LIST_MAIN; selected = 0; scrollTop = 0; startAsyncScan(); }
  }

  prevUp = curUp; prevDown = curDown; prevOk = curOk;
}

void drawListMain(){
  u8g2.clearBuffer();
  drawHeader("Wi-Fi Networks");

  int total = netCount + 1;
  if(total <= 0){
    const bool running = (WiFi.scanComplete() == WIFI_SCAN_RUNNING);
    const char* m = running ? "Scanning..." : "No networks";
    uint8_t w = u8g2.getStrWidth(m);
    u8g2.setCursor((128 - w)/2, 72);
    u8g2.print(m);
    u8g2.sendBuffer();
    return;
  }

  int end = min(scrollTop + VISIBLE_ROWS, total);
  for(int idx = scrollTop; idx < end; idx++){
    int row = idx - scrollTop;
    int yBase = LIST_TOP + row*ROW_H;
    int bandY = yBase - FONT_H;
    bool sel = (idx == selected);

    if(sel){ u8g2.drawBox(0, bandY, 128, FONT_H + 4); u8g2.setDrawColor(0); }
    else    { u8g2.setDrawColor(1); }

    if(idx == 0){
      const char* label = "Make Custom WiFi";
      u8g2.setCursor(4, yBase);
      u8g2.print(label);
    }else{
      String ssid = trimSSID(WiFi.SSID(idx - 1));
      u8g2.setCursor(4, yBase);
      u8g2.print(ssid);

      if (scanReady()) {
        bool locked = (WiFi.encryptionType(idx - 1) != WIFI_AUTH_OPEN);
        int  rssi   = WiFi.RSSI(idx - 1);
        if(locked){
          int lockY = yBase - 8;
          u8g2.drawHLine(128-35, lockY, 4);
          u8g2.drawVLine(128-35, lockY+1, 2);
          u8g2.drawVLine(128-32, lockY+1, 2);
          u8g2.drawFrame(128-37, lockY+4, 8, 6);
        }
        drawSignalBars(128-21, yBase, rssi);
      }
    }
    u8g2.setDrawColor(1);
  }

  if(scanning) u8g2.drawHLine(0, 126, (millis()/50)%129);

  u8g2.sendBuffer();
}

void drawListCustom(){
  u8g2.clearBuffer();
  drawHeader("Custom WiFi");

  int total = CUSTOM_COUNT + 1; // row 0 = "Back to WiFi list"
  if(total <= 0){
    const char* m = "No entries";
    uint8_t w = u8g2.getStrWidth(m);
    u8g2.setCursor((128 - w)/2, 72);
    u8g2.print(m);
    u8g2.sendBuffer();
    return;
  }

  int end = min(scrollTop + VISIBLE_ROWS, total);
  for(int i = scrollTop; i < end; i++){
    int row = i - scrollTop;
    int yBase = LIST_TOP + row*ROW_H;
    int bandY = yBase - FONT_H;
    bool sel = (i == selected);

    if(sel){ u8g2.drawBox(0, bandY, 128, FONT_H + 4); u8g2.setDrawColor(0); }
    else    { u8g2.setDrawColor(1); }

    if(i == 0){
      const char* back = "Back to WiFi list";
      u8g2.setCursor(4, yBase);
      u8g2.print(back);
    } else {
      u8g2.setCursor(4, yBase);
      u8g2.print(CUSTOM_SSIDS[i - 1]);
    }

    u8g2.setDrawColor(1);
  }

  u8g2.sendBuffer();
}

void drawApScreen(){
  u8g2.clearBuffer();
  drawHeader("Captive Portal");
  String line1 = "AP SSID:";
  String name  = trimSSID(pickedSSID.length() ? pickedSSID : String("ESP32_AP"));
  uint8_t w1 = u8g2.getStrWidth(line1.c_str());
  u8g2.setCursor((128 - w1)/2, 46);
  u8g2.print(line1);
  uint8_t w2 = u8g2.getStrWidth(name.c_str());
  u8g2.setCursor((128 - w2)/2, 62);
  u8g2.print(name);
  const char* hint = "Join & browse";
  uint8_t w3 = u8g2.getStrWidth(hint);
  u8g2.setCursor((128 - w3)/2, 88);
  u8g2.print(hint);
  const char* back = "DOWN/OK: back";
  uint8_t w4 = u8g2.getStrWidth(back);
  u8g2.setCursor((128 - w4)/2, 110);
  u8g2.print(back);
  u8g2.sendBuffer();
}

void drawHelloScreen(){
  u8g2.clearBuffer();
  drawHeader("Message");
  String msgTop = "Hello,";
  String msgName = helloName.length() ? helloName : String("(no name)");
  uint8_t w1 = u8g2.getStrWidth(msgTop.c_str());
  u8g2.setCursor((128 - w1)/2, 56);
  u8g2.print(msgTop);
  uint8_t w2 = u8g2.getStrWidth(msgName.c_str());
  u8g2.setCursor((128 - w2)/2, 76);
  u8g2.print(msgName);
  const char* back = "DOWN/OK: back";
  uint8_t w3 = u8g2.getStrWidth(back);
  u8g2.setCursor((128 - w3)/2, 110);
  u8g2.print(back);
  u8g2.sendBuffer();
}

void setup(){
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK,   INPUT_PULLUP);
  u8g2.begin();
  u8g2.setContrast(220);
  startAsyncScan();
}

void loop(){
  static uint32_t lastDNS = 0;
  if(mode == MODE_AP || mode == MODE_HELLO){
    if(millis() - lastDNS >= DNS_INTERVAL){
      dnsServer.processNextRequest();
      lastDNS = millis();
    }
  } else {
    updateScanState();
  }

  handleButtons();

  if(mode == MODE_LIST_MAIN)        drawListMain();
  else if(mode == MODE_LIST_CUSTOM) drawListCustom();
  else if(mode == MODE_AP)          drawApScreen();
  else                              drawHelloScreen();

  delay(10);
}
