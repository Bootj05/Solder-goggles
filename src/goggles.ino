/**
 * Firmware for LED goggles
 * Provides WiFi control and OTA updates
 */
#include "secrets.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <vector>
#include <ctype.h>

namespace cfg {
constexpr uint8_t LED_PIN = 2;
constexpr uint8_t NUM_LEDS = 13;
constexpr uint8_t BTN_PREV = 0;
constexpr uint8_t BTN_NEXT = 35;
const char *SSID = WIFI_SSID;
const char *PASSWORD = WIFI_PASSWORD;
} // namespace cfg

/**
 * Types of LED animations.
 *
 * - `STATIC`     Solid color chosen per preset
 * - `RAINBOW`    Continuously cycling rainbow
 * - `POLICE_NL`  Blue and white pattern similar to Dutch police lights
 * - `POLICE_USA` Red, white and blue pattern similar to US police lights
 * - `STROBE`     Fast white flash on and off
 * - `LAVALAMP`   Slowly moving color gradient
 */
enum class PresetType {
  STATIC,
  RAINBOW,
  POLICE_NL,
  POLICE_USA,
  STROBE,
  LAVALAMP
};
struct Preset {
  String name;
  PresetType type;
  CRGB color; // Only for STATIC
};

struct PresetData {
  const char *name;
  PresetType type;
  CRGB color;
};

const PresetData defaultPresets[] PROGMEM = {
    {"White", PresetType::STATIC, CRGB::White},
    {"Rainbow", PresetType::RAINBOW, CRGB::Black},
    {"Police NL", PresetType::POLICE_NL, CRGB::Black},
    {"Police USA", PresetType::POLICE_USA, CRGB::Black},
    {"Strobe", PresetType::STROBE, CRGB::Black},
    {"Lavalamp", PresetType::LAVALAMP, CRGB::Black}};

std::vector<Preset> presets;
const size_t DEFAULT_PRESET_COUNT = sizeof(defaultPresets) / sizeof(defaultPresets[0]);

CRGB leds[cfg::NUM_LEDS];
int currentPreset = 0;
uint8_t rainbowHue = 0;
unsigned long lastAnim = 0;

WebServer server(80);
WebSocketsServer ws(81);

/**
 * Connect to WiFi using credentials from secrets.h
 */

void connectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg::SSID, cfg::PASSWORD);
  unsigned long start = millis();
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" failed to connect.");
  }
}

void loadDefaultPresets() {
  presets.clear();
  for (size_t i = 0; i < DEFAULT_PRESET_COUNT; ++i) {
    PresetData data;
    memcpy_P(&data, &defaultPresets[i], sizeof(PresetData));
    presets.push_back({String(data.name), data.type, data.color});
  }
}

void loadCustomPresets() {
  File f = SPIFFS.open("/presets.txt", "r");
  if (!f)
    return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length())
      continue;
    int first = line.indexOf(',');
    int second = line.indexOf(',', first + 1);
    if (first == -1 || second == -1)
      continue;
    String name = line.substring(0, first);
    int type = line.substring(first + 1, second).toInt();
    String colStr = line.substring(second + 1);
    uint32_t val = strtoul(colStr.c_str(), nullptr, 16);
    presets.push_back({name, static_cast<PresetType>(type),
                       CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF,
                            val & 0xFF)});
  }
  f.close();
}

void saveCustomPresets() {
  File f = SPIFFS.open("/presets.txt", "w");
  if (!f)
    return;
  for (size_t i = DEFAULT_PRESET_COUNT; i < presets.size(); ++i) {
    char buf[64];
    sprintf(buf, "%s,%d,%02x%02x%02x\n", presets[i].name.c_str(),
            static_cast<int>(presets[i].type), presets[i].color.r,
            presets[i].color.g, presets[i].color.b);
    f.print(buf);
  }
  f.close();
}

/**
 * Update LEDs based on active preset
 */
void applyPreset() {
  switch (presets[currentPreset].type) {
  case PresetType::STATIC:
    fill_solid(leds, cfg::NUM_LEDS, presets[currentPreset].color);
    break;
  case PresetType::RAINBOW:
    fill_rainbow(leds, cfg::NUM_LEDS, rainbowHue++, 7);
    break;
  case PresetType::POLICE_NL:
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = (i % 4 < 2) ? CRGB::Blue : CRGB::White;
    }
    break;
  case PresetType::POLICE_USA:
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = (i % 6 < 2)   ? CRGB::Red
                : (i % 6 < 4) ? CRGB::White
                              : CRGB::Blue;
    }
    break;
  case PresetType::STROBE: {
    static bool on = false;
    fill_solid(leds, cfg::NUM_LEDS, on ? CRGB::White : CRGB::Black);
    on = !on;
  } break;
  case PresetType::LAVALAMP: {
    static uint8_t lavaPos = 0;
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = CHSV((lavaPos + i * 10) % 255, 200, 255);
    }
    ++lavaPos;
  } break;
  }
  FastLED.show();
}

/**
 * Cycle to the next preset
 */
void nextPreset() {
  currentPreset = (currentPreset + 1) % presets.size();
  applyPreset();
}

/**
 * Cycle to the previous preset
 */
void previousPreset() {
  currentPreset = (currentPreset - 1 + presets.size()) % presets.size();
  applyPreset();
}

/**
 * Serve the main HTML interface listing presets
 */
const char HTML_HEADER[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <title>Solder Goggles</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@4.6.2/dist/css/bootstrap.min.css">
</head>
<body class="container mt-4">
  <h1 class="mb-3">LED Presets</h1>
  <ul class="list-group">
)html";

const char HTML_FOOTER[] PROGMEM = R"html(
  </ul>
  <form method='POST' action='/add' class='mt-4'>
    <div class='form-group'>
      <label for='name'>Name</label>
      <input class='form-control' id='name' name='name'>
    </div>
    <div class='form-group'>
      <label for='color'>Color (hex like #ff00ff)</label>
      <input class='form-control' id='color' name='color'>
    </div>
    <button class='btn btn-primary'>Add</button>
  </form>
</body>
</html>
)html";

void handleRoot() {
  String html = FPSTR(HTML_HEADER);
  for (size_t i = 0; i < presets.size(); ++i) {
    html += "<li class='list-group-item'>" + presets[i].name;
    if (i == currentPreset)
      html += " <span class='badge badge-success'>active</span>";
    html += "</li>";
  }
  html += FPSTR(HTML_FOOTER);
  server.send(200, "text/html", html);
}

/**
 * Add a new static preset from form input
 */
void handleAdd() {
  if (!server.hasArg("name") || !server.hasArg("color")) {
    server.send(400, "text/html",
                "<html><body><p>Missing name or color.</p><a href='/' >Back</a></body></html>");
    return;
  }

  String name = server.arg("name");
  String colorStr = server.arg("color");

  if (colorStr.length() != 7 || colorStr[0] != '#') {
    server.send(400, "text/html",
                "<html><body><p>Color must be in format #RRGGBB.</p><a href='/' >Back</a></body></html>");
    return;
  }

  colorStr = colorStr.substring(1);
  for (size_t i = 0; i < colorStr.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(colorStr[i]))) {
      server.send(400, "text/html",
                  "<html><body><p>Color contains invalid hex characters.</p><a href='/' >Back</a></body></html>");
      return;
    }
  }

  long val = strtol(colorStr.c_str(), nullptr, 16);
  presets.push_back({name, PresetType::STATIC,
                     CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF)});
  currentPreset = presets.size() - 1;
  saveCustomPresets();
  applyPreset();

  server.sendHeader("Location", "/");
  server.send(303);
}

/**
 * Handle incoming WebSocket messages
 */
void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  if (type != WStype_TEXT)
    return;
  String msg = String((char *)payload);
  if (msg == "next")
    nextPreset();
  else if (msg == "prev")
    previousPreset();
  else if (msg.startsWith("set:")) {
    String idxStr = msg.substring(4);
    bool digitsOnly = idxStr.length() > 0;
    for (size_t i = 0; i < idxStr.length() && digitsOnly; ++i) {
      digitsOnly = isDigit(idxStr[i]);
    }
    // Validate the index to avoid falling back to preset 0 on bad input
    if (digitsOnly) {
      int idx = idxStr.toInt();
      if (idx >= 0 && idx < presets.size()) {
        currentPreset = idx;
        applyPreset();
      }
    }
  }
}

/**
 * Initialize hardware and network services
 */
void setup() {
  Serial.begin(115200);
  pinMode(cfg::BTN_PREV, INPUT_PULLUP);
  // Buttons use internal pull-ups and are thus active-low
  pinMode(cfg::BTN_NEXT, INPUT_PULLUP);
  FastLED.addLeds<WS2812, cfg::LED_PIN, GRB>(leds, cfg::NUM_LEDS);

  SPIFFS.begin(true);
  loadDefaultPresets();
  loadCustomPresets();

  connectWiFi();
  if (MDNS.begin("JohannesBril")) {
    Serial.println("mDNS active on JohannesBril.local");
  }

  server.on("/", handleRoot);
  server.on("/add", HTTP_POST, handleAdd);
  server.begin();

  ws.begin();
  ws.onEvent(wsEvent);

  ArduinoOTA.setHostname("JohannesBril");
  ArduinoOTA.begin();

  applyPreset();
}

/**
 * Main execution loop
 */
void loop() {
  static bool lastBtnPrev = HIGH;
  static bool lastBtnNext = HIGH;
  static unsigned long lastDebounce = 0;
  const unsigned long debounceDelay = 50;

  bool btnPrev = digitalRead(cfg::BTN_PREV) == LOW;
  bool btnNext = digitalRead(cfg::BTN_NEXT) == LOW;

  if (millis() - lastDebounce > debounceDelay) {
    if (btnPrev && !lastBtnPrev)
      previousPreset();
    if (btnNext && !lastBtnNext)
      nextPreset();
    lastDebounce = millis();
  }

  lastBtnPrev = btnPrev;
  lastBtnNext = btnNext;

  server.handleClient();
  ws.loop();
  ArduinoOTA.handle();

  if (millis() - lastAnim > 50) {
    applyPreset();
    lastAnim = millis();
  }
}
