/*
 * ESP32 Clock with 4x WS2812 8x8 LED Matrix
 * - NTP time sync via WiFi
 * - Auto color changing with multiple modes
 * - Phone control via WiFi Web Server
 *
 * Wokwi: Bat [net] enable=true trong wokwi.toml
 * Thuc te: Ket noi WiFi qua giao dien web tren dien thoai
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

/*=== Config ===*/
#define AP_SSID    "ESP32-Clock"
#define AP_PASS    "12345678"
#define LED_PIN    13
#define NUM_LEDS   256
Adafruit_NeoPixel matrix(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define NTP_SERVER1    "pool.ntp.org"
#define NTP_SERVER2    "time.google.com"
#define GMT_OFFSET_SEC 25200
#define DST_OFFSET_SEC 0

WebServer server(80);
Preferences prefs;

/*=== Time ===*/
int hours = 0, minutes = 0, seconds = 0;
unsigned long lastTick = 0;
bool ntpSynced = false;
unsigned long lastNtpAttempt = 0;

/*=== Display ===*/
int brightness = 50;
bool use24h = true;
bool showColon = true;
bool colonOn = true;
unsigned long lastColonToggle = 0;
bool fontBold = false;

/*=== Color ===*/
enum ColorMode { MODE_SINGLE=0, MODE_RAINBOW, MODE_RANDOM, MODE_BREATHING, MODE_GRADIENT, MODE_WAVE, MODE_COUNT };
int colorMode = MODE_RAINBOW;
uint8_t fixedR = 0, fixedG = 255, fixedB = 0;
float hueOffset = 0;
unsigned long lastHueUpdate = 0;
bool autoColorChange = true;
unsigned long lastAutoChange = 0;
uint32_t digitColors[4];

/*=== WiFi ===*/
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;
unsigned long lastWifiAttempt = 0;

/*=== Font 8x8 for digits ===*/
static const uint8_t FONT[10][8] = {
  { // 0
    0b00111100,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b00111100
  },
  { // 1
    0b00001000,
    0b00011000,
    0b00101000,
    0b01001000,
    0b00001000,
    0b00001000,
    0b00001000,
    0b01111110
  },
  { // 2
    0b00111100,
    0b01000010,
    0b01000010,
    0b00000100,
    0b00001000,
    0b00010000,
    0b00100000,
    0b01111110
  },
  { // 3
    0b00111100,
    0b01000010,
    0b00000010,
    0b00011100,
    0b00000010,
    0b00000010,
    0b01000010,
    0b00111100
  },
  { // 4
    0b00000100,
    0b00001100,
    0b00010100,
    0b00100100,
    0b01000100,
    0b01111110,
    0b00000100,
    0b00000100
  },
  { // 5
    0b01111110,
    0b01000000,
    0b01000000,
    0b01111100,
    0b00000010,
    0b00000010,
    0b01000010,
    0b00111100
  },
  { // 6
    0b00111100,
    0b01000010,
    0b01000000,
    0b01000000,
    0b01111100,
    0b01000010,
    0b01000010,
    0b00111100
  },
  { // 7
    0b01111110,
    0b01000010,
    0b00000010,
    0b00000100,
    0b00001000,
    0b00010000,
    0b00100000,
    0b00100000
  },
  { // 8
    0b00111100,
    0b01000010,
    0b01000010,
    0b00111100,
    0b01000010,
    0b01000010,
    0b01000010,
    0b00111100
  },
  { // 9
    0b00111100,
    0b01000010,
    0b01000010,
    0b00111110,
    0b00000010,
    0b00000010,
    0b01000010,
    0b00111100
  }
};

/*=== Font Bold 8x8 ===*/
static const uint8_t FONT_BOLD[10][8] = {
  { // 0
    0b00111100,
    0b01100110,
    0b01100110,
    0b01100110,
    0b01100110,
    0b01100110,
    0b01100110,
    0b00111100
  },
  { // 1
    0b00001000,
    0b00011000,
    0b00111000,
    0b00011000,
    0b00011000,
    0b00011000,
    0b00011000,
    0b01111110
  },
  { // 2
    0b00111100,
    0b01100110,
    0b01100110,
    0b00000100,
    0b00001000,
    0b00010000,
    0b00100110,
    0b01111110
  },
  { // 3
    0b00111100,
    0b01100110,
    0b00000110,
    0b00011100,
    0b00000110,
    0b00000110,
    0b01100110,
    0b00111100
  },
  { // 4
    0b00001100,
    0b00011100,
    0b00101100,
    0b01001100,
    0b01111110,
    0b00001100,
    0b00001100,
    0b00001100
  },
  { // 5
    0b01111110,
    0b01100000,
    0b01100000,
    0b01111100,
    0b00000110,
    0b00000110,
    0b01100110,
    0b00111100
  },
  { // 6
    0b00111100,
    0b01100110,
    0b01100000,
    0b01111100,
    0b01100110,
    0b01100110,
    0b01100110,
    0b00111100
  },
  { // 7
    0b01111110,
    0b01100110,
    0b00000110,
    0b00000100,
    0b00001000,
    0b00010000,
    0b00110000,
    0b00110000
  },
  { // 8
    0b00111100,
    0b01100110,
    0b01100110,
    0b00111100,
    0b01100110,
    0b01100110,
    0b01100110,
    0b00111100
  },
  { // 9
    0b00111100,
    0b01100110,
    0b01100110,
    0b00111110,
    0b00000110,
    0b00000110,
    0b01100110,
    0b00111100
  }
};

/*=== Pixel index ===*/
inline int px(int x, int y, int panel) {
  return panel * 64 + y * 8 + x;
}

/*=== HSV to RGB ===*/
uint32_t hsv(float h, float s, float v) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r, g, b;
  if      (h < 60)  { r=c; g=x; b=0; }
  else if (h < 120) { r=x; g=c; b=0; }
  else if (h < 180) { r=0; g=c; b=x; }
  else if (h < 240) { r=0; g=x; b=c; }
  else if (h < 300) { r=x; g=0; b=c; }
  else              { r=c; g=0; b=x; }
  return matrix.Color((uint8_t)((r+m)*255), (uint8_t)((g+m)*255), (uint8_t)((b+m)*255));
}

/*=== Get color per digit (all panels same color) ===*/
uint32_t getColor(int i) {
  (void)i;
  switch (colorMode) {
    case MODE_SINGLE:    return matrix.Color(fixedR, fixedG, fixedB);
    case MODE_RAINBOW:   return hsv(fmodf(hueOffset, 360.0f), 1, 1);
    case MODE_RANDOM:    return digitColors[0];
    case MODE_BREATHING: {
      float b = (sinf(hueOffset * 3.14159f / 180.0f) + 1.0f) / 2.0f;
      b = 0.2f + b * 0.8f;
      return matrix.Color((uint8_t)(fixedR*b), (uint8_t)(fixedG*b), (uint8_t)(fixedB*b));
    }
    case MODE_GRADIENT: {
      float t = fmodf(hueOffset / 360.0f, 1.0f);
      return matrix.Color((uint8_t)(255*(1-t)), (uint8_t)(128*t+127*(1-t)), (uint8_t)(255*t));
    }
    case MODE_WAVE:      return hsv(fmodf(hueOffset*2, 360), 1, 1);
    default:             return matrix.Color(fixedR, fixedG, fixedB);
  }
}

/*=== Draw digit ===*/
void drawDigit(int d, int panel, uint32_t color) {
  if (d < 0 || d > 9) return;
  const uint8_t (*font)[8] = fontBold ? FONT_BOLD : FONT;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      if (font[d][y] & (1 << (7 - x)))
        matrix.setPixelColor(px(x, y, panel), color);
}

/*=== Draw colon ===*/
void drawColon(uint32_t color) {
  if (!showColon || !colonOn) return;
  matrix.setPixelColor(px(7, 2, 1), color);
  matrix.setPixelColor(px(7, 5, 1), color);
  matrix.setPixelColor(px(0, 2, 2), color);
  matrix.setPixelColor(px(0, 5, 2), color);
}

/*=== Random colors ===*/
void randomColors() {
  for (int i = 0; i < 4; i++)
    digitColors[i] = matrix.Color(random(80,256), random(80,256), random(80,256));
}

/*=== NTP ===*/
void tryNtp() {
  struct tm ti;
  if (getLocalTime(&ti, 500) && ti.tm_year + 1900 >= 2024) {
    hours   = ti.tm_hour;
    minutes = ti.tm_min;
    seconds = ti.tm_sec;
    ntpSynced = true;
    lastTick = millis();  // Reset tick to prevent catch-up
    Serial.printf("[NTP] Synced: %02d:%02d:%02d\n", hours, minutes, seconds);
  } else {
    lastTick = millis();  // Reset even on fail to prevent fast forward
    Serial.println("[NTP] Failed");
  }
}

/*=== WiFi ===*/
void wifiStart() {
  if (wifiSSID.length() == 0) return;
  Serial.printf("[WiFi] Connecting '%s'\n", wifiSSID.c_str());
  WiFi.disconnect(false);
  delay(50);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
  lastWifiAttempt = millis();
}

void wifiCheck() {
  bool conn = (WiFi.status() == WL_CONNECTED);
  if (conn && !wifiConnected) {
    wifiConnected = true;
    Serial.printf("[WiFi] OK IP:%s\n", WiFi.localIP().toString().c_str());
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    lastTick = millis();  // Reset tick after WiFi connect
  } else if (!conn && wifiConnected) {
    wifiConnected = false;
    ntpSynced = false;
    Serial.println("[WiFi] Disconnected");
  }
  if (!conn && wifiSSID.length() > 0 && millis() - lastWifiAttempt > 30000) {
    wifiStart();
  }
}

/*=== Web Page ===*/
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Clock</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#0a0a2e;color:#fff;padding:16px}
.c{max-width:400px;margin:auto}
h1{text-align:center;margin:12px 0;background:linear-gradient(90deg,#0ff,#f0e);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.card{background:rgba(255,255,255,.08);border-radius:14px;padding:14px;margin:10px 0;border:1px solid rgba(255,255,255,.1)}
.card h2{font-size:15px;color:#0ff;margin-bottom:10px}
.time{text-align:center;font-size:44px;font-weight:bold;letter-spacing:3px;text-shadow:0 0 15px rgba(0,240,255,.5)}
.bg{display:flex;flex-wrap:wrap;gap:8px}
.bg button{flex:1;min-width:45%;padding:12px 10px;border:2px solid transparent;border-radius:12px;font-size:13px;font-weight:700;cursor:pointer;color:#fff;position:relative;overflow:hidden;transition:transform .2s,box-shadow .3s;text-shadow:0 1px 3px rgba(0,0,0,.5)}
.bg button:active{transform:scale(.95)}
.bg button.act{box-shadow:0 0 0 2px #fff,0 0 12px rgba(255,255,255,.4);transform:scale(1.03)}
.b0{background:linear-gradient(135deg,#ff0000,#ff8800,#ffff00,#00cc00,#0066ff,#8800ff);background-size:300% 300%;animation:rb 3s ease infinite}
@keyframes rb{0%,100%{background-position:0% 50%}50%{background-position:100% 50%}}
.b1{background:linear-gradient(135deg,#0ff,#06c);border-color:rgba(0,255,255,.3);box-shadow:inset 0 0 20px rgba(0,255,255,.15)}
.b2{background:linear-gradient(135deg,#f0e,#60f,#0ff);background-size:200% 200%;animation:rd 2s ease infinite}
@keyframes rd{0%,100%{background-position:0% 50%}50%{background-position:100% 50%}}
.b3{background:linear-gradient(135deg,#0f8,#064);animation:br 2s ease-in-out infinite}
@keyframes br{0%,100%{opacity:1}50%{opacity:.5}}
.b4{background:linear-gradient(90deg,#ff4444,#44ff88,#4488ff);background-size:100% 100%}
.b5{background:linear-gradient(90deg,#f48,#84f,#48f,#4f8,#f84);background-size:400% 100%;animation:wv 3s linear infinite}
@keyframes wv{0%{background-position:0% 0}100%{background-position:400% 0}}
input[type=range]{width:100%;margin:8px 0}
input[type=color]{width:100%;height:40px;border:none;border-radius:8px}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0}
.sw{position:relative;width:44px;height:24px}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:#333;border-radius:12px;transition:.3s;cursor:pointer}
.sw span:before{content:"";position:absolute;width:18px;height:18px;left:3px;top:3px;background:#888;border-radius:50%;transition:.3s}
.sw input:checked+span{background:#0ff}
.sw input:checked+span:before{transform:translateX(20px);background:#fff}
.wf input{width:100%;padding:8px;margin:4px 0;border-radius:8px;border:1px solid #333;background:#111;color:#fff}
.wf button{width:100%;padding:10px;margin-top:8px;background:#0ff;color:#000;border:none;border-radius:10px;font-weight:bold;cursor:pointer}
.st{text-align:center;font-size:11px;color:#888;margin-top:6px}
</style></head><body><div class="c">
<h1>ESP32 LED Clock</h1>
<div class="card"><h2>Thời Gian</h2><div class="time" id="t">--:--:--</div><div class="st" id="ns">...</div></div>
<div class="card"><h2>Hiệu Ứng Màu</h2><div class="bg" id="mg">
<button class="b0" onclick="M(1)">🌈 Cầu Vồng</button>
<button class="b1" onclick="M(0)">🎨 Một Màu</button>
<button class="b2" onclick="M(2)">🎲 Ngẫu Nhiên</button>
<button class="b3" onclick="M(3)">💨 Hiệu Ứng Thở</button>
<button class="b4" onclick="M(4)">🌅 Gradient</button>
<button class="b5" onclick="M(5)">🌊 Sóng Màu</button>
</div></div>
<div class="card"><h2>Chọn Màu</h2><input type="color" id="cp" value="#00ff00" onchange="C(this.value)"></div>
<div class="card"><h2>Độ Sáng: <span id="bv">50</span></h2><input type="range" min="5" max="100" value="50" id="bs" oninput="B(this.value)"></div>
<div class="card"><h2>Cài Đặt</h2>
<div class="row"><span>Tự động đổi hiệu ứng</span><label class="sw"><input type="checkbox" id="ac" checked onchange="S('auto',this.checked?1:0)"><span></span></label></div>
<div class="row"><span>Dấu 2 chấm</span><label class="sw"><input type="checkbox" id="sc" checked onchange="S('colon',this.checked?1:0)"><span></span></label></div>
<div class="row"><span>24h</span><label class="sw"><input type="checkbox" id="f24" checked onchange="S('format',this.checked?1:0)"><span></span></label></div>
<div class="row"><span>Font Đậm</span><label class="sw"><input type="checkbox" id="fb" onchange="S('font',this.checked?1:0)"><span></span></label></div>
</div>
<div class="card"><h2>WiFi</h2><div class="wf">
<input id="ss" placeholder="Tên Wifi"><input id="pw" type="password" placeholder="Mật Khẩu">
<button onclick="W()">Kết Nối</button></div><div class="st" id="ws"></div></div>
</div><script>
function F(u,c){fetch(u).then(r=>r.json()).then(c).catch(()=>{})}
function M(m){F('/api/mode?m='+m,d=>{});var bs=document.getElementById('mg').children;for(var i=0;i<bs.length;i++)bs[i].classList.remove('act');var order=[1,0,2,3,4,5];for(var i=0;i<order.length;i++)if(order[i]==m){bs[i].classList.add('act');break}}
function C(v){var r=parseInt(v.substr(1,2),16),g=parseInt(v.substr(3,2),16),b=parseInt(v.substr(5,2),16);F('/api/color?r='+r+'&g='+g+'&b='+b,d=>{})}
function B(v){document.getElementById('bv').textContent=v;F('/api/brightness?v='+v,d=>{})}
function S(k,v){F('/api/'+k+'?v='+v,d=>{})}
function W(){var s=document.getElementById('ss').value,p=document.getElementById('pw').value;if(!s)return;F('/api/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p),d=>{document.getElementById('ws').textContent=d.msg||'OK'})}
function U(){F('/api/status',d=>{
document.getElementById('t').textContent=d.time||'--:--:--';
document.getElementById('ns').textContent=d.ntp?'NTP OK':'Chưa đồng bộ thời gian';
document.getElementById('bs').value=d.br;document.getElementById('bv').textContent=d.br;
document.getElementById('ac').checked=d.ac;document.getElementById('sc').checked=d.cl;
document.getElementById('f24').checked=d.f24;document.getElementById('fb').checked=d.fb;
document.getElementById('ws').textContent=d.wifi?'IP: '+d.ip:'Chưa kết nối wifi';
var bs=document.getElementById('mg').children,order=[1,0,2,3,4,5];for(var i=0;i<bs.length;i++){bs[i].classList.remove('act');if(order[i]==d.mode)bs[i].classList.add('act')}
})}
setInterval(U,2000);U();
</script></body></html>
)rawliteral";

/*=== API Handlers ===*/
void apiStatus() {
  int dh = use24h ? hours : (hours%12==0?12:hours%12);
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", dh, minutes, seconds);
  String j = "{\"time\":\"" + String(buf) + "\",\"ntp\":" + (ntpSynced?"true":"false")
    + ",\"br\":" + String(brightness) + ",\"ac\":" + (autoColorChange?"true":"false")
    + ",\"cl\":" + (showColon?"true":"false") + ",\"f24\":" + (use24h?"true":"false")
    + ",\"fb\":" + (fontBold?"true":"false")
    + ",\"mode\":" + String(colorMode)
    + ",\"wifi\":" + (wifiConnected?"true":"false")
    + ",\"ip\":\"" + (wifiConnected?WiFi.localIP().toString():"") + "\"}";
  server.send(200, "application/json", j);
}

void apiMode() {
  if (server.hasArg("m")) {
    int m = server.arg("m").toInt();
    if (m >= 0 && m < MODE_COUNT) { colorMode = m; if (m==MODE_RANDOM) randomColors(); }
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void apiColor() {
  if (server.hasArg("r")) fixedR = constrain(server.arg("r").toInt(), 0, 255);
  if (server.hasArg("g")) fixedG = constrain(server.arg("g").toInt(), 0, 255);
  if (server.hasArg("b")) fixedB = constrain(server.arg("b").toInt(), 0, 255);
  server.send(200, "application/json", "{\"ok\":1}");
}

void apiBrightness() {
  if (server.hasArg("v")) { brightness = constrain(server.arg("v").toInt(), 5, 255); matrix.setBrightness(brightness); }
  server.send(200, "application/json", "{\"ok\":1}");
}

void apiAuto()   { if(server.hasArg("v")) autoColorChange=(server.arg("v")=="1"); server.send(200,"application/json","{\"ok\":1}"); }
void apiColon()  { if(server.hasArg("v")) showColon=(server.arg("v")=="1"); server.send(200,"application/json","{\"ok\":1}"); }
void apiFormat() { if(server.hasArg("v")) use24h=(server.arg("v")=="1"); server.send(200,"application/json","{\"ok\":1}"); }
void apiFont()   { if(server.hasArg("v")) fontBold=(server.arg("v")=="1"); server.send(200,"application/json","{\"ok\":1}"); }

void apiWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() > 0 && ssid.length() <= 32) {
    prefs.begin("clk", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    wifiSSID = ssid;
    wifiPass = pass;
    server.send(200, "application/json", "{\"msg\":\"Đang Kết Nối...\"}");
    wifiStart();
  } else {
    server.send(200, "application/json", "{\"msg\":\"SSID Lỗi!\"}");
  }
}

/*=== SETUP ===*/
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 LED Clock ===");

  matrix.begin();
  matrix.setBrightness(brightness);
  matrix.show();
  randomColors();

  // Load WiFi
  prefs.begin("clk", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();

  // Start AP always
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[AP] %s @ %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Connect WiFi
  if (wifiSSID.length() > 0) {
    wifiStart();
  } else {
    // Wokwi: try virtual network
    wifiSSID = "Wokwi-GUEST";
    wifiPass = "";
    wifiStart();
  }

  // Web server routes
  server.on("/", []() { server.send(200, "text/html", HTML); });
  server.on("/api/status", apiStatus);
  server.on("/api/mode", apiMode);
  server.on("/api/color", apiColor);
  server.on("/api/brightness", apiBrightness);
  server.on("/api/auto", apiAuto);
  server.on("/api/colon", apiColon);
  server.on("/api/format", apiFormat);
  server.on("/api/font", apiFont);
  server.on("/api/wifi", apiWifi);
  server.begin();

  lastTick = millis();
  Serial.println("[OK] Ready! Open http://192.168.4.1");
}

/*=== LOOP ===*/
void loop() {
  unsigned long now = millis();

  server.handleClient();
  wifiCheck();

  // --- Time tick every 1 second (max 1 tick per loop to prevent fast-forward) ---
  if (now - lastTick >= 1000) {
    lastTick = now;  // Use 'now' not '+= 1000' to prevent catching up
    seconds++;
    if (seconds >= 60) { seconds = 0; minutes++; }
    if (minutes >= 60) { minutes = 0; hours++; }
    if (hours >= 24)   { hours = 0; }
  }

  // --- NTP sync ---
  if (wifiConnected) {
    unsigned long interval = ntpSynced ? 60000 : 5000;
    if (now - lastNtpAttempt >= interval) {
      lastNtpAttempt = now;
      tryNtp();
    }
  }

  // --- Colon blink ---
  if (now - lastColonToggle >= 500) {
    colonOn = !colonOn;
    lastColonToggle = now;
  }

  // --- Color animation ---
  if (now - lastHueUpdate >= 50) {
    hueOffset += 1.0f;
    if (hueOffset >= 360.0f) hueOffset -= 360.0f;
    lastHueUpdate = now;
  }

  // --- Auto mode change ---
  if (autoColorChange && now - lastAutoChange >= 60000) {
    lastAutoChange = now;
    colorMode = (colorMode + 1) % MODE_COUNT;
    if (colorMode == MODE_RANDOM) randomColors();
  }

  // --- Display ---
  matrix.clear();
  int dh = hours;
  if (!use24h) { dh = hours % 12; if (dh == 0) dh = 12; }

  drawDigit(dh / 10,      0, getColor(0));
  drawDigit(dh % 10,      1, getColor(1));
  drawDigit(minutes / 10, 2, getColor(2));
  drawDigit(minutes % 10, 3, getColor(3));
  drawColon(getColor(1));

  matrix.show();
  delay(20);
}