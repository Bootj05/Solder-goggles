/**
 * Firmware for LED goggles
 * Provides WiFi control and OTA updates
 */
#include "secrets.h"
#include "utils.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <vector>
#include <ctype.h>

namespace cfg {
constexpr uint8_t LED_PIN = 2;
constexpr uint8_t NUM_LEDS = 13;
constexpr uint8_t BTN_PREV = 0;
constexpr uint8_t BTN_NEXT = 35;
const char *SSID = WIFI_SSID;
const char *PASSWORD = WIFI_PASSWORD;
#ifdef USE_AUTH
const char *TOKEN = AUTH_TOKEN;
#endif
} // namespace cfg

/**
 * Types of LED animations.
 *
 * - `STATIC` &mdash; Solid color chosen per preset.
 * - `RAINBOW` &mdash; Continuously cycling rainbow.
 * - `POLICE_NL` &mdash; Blue and white pattern similar to Dutch police lights.
 * - `POLICE_USA` &mdash; Red, white and blue pattern similar to US police lights.
 * - `STROBE` &mdash; Fast white flash on and off.
 * - `LAVALAMP` &mdash; Slowly moving color gradient.
 * - `FIRE`       Randomized warm flicker
 * - `CANDLE`     Single warm flickering glow
 * - `PARTY`      Random bright colors
 */
enum class PresetType {
  STATIC,
  RAINBOW,
  POLICE_NL,
  POLICE_USA,
  STROBE,
  LAVALAMP,
  FIRE,
  CANDLE,
  PARTY
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

constexpr PresetData defaultPresets[] PROGMEM = {
    {"White", PresetType::STATIC, CRGB::White},
    {"Rainbow", PresetType::RAINBOW, CRGB::Black},
    {"Police NL", PresetType::POLICE_NL, CRGB::Black},
    {"Police USA", PresetType::POLICE_USA, CRGB::Black},
    {"Strobe", PresetType::STROBE, CRGB::Black},
    {"Lavalamp", PresetType::LAVALAMP, CRGB::Black},
    {"Fire", PresetType::FIRE, CRGB::Black},
    {"Candle", PresetType::CANDLE, CRGB::Black},
    {"Party", PresetType::PARTY, CRGB::Black},
};
std::vector<Preset> presets;
const size_t DEFAULT_PRESET_COUNT = sizeof(defaultPresets) / sizeof(defaultPresets[0]);

constexpr char DEFAULT_HOST[] = "JohannesBril";


CRGB leds[cfg::NUM_LEDS];
int currentPreset = 0;
uint8_t rainbowHue = 0;

unsigned long lastAnim = 0;
uint8_t brightness = 255;
unsigned long animInterval = 50;

Preferences prefs;
String storedSSID;
String storedPassword;
String storedHostname;

WebServer server(80);
WebSocketsServer ws(81);

void loadCredentials() {
  prefs.begin("wifi", true);
  storedSSID = prefs.getString("ssid", "");
  storedPassword = prefs.getString("pass", "");
  storedHostname = prefs.getString("host", DEFAULT_HOST);
  prefs.end();
}

void saveCredentials(const String &ssid, const String &password,
                     const String &host) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.putString("host", host);
  prefs.end();
  storedSSID = ssid;
  storedPassword = password;
  storedHostname = host;
}

/**
 * Connect to WiFi using stored credentials if available
 */

void connectWiFi() {
  loadCredentials();
  const char *ssid = storedSSID.length() ? storedSSID.c_str() : cfg::SSID;
  const char *pass = storedPassword.length() ? storedPassword.c_str() : cfg::PASSWORD;
  const char *host = storedHostname.length() ? storedHostname.c_str() : DEFAULT_HOST;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected!");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(host)) {
      Serial.print("mDNS active on ");
      Serial.print(host);
      Serial.println(".local");
    }
    ArduinoOTA.setHostname(host);
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
  for (size_t i = DEFAULT_PRESET_COUNT; i + 1 < presets.size(); ++i) {
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
    EVERY_N_MILLISECONDS(50) { ++rainbowHue; }
    fill_rainbow(leds, cfg::NUM_LEDS, rainbowHue, 7);
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
    EVERY_N_MILLISECONDS(50) { on = !on; }
    fill_solid(leds, cfg::NUM_LEDS, on ? CRGB::White : CRGB::Black);
  } break;

  case PresetType::LAVALAMP: {
    static uint8_t lavaPos = 0;
    EVERY_N_MILLISECONDS(50) { ++lavaPos; }
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = CHSV((lavaPos + i * 10) % 255, 200, 255);
    }
  } break;
  case PresetType::FIRE: {
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = CHSV(random8(0, 40), 255, random8(120, 255));
    }
  } break;
  case PresetType::CANDLE: {
    uint8_t bri = random8(150, 255);
    CRGB color = CHSV(random8(25, 45), 200, bri);
    fill_solid(leds, cfg::NUM_LEDS, color);
  } break;
  case PresetType::PARTY: {
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = CHSV(random8(), 255, 255);
    }
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
 * The page template is stored entirely in PROGMEM with a %PRESETS% placeholder
 * that gets replaced with the generated preset list.
 */
const char HTML_PAGE[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <title>Solder Goggles</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
  <style>
    body {
      background: linear-gradient(135deg, #222, #000);
      color: #f8f9fa;
    }
    .list-group-item {
      background-color: rgba(255,255,255,0.05);
      border-color: rgba(255,255,255,0.15);
    }
  </style>
</head>
<body class="container py-4">
  <h1 class="mb-4 text-center">Solder Goggles</h1>
  <div class='mb-3 text-center'>
    <a class='btn btn-secondary me-2' href='/prev'>Prev</a>
    <a class='btn btn-secondary' href='/next'>Next</a>
  </div>
  <ul class="list-group">
  %PRESETS%
  </ul>
  <form method='POST' action='/add' class='row row-cols-lg-auto g-3 align-items-center mt-4'>
    <div class='col-12'>
      <input class='form-control' id='name' name='name' placeholder='Name'>
    </div>
    <div class='col-12'>
      <input class='form-control' id='color' name='color' type='color'>
    </div>
    <div class='col-12'>
      <button class='btn btn-primary'>Add</button>
    </div>
  </form>
  <a class='btn btn-link mt-3' href='/wifi'>WiFi setup</a>
</body>
</html>
)html";

void handleRoot() {
  String presetList;
  for (size_t i = 0; i < presets.size(); ++i) {
    presetList += "<li class='list-group-item d-flex justify-content-between align-items-center'>";
    presetList += presets[i].name;
    if (i == currentPreset)
      presetList += " <span class='badge bg-success'>active</span>";
    else
      presetList += " <a class='btn btn-sm btn-outline-light' href='/set?i=" + String(i) + "'>Select</a>";
    presetList += "</li>";
  }

  String html = FPSTR(HTML_PAGE);
  html.replace("%PRESETS%", presetList);
  server.send(200, "text/html", html);
}

/**
 * Add a new static preset from form input
 */
void handleAdd() {
#if USE_AUTH
  if (!server.hasArg("token") || server.arg("token") != cfg::TOKEN) {
    server.send(403, "text/html",
                "<html><body><p>Invalid token.</p><a href='/' >Back</a></body></html>");
    return;
  }
#endif
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

  uint32_t colorVal;
  if (!parseHexColor(colorStr.c_str() + 1, colorVal)) {
    server.send(400, "text/html",
                "<html><body><p>Color contains invalid hex characters.</p><a href='/' >Back</a></body></html>");
    return;
  }

  presets.insert(presets.end() - 1,
                 {name, PresetType::STATIC,
                  CRGB((colorVal >> 16) & 0xFF, (colorVal >> 8) & 0xFF,
                       colorVal & 0xFF)});
  currentPreset = presets.size() - 2;
  saveCustomPresets();
  applyPreset();

  server.sendHeader("Location", "/");
  server.send(303);
}

/**
 * Set active preset by index via query parameter 'i'
 */
void handleSet() {
  if (!server.hasArg("i")) {
    server.send(400, "text/plain", "Missing index");
    return;
  }
  int idx = server.arg("i").toInt();
  if (idx < 0 || idx >= presets.size()) {
    server.send(400, "text/plain", "Invalid index");
    return;
  }
  currentPreset = idx;
  applyPreset();
  server.sendHeader("Location", "/");
  server.send(303);
}

/** Navigate to next preset */
void handleNext() {
  nextPreset();
  server.sendHeader("Location", "/");
  server.send(303);
}

/** Navigate to previous preset */
void handlePrev() {
  previousPreset();
  server.sendHeader("Location", "/");
  server.send(303);
}

/**
 * Display form for WiFi credentials
 */
void handleWifiForm() {
  String html =
      "<!DOCTYPE html><html><head><title>WiFi Setup</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css'></head>"
      "<body class='container py-4'>"
      "<h1 class='mb-3'>WiFi Credentials</h1>"
      "<form method='POST' action='/wifi' class='row g-3'>"
      "<div class='col-12'><label class='form-label' for='ssid'>SSID</label>"
      "<input class='form-control' id='ssid' name='ssid' value='" +
      storedSSID + "'></div>"
      "<div class='col-12'><label class='form-label' for='password'>Password</label>"
      "<input class='form-control' id='password' name='password' type='password' value='" +
      storedPassword + "'></div>"
      "<div class='form-group'><label for='host'>Device name</label>"
      "<input class='form-control' id='host' name='host' value='" +
      storedHostname + "'></div>"
      "<button class='btn btn-primary'>Save</button></form>"
      "</body></html>";
  server.send(200, "text/html", html);
}

/**
 * Save WiFi credentials from form
 */
void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password") || !server.hasArg("host")) {
    server.send(400, "text/html",
                "<html><body><p>Missing SSID, password, or device name.</p><a href='/wifi'>Back</a></body></html>");
    return;
  }
  saveCredentials(server.arg("ssid"), server.arg("password"), server.arg("host"));
  connectWiFi();
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
#if USE_AUTH
  String prefix = String(cfg::TOKEN) + ":";
  if (!msg.startsWith(prefix))
    return;
  msg = msg.substring(prefix.length());
#endif
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
  } else if (msg.startsWith("bright:")) {
    String valStr = msg.substring(7);
    bool digitsOnly = valStr.length() > 0;
    for (size_t i = 0; i < valStr.length() && digitsOnly; ++i) {
      digitsOnly = isDigit(valStr[i]);
    }
    if (digitsOnly) {
      int val = valStr.toInt();
      if (val >= 0 && val <= 255) {
        brightness = val;
        FastLED.setBrightness(brightness);
        applyPreset();
      }
    }
  } else if (msg.startsWith("color:")) {
    String colorStr = msg.substring(6);
    if (colorStr.length() == 7 && colorStr[0] == '#') {
      colorStr = colorStr.substring(1);
      bool hexOnly = colorStr.length() == 6;
      for (size_t i = 0; i < colorStr.length() && hexOnly; ++i) {
        hexOnly = isxdigit(static_cast<unsigned char>(colorStr[i]));
      }
      if (hexOnly) {
        long val = strtol(colorStr.c_str(), nullptr, 16);
        presets[currentPreset].color =
            CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
        applyPreset();
      }
    }
  } else if (msg.startsWith("speed:")) {
    String valStr = msg.substring(6);
    bool digitsOnly = valStr.length() > 0;
    for (size_t i = 0; i < valStr.length() && digitsOnly; ++i) {
      digitsOnly = isDigit(valStr[i]);
    }
    if (digitsOnly) {
      int val = valStr.toInt();
      if (val > 0)
        animInterval = val;
    }
  }
}

/**
 * Initialize hardware and network services
 */
void setup() {
  Serial.begin(115200);
  pinMode(cfg::BTN_PREV, INPUT_PULLUP);
  // Buttons are active-low. Pins 34-39 do not support internal pull-ups,
  // so fall back to plain INPUT when necessary.
  if (cfg::BTN_NEXT >= 34)
    pinMode(cfg::BTN_NEXT, INPUT);
  else
    pinMode(cfg::BTN_NEXT, INPUT_PULLUP);
  FastLED.addLeds<WS2812, cfg::LED_PIN, GRB>(leds, cfg::NUM_LEDS);
  FastLED.setBrightness(brightness);

  SPIFFS.begin(true);
  loadDefaultPresets();
  loadCustomPresets();
  presets.push_back({"Off", PresetType::STATIC, CRGB::Black});
  currentPreset = presets.size() - 1;

  connectWiFi();

  server.on("/", handleRoot);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/next", HTTP_GET, handleNext);
  server.on("/prev", HTTP_GET, handlePrev);
  server.on("/wifi", HTTP_GET, handleWifiForm);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.begin();

  ws.begin();
  ws.onEvent(wsEvent);

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

  if (millis() - lastAnim > animInterval) {
    applyPreset();
    lastAnim = millis();
  }
}
