#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FastLED.h>
#include <ESP_I2S.h>
#include <esp_task_wdt.h>

// Wi-Fi configuration
const char* ssid = "LED Nhay Theo Nhac";
const char* password = "Lednhay12345";

// Initialize objects
WebServer server(80);
DNSServer dnsServer;

// LED declaration
#define NUM_LEDS_PER_STRIP 60
#define DATA_PIN_LEF 13
#define DATA_PIN_RIG 14
CRGB leds1[NUM_LEDS_PER_STRIP]; // Color data storage array for LEDs
CRGB leds2[NUM_LEDS_PER_STRIP]; // Color data storage array for LEDs

// INMP441 I2S MEMS microphone (digital). Adjust these to match your wiring.
// INMP441 -> ESP32: SCK->I2S_SCK_PIN, WS->I2S_WS_PIN, SD->I2S_SD_PIN,
//                   VDD->3V3, GND->GND, L/R->GND (selects the LEFT slot).
#define I2S_SCK_PIN 26 // bit clock (BCLK)
#define I2S_WS_PIN  25 // word select (LRCL)
#define I2S_SD_PIN  33 // serial data out from mic (DOUT) -> ESP32 input
#define I2S_SAMPLE_RATE 16000
// INMP441 sends 24-bit samples left-justified in a 32-bit frame. Shifting right
// by 20 keeps the top ~12 bits, so the rectified envelope lands in roughly the
// same 0..2048 range as the old 12-bit ADC mic and the downstream bass/treble
// thresholds stay usable. Lower the shift for more gain; tune on hardware.
#define MIC_SAMPLE_SHIFT 20

I2SClass i2sMic;
bool i2sReady = false;

enum VisualMode : uint8_t {
  MODE_SPLIT = 0, // Left strip = bass, right strip = treble
  MODE_SYNC = 1   // Both strips use the same level
};

enum ColorEffect : uint8_t {
  EFFECT_SOLID = 0,
  EFFECT_RAINBOW = 1,
  EFFECT_OCEAN = 2,
  EFFECT_FIRE = 3
};

struct AppState {
  VisualMode mode = MODE_SPLIT;
  ColorEffect effect = EFFECT_RAINBOW;
  bool animateEffect = true;
  uint8_t brightnessPct = 20;  // default 20/100 for safer startup power
  uint8_t sensitivityPct = 50; // default mic sensitivity
};

AppState state;
unsigned long lastFrameMs = 0;
unsigned long lastLoopMs = 0;
unsigned long bootStartMs = 0;
const unsigned long LOOP_WARN_MS = 200; // warn if a loop iteration is slower than this
const uint32_t WDT_TIMEOUT_S = 5;       // hardware watchdog reboots if loop hangs this long
const uint8_t LED_SUPPLY_VOLTS = 5;
const uint16_t LED_MAX_MILLIAMPS = 350; // stricter cap for unstable USB power
const uint8_t MAX_BRIGHTNESS_PCT = 40;  // hard ceiling to avoid current spikes
const unsigned long BOOT_STABILIZE_MS = 4000;
const unsigned long STRIP2_ENABLE_DELAY_MS = 8000;
uint8_t hueBase = 0;
float bassEnergy = 0.0f;
float trebleEnergy = 0.0f;

const char* WEB_UI = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>LED Nhay Theo Nhac</title>
  <style>
    :root { --bg:#0c0f15; --panel:#171b24; --line:#2b3342; --text:#edf2ff; --muted:#9ea8c3; --accent:#28c8ff; }
    * { box-sizing: border-box; }
    body { margin:0; font-family: "Trebuchet MS", "Segoe UI", sans-serif; background: radial-gradient(1200px 500px at 20% -20%, #234, transparent), var(--bg); color: var(--text); }
    .wrap { max-width: 760px; margin: 0 auto; padding: 20px 14px 40px; }
    .card { background: linear-gradient(145deg,#1b2130,#141923); border: 1px solid var(--line); border-radius: 14px; padding: 14px; margin-bottom: 12px; }
    h1 { font-size: 1.2rem; margin: 0 0 8px; }
    .sub { color: var(--muted); font-size: .92rem; margin-bottom: 6px; }
    label { display:block; margin-top: 10px; margin-bottom: 4px; font-size: .92rem; color: #c8d2eb; }
    select, input[type='range'], button { width: 100%; }
    select, button { background: #0f1420; color: var(--text); border:1px solid var(--line); border-radius: 10px; padding: 10px; }
    .row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .btn { background: linear-gradient(120deg,#0fa3d6,#27d0d6); color:#02111a; font-weight: 700; border: none; }
    .status { font-size: .88rem; color: var(--muted); margin-top: 8px; }
    .pill { display:inline-block; padding: 4px 8px; border-radius: 99px; background:#0d1d28; border:1px solid #274457; margin-right: 6px; margin-top: 6px; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>ESP32 LED Nháy Theo Nhạc</h1>
      <div class="sub">AP: LED Nháy Theo Nhạc | IP: 192.168.4.1</div>
      <div class="pill" id="modeBadge"></div>
      <div class="pill" id="effectBadge"></div>
    </div>

    <div class="card">
      <label>Chế độ hiển thị</label>
      <select id="mode">
        <option value="split">Bass/Treble (Trái: bass, Phải: treble)</option>
        <option value="sync">Đồng bộ (2 Dây giống nhau)</option>
      </select>

      <label>Hiệu ứng màu</label>
      <select id="effect">
        <option value="rainbow">Rainbow</option>
        <option value="solid">Solid</option>
        <option value="ocean">Ocean</option>
        <option value="fire">Fire</option>
      </select>

      <label>Thay đổi hiệu ứng tự động</label>
      <select id="animate">
        <option value="1">Có</option>
        <option value="0">Không</option>
      </select>

      <div class="row">
        <div>
          <label>Độ sáng LED: <span id="brightnessValue">20</span>/100</label>
          <input id="brightness" type="range" min="1" max="40" value="20">
        </div>
        <div>
          <label>Độ nhạy mic: <span id="sensitivityValue">50</span>/100</label>
          <input id="sensitivity" type="range" min="1" max="100" value="50">
        </div>
      </div>

      <button class="btn" id="applyBtn">Áp dụng thay đổi</button>
      <div class="status" id="status">Sẵn sàng.</div>
    </div>
  </div>

  <script>
    const el = (id) => document.getElementById(id);
    const mode = el('mode');
    const effect = el('effect');
    const animate = el('animate');
    const brightness = el('brightness');
    const sensitivity = el('sensitivity');
    const brightnessValue = el('brightnessValue');
    const sensitivityValue = el('sensitivityValue');
    const status = el('status');
    const modeBadge = el('modeBadge');
    const effectBadge = el('effectBadge');

    brightness.oninput = () => brightnessValue.textContent = brightness.value;
    sensitivity.oninput = () => sensitivityValue.textContent = sensitivity.value;

    function drawBadges() {
      modeBadge.textContent = 'Mode: ' + mode.value;
      effectBadge.textContent = 'Effect: ' + effect.value + (animate.value === '1' ? ' (animate)' : ' (static)');
    }

    async function loadState() {
      const res = await fetch('/api/state');
      const s = await res.json();
      mode.value = s.mode;
      effect.value = s.effect;
      animate.value = s.animate ? '1' : '0';
      brightness.value = s.brightness;
      sensitivity.value = s.sensitivity;
      brightnessValue.textContent = s.brightness;
      sensitivityValue.textContent = s.sensitivity;
      drawBadges();
    }

    async function applyState() {
      status.textContent = 'Đang cập nhật...';
      const q = new URLSearchParams({
        mode: mode.value,
        effect: effect.value,
        animate: animate.value,
        brightness: brightness.value,
        sensitivity: sensitivity.value,
      });
      const res = await fetch('/api/set?' + q.toString());
      const txt = await res.text();
      status.textContent = txt;
      drawBadges();
    }

    el('applyBtn').onclick = applyState;
    mode.onchange = drawBadges;
    effect.onchange = drawBadges;
    animate.onchange = drawBadges;
    loadState().catch(() => status.textContent = 'Không tải được trạng thái.');
  </script>
</body>
</html>
)rawliteral";

int readMicEnvelope() {
  if (!i2sReady) return 0;

  // Average the rectified amplitude of a short burst of I2S samples. The INMP441
  // is already centered on zero, so no DC offset removal is needed (unlike the
  // old ADC mic that was centered on 2048).
  int64_t sum = 0;
  int collected = 0;
  const int samples = 32;
  for (int i = 0; i < samples; i++) {
    const int32_t raw = i2sMic.read(); // full 32-bit sample, or -1 if no data ready
    if (raw == -1) continue;
    const int32_t scaled = raw >> MIC_SAMPLE_SHIFT;
    sum += llabs((long long)scaled);
    collected++;
  }
  if (collected == 0) return 0;
  return (int)(sum / (int64_t)collected);
}

float applySensitivity(float v) {
  const float gain = 0.5f + (state.sensitivityPct / 100.0f) * 3.5f;
  float out = v * gain;
  if (out < 0.0f) out = 0.0f;
  if (out > 1.0f) out = 1.0f;
  return out;
}

uint8_t pctToBrightness255(uint8_t pct) {
  if (pct > MAX_BRIGHTNESS_PCT) pct = MAX_BRIGHTNESS_PCT;
  return (uint8_t)((pct * 255) / 100);
}

void applyBrightnessFromState() {
  FastLED.setBrightness(pctToBrightness255(state.brightnessPct));
}

CRGB colorForPixel(uint8_t index, uint8_t level255) {
  uint8_t h = hueBase;
  switch (state.effect) {
    case EFFECT_SOLID:
      h = 170;
      break;
    case EFFECT_RAINBOW:
      h = hueBase + index * 4;
      break;
    case EFFECT_OCEAN:
      h = 140 + ((hueBase / 3) + index * 2) % 60;
      break;
    case EFFECT_FIRE:
      h = ((hueBase / 4) + index) % 28;
      break;
  }
  return CHSV(h, 255, level255);
}

void renderStrip(CRGB* strip, float level) {
  const int onCount = (int)(level * NUM_LEDS_PER_STRIP + 0.5f);
  for (int i = 0; i < NUM_LEDS_PER_STRIP; i++) {
      if (i < onCount) {
      // Keep per-pixel value lower to avoid current spikes that can brownout ESP32.
        uint8_t ledLevel = (uint8_t)(64 + level * 110);
      strip[i] = colorForPixel((uint8_t)i, ledLevel);
    } else {
      strip[i] = CRGB::Black;
    }
  }
}

String modeToString(VisualMode mode) {
  return mode == MODE_SYNC ? "sync" : "split";
}

String effectToString(ColorEffect effect) {
  switch (effect) {
    case EFFECT_SOLID:
      return "solid";
    case EFFECT_OCEAN:
      return "ocean";
    case EFFECT_FIRE:
      return "fire";
    default:
      return "rainbow";
  }
}

void handleRoot() {
  server.send(200, "text/html", WEB_UI);
}

void handleCaptiveProbe() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleStateApi() {
  String body = "{";
  body += "\"mode\":\"" + modeToString(state.mode) + "\",";
  body += "\"effect\":\"" + effectToString(state.effect) + "\",";
  body += "\"animate\":" + String(state.animateEffect ? "true" : "false") + ",";
  body += "\"brightness\":" + String(state.brightnessPct) + ",";
  body += "\"sensitivity\":" + String(state.sensitivityPct);
  body += "}";
  server.send(200, "application/json", body);
}

void handleBrightnessApi() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Thiếu tham số value (0..255)");
    return;
  }
  int v = server.arg("value").toInt();
  if (v < 0) v = 0;
  const int maxBrightness255 = (MAX_BRIGHTNESS_PCT * 255) / 100;
  if (v > maxBrightness255) v = maxBrightness255;
  // Keep AppState in sync (it stores brightness as a 0..100 percentage).
  state.brightnessPct = (uint8_t)((v * 100 + 127) / 255);
  if (state.brightnessPct < 1) state.brightnessPct = 1;
  if (state.brightnessPct > MAX_BRIGHTNESS_PCT) state.brightnessPct = MAX_BRIGHTNESS_PCT;
  applyBrightnessFromState();
  server.send(200, "text/plain", "Độ sáng: " + String(v) + "/255");
}

void handleStatusApi() {
  String body = "{";
  body += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  body += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  body += "\"apClients\":" + String(WiFi.softAPgetStationNum());
  body += "}";
  server.send(200, "application/json", body);
}

void handleSetApi() {
  if (server.hasArg("mode")) {
    const String v = server.arg("mode");
    state.mode = (v == "sync") ? MODE_SYNC : MODE_SPLIT;
  }
  if (server.hasArg("effect")) {
    const String v = server.arg("effect");
    if (v == "solid") state.effect = EFFECT_SOLID;
    else if (v == "ocean") state.effect = EFFECT_OCEAN;
    else if (v == "fire") state.effect = EFFECT_FIRE;
    else state.effect = EFFECT_RAINBOW;
  }
  if (server.hasArg("animate")) {
    state.animateEffect = server.arg("animate") != "0";
  }
  if (server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b < 1) b = 1;
    if (b > MAX_BRIGHTNESS_PCT) b = MAX_BRIGHTNESS_PCT;
    state.brightnessPct = (uint8_t)b;
  }
  if (server.hasArg("sensitivity")) {
    int s = server.arg("sensitivity").toInt();
    if (s < 1) s = 1;
    if (s > 100) s = 100;
    state.sensitivityPct = (uint8_t)s;
  }
  applyBrightnessFromState();
  server.send(200, "text/plain", "Da cap nhat cai dat");
}

void updateLedsMusicReactive() {
  const unsigned long upMs = millis() - bootStartMs;
  if (upMs < BOOT_STABILIZE_MS) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastFrameMs < 16) {
    return;
  }
  lastFrameMs = now;

  const float env = (float)readMicEnvelope();
  bassEnergy = bassEnergy * 0.92f + env * 0.08f;
  trebleEnergy = trebleEnergy * 0.70f + env * 0.30f;
  float bass = bassEnergy;
  float treble = trebleEnergy - bassEnergy;
  if (treble < 0.0f) treble = 0.0f;

  const float bassNorm = applySensitivity((bass - 18.0f) / 900.0f);
  const float trebleNorm = applySensitivity((treble - 8.0f) / 450.0f);
  const float syncNorm = (bassNorm + trebleNorm) * 0.5f;

  if (state.mode == MODE_SPLIT) {
    renderStrip(leds1, bassNorm);
    if (upMs >= STRIP2_ENABLE_DELAY_MS) {
      renderStrip(leds2, trebleNorm);
    } else {
      fill_solid(leds2, NUM_LEDS_PER_STRIP, CRGB::Black);
    }
  } else {
    renderStrip(leds1, syncNorm);
    if (upMs >= STRIP2_ENABLE_DELAY_MS) {
      renderStrip(leds2, syncNorm);
    } else {
      fill_solid(leds2, NUM_LEDS_PER_STRIP, CRGB::Black);
    }
  }

  if (state.animateEffect) {
    hueBase++;
  }
  FastLED.show();
}

void setupTaskWatchdog() {
  // Subscribe the Arduino loop task to the hardware Task Watchdog Timer (TWDT).
  // If loop() ever stops calling esp_task_wdt_reset() within WDT_TIMEOUT_S, the
  // chip panics and reboots. The init API changed in Arduino-ESP32 core 3.x.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  // The TWDT may already be initialized by the core; reconfigure if so.
  if (esp_task_wdt_init(&wdtConfig) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&wdtConfig);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL); // NULL = current (loop) task
}

void setup() {
  Serial.begin(115200);
  bootStartMs = millis();

  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // INMP441 I2S microphone: standard mode, mono, left slot (L/R tied to GND).
  // Read as 32-bit because the INMP441 left-justifies 24 valid bits per frame.
  i2sMic.setPins(I2S_SCK_PIN, I2S_WS_PIN, -1 /*no DOUT*/, I2S_SD_PIN);
  i2sReady = i2sMic.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                          I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  if (!i2sReady) {
    Serial.println("Khoi tao I2S mic that bai!");
  } else {
    Serial.println("I2S mic san sang");
  }

  // Setting up an Access Point
  WiFi.softAP(ssid, password, 1, 0, 4);
  
  // Set up DNS to capture all accesses to 192.168.4.1
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/api/state", HTTP_GET, handleStateApi);
  server.on("/api/set", HTTP_GET, handleSetApi);
  server.on("/status", HTTP_GET, handleStatusApi);
  server.on("/brightness", HTTP_GET, handleBrightnessApi);

  // Common OS captive portal probe URLs.
  server.on("/generate_204", HTTP_GET, handleCaptiveProbe);      // Android
  server.on("/gen_204", HTTP_GET, handleCaptiveProbe);           // Android alternate
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe); // iOS/macOS
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);   // Windows
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);          // Windows legacy
  server.on("/fwlink", HTTP_GET, handleCaptiveProbe);
  server.onNotFound(handleCaptiveProbe);

  server.begin();
  FastLED.addLeds<WS2812B, DATA_PIN_LEF, GRB>(leds1, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, DATA_PIN_RIG, GRB>(leds2, NUM_LEDS_PER_STRIP);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_SUPPLY_VOLTS, LED_MAX_MILLIAMPS);
  FastLED.clear(true);

  // Soft-start brightness to reduce inrush current at boot.
  FastLED.setBrightness(0);
  FastLED.show();
  for (uint8_t b = 0; b <= pctToBrightness255(state.brightnessPct); b += 4) {
    FastLED.setBrightness(b);
    FastLED.show();
    delay(8);
  }
  applyBrightnessFromState();

  setupTaskWatchdog();

  Serial.println("AP ready");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  // Soft watchdog: a blocking call here (long delay, async lwIP stall) starves
  // the Wi-Fi stack. Measure how long each iteration took and warn over Serial.
  const unsigned long loopStart = millis();
  if (lastLoopMs != 0) {
    const unsigned long elapsed = loopStart - lastLoopMs;
    if (elapsed > LOOP_WARN_MS) {
      Serial.print("[WATCHDOG] Loop cham: ");
      Serial.print(elapsed);
      Serial.println(" ms");
    }
  }
  lastLoopMs = loopStart;

  esp_task_wdt_reset(); // feed the hardware watchdog each iteration

  dnsServer.processNextRequest();
  server.handleClient();
  updateLedsMusicReactive();
}