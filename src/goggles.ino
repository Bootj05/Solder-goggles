/**
 * Firmware for LED goggles
 * Provides WiFi control and OTA updates
 */
// Copyright 2025 Bootj05
#include <ctype.h>
#include <pgmspace.h>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
#include <algorithm>

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <BluetoothSerial.h>
#include <Update.h>

#include "secrets.h"  // NOLINT(build/include_subdir)
#include "utils.h"

#ifndef KEEP_NAMES_IN_FLASH
#define KEEP_NAMES_IN_FLASH 0
#endif

namespace cfg {
  constexpr uint8_t LED_PIN = 2;
  constexpr uint8_t NUM_LEDS = 13;
  constexpr uint8_t BTN_PREV = 0;
  constexpr uint8_t BTN_NEXT = 35;
  // When held this button temporarily activates a user-selected preset
  constexpr uint8_t BTN_HOLD = 17;
  const char *SSID = WIFI_SSID;
  const char *PASSWORD = WIFI_PASSWORD;
#ifdef USE_AUTH
  const char *TOKEN = AUTH_TOKEN;
#endif
}  // namespace cfg

/**
 * Types of LED animations.
 *
 * - `STATIC` &mdash; Solid color chosen per preset.
 * - `RAINBOW` &mdash; Continuously cycling rainbow.
 * - `POLICE_NL` &mdash; Segmented blue pattern with timed strobe like Dutch police lights.
 * - `POLICE_USA` &mdash; Red, white and blue pattern similar to US police lights.
 * - `STROBE` &mdash; Fast white flash on and off.
 * - `LAVALAMP` &mdash; Slowly moving color gradient.
 * - `FIRE`       Randomized warm flicker
 * - `CANDLE`     Single warm flickering glow
 * - `PARTY`      Random bright colors
 * - `CUSTOM`     Per-LED colors stored from the client
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
  PARTY,
  CUSTOM
};
struct Preset {
#if KEEP_NAMES_IN_FLASH
  const __FlashStringHelper *flashName = nullptr;
#endif
  String name;
  PresetType type;
  CRGB color;               // Only for STATIC
  std::vector<CRGB> leds;        // Only for CUSTOM
  std::vector<uint8_t> effects;  // Optional per-LED effect

  Preset()
      : name(),
        type(PresetType::STATIC),
        color(CRGB::Black),
        leds(),
        effects() {}

  Preset(const String &n, PresetType t, CRGB c)
      : name(n),
        type(t),
        color(c),
        leds(),
        effects() {
    if (t == PresetType::CUSTOM) {
      leds.assign(cfg::NUM_LEDS, CRGB::Black);
      effects.assign(cfg::NUM_LEDS, 0);
    }
  }

  Preset(const Preset &other) = default;

  Preset &operator=(const Preset &other) = default;

  Preset(Preset &&other) noexcept = default;

  Preset &operator=(Preset &&other) noexcept = default;

  ~Preset() = default;
};

struct PresetData {
  uint8_t nameIdx;
  PresetType type;
  CRGB color;
};

constexpr const __FlashStringHelper *defaultPresetNames[] PROGMEM = {
    F("White"),     F("Rainbow"),   F("Police NL"), F("Police USA"),
    F("Strobe"),    F("Lavalamp"),  F("Fire"),      F("Candle"),
    F("Party")};

constexpr PresetData defaultPresets[] PROGMEM = {
    {0, PresetType::STATIC, CRGB::White},
    {1, PresetType::RAINBOW, CRGB::Black},
    {2, PresetType::POLICE_NL, CRGB::Black},
    {3, PresetType::POLICE_USA, CRGB::Black},
    {4, PresetType::STROBE, CRGB::Black},
    {5, PresetType::LAVALAMP, CRGB::Black},
    {6, PresetType::FIRE, CRGB::Black},
    {7, PresetType::CANDLE, CRGB::Black},
    {8, PresetType::PARTY, CRGB::Black},
};
std::vector<Preset> presets;
const size_t DEFAULT_PRESET_COUNT =
    sizeof(defaultPresets) / sizeof(defaultPresets[0]);

constexpr char DEFAULT_HOST[] = "JohannesBril";


CRGB leds[cfg::NUM_LEDS];
int currentPreset = 0;
// Index of the preset triggered when BTN_HOLD is pressed
int holdPreset = 0;
// Previous preset to restore after releasing BTN_HOLD
int savedPreset = -1;
uint8_t rainbowHue = 0;

uint32_t lastAnim = 0;
uint8_t brightness = 255;
uint32_t animInterval = 50;

bool wifiConnecting = false;
uint32_t wifiConnectStart = 0;
uint32_t wifiLastPrint = 0;
bool wifiApActive = false;

Preferences prefs;
String storedSSID;
String storedPassword;
String storedHostname;

WebServer server(80);
WebSocketsServer ws(81);
BluetoothSerial bt;

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

void loadHoldPreset() {
  prefs.begin("cfg", true);
  holdPreset = prefs.getInt("hold", 0);
  prefs.end();
}

void saveHoldPreset() {
  prefs.begin("cfg", false);
  prefs.putInt("hold", holdPreset);
  prefs.end();
}

/**
 * Connect to WiFi using stored credentials if available
 */

void connectWiFi() {
  loadCredentials();
  const char *ssid =
      storedSSID.length() ? storedSSID.c_str() : cfg::SSID;
  const char *pass =
      storedPassword.length() ? storedPassword.c_str() : cfg::PASSWORD;
  const char *host =
      storedHostname.length() ? storedHostname.c_str() : DEFAULT_HOST;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  wifiApActive = false;
  WiFi.begin(ssid, pass);
  wifiConnectStart = millis();
  wifiConnecting = true;
  wifiLastPrint = 0;
  Serial.print("Connecting to WiFi");
}

void handleWiFi() {
  if (!wifiConnecting)
    return;
  const char *host =
      storedHostname.length() ? storedHostname.c_str() : DEFAULT_HOST;
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    Serial.println(" connected!");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(host)) {
      Serial.print("mDNS active on ");
      Serial.print(host);
      Serial.println(".local");
    }
    ArduinoOTA.setHostname(host);
    return;
  }
  if (millis() - wifiConnectStart > 15000) {
    wifiConnecting = false;
    Serial.println(" failed to connect.");
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(host)) {
      Serial.print("Started access point ");
      Serial.print(host);
      Serial.print(" at ");
      Serial.println(WiFi.softAPIP());
    }
    wifiApActive = true;
    return;
  }
  if (millis() - wifiLastPrint > 500) {
    Serial.print('.');
    wifiLastPrint = millis();
  }
}

void loadDefaultPresets() {
  presets.clear();
  for (size_t i = 0; i < DEFAULT_PRESET_COUNT; ++i) {
    PresetData data;
    memcpy_P(&data, &defaultPresets[i], sizeof(PresetData));
    Preset p;
#if KEEP_NAMES_IN_FLASH
    p.flashName = reinterpret_cast<const __FlashStringHelper *>(
        pgm_read_ptr(&defaultPresetNames[data.nameIdx]));
#else
    char buf[32];
    strcpy_P(buf,
             reinterpret_cast<const char *>(
                 pgm_read_ptr(&defaultPresetNames[data.nameIdx])));
    p.name = String(buf);
#endif
    p.type = data.type;
    p.color = data.color;
    presets.push_back(p);
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
    Preset p;
#if KEEP_NAMES_IN_FLASH
    p.flashName = nullptr;
#endif
    p.name = name;
    p.type = static_cast<PresetType>(type);
    p.color = CRGB::Black;
    if (p.type == PresetType::CUSTOM) {
      p.leds.assign(cfg::NUM_LEDS, CRGB::Black);
      p.effects.assign(cfg::NUM_LEDS, 0);
      int idx = 0;
      while (idx < cfg::NUM_LEDS && colStr.length()) {
        int sep = colStr.indexOf(';');
        String tok = sep == -1 ? colStr : colStr.substring(0, sep);
        if (tok.startsWith("#"))
          tok.remove(0, 1);
        uint32_t val;
        if (parseHexColor(tok.c_str(), val)) {
          p.leds[idx] = CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF,
                             val & 0xFF);
        }
        if (sep == -1)
          colStr = "";
        else
          colStr = colStr.substring(sep + 1);
        ++idx;
      }
    } else {
      uint32_t val = strtoul(colStr.c_str(), nullptr, 16);
      p.color = CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }
    presets.push_back(p);
  }
  f.close();
}

void saveCustomPresets() {
  File f = SPIFFS.open("/presets.txt", "w");
  if (!f)
    return;
  for (size_t i = DEFAULT_PRESET_COUNT; i + 1 < presets.size(); ++i) {
    if (presets[i].type == PresetType::CUSTOM) {
      String line =
#if KEEP_NAMES_IN_FLASH
          (presets[i].flashName
               ? String(FPSTR(presets[i].flashName))
               : presets[i].name) +
#else
          presets[i].name +
#endif
          "," + String(static_cast<int>(presets[i].type)) + ",";
      for (int j = 0; j < cfg::NUM_LEDS; ++j) {
        char buf[8];
        CRGB c = j < presets[i].leds.size() ? presets[i].leds[j] : CRGB::Black;
        snprintf(buf, sizeof(buf), "%02x%02x%02x", c.r, c.g, c.b);
        line += buf;
        if (j + 1 < cfg::NUM_LEDS)
          line += ';';
      }
      line += "\n";
      f.print(line);
    } else {
      char buf[64];
      String n =
#if KEEP_NAMES_IN_FLASH
          (presets[i].flashName
               ? String(FPSTR(presets[i].flashName))
               : presets[i].name);
#else
          presets[i].name;
#endif
      snprintf(buf, sizeof(buf), "%s,%d,%02x%02x%02x\n", n.c_str(),
               static_cast<int>(presets[i].type), presets[i].color.r,
               presets[i].color.g, presets[i].color.b);
      f.print(buf);
    }
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

  case PresetType::POLICE_NL: {
    static bool phase = false;        // false = groups 1&3, true = groups 2&4
    static uint32_t lastToggle = 0;
    static uint32_t flashCount = 0;
    static bool strobe = false;
    static uint8_t strobeGroup = 0;   // 1 or 2 when strobing
    const uint16_t flashInterval = 250;  // ms
    const uint16_t strobeInterval = 20;  // ms

    if (millis() - lastToggle >= flashInterval) {
      phase = !phase;
      lastToggle = millis();
      ++flashCount;
      strobe = false;
      strobeGroup = 0;
      if (flashCount % 20 == 0) {
        strobe = true;
        strobeGroup = 1;  // groups 1 & 3
      } else if (flashCount % 20 == 10) {
        strobe = true;
        strobeGroup = 2;  // groups 2 & 4
      }
    }

    auto setGroup = [&](int start, int count, CRGB color) {
      for (int i = start; i < start + count; ++i) {
        leds[i] = color;
      }
    };

    bool strobeOn = strobe && ((millis() / strobeInterval) % 2 == 0);

    // Map groups: 0-2, 3-5, 6-8, 9-12
    if (strobe) {
      if (strobeGroup == 1) {
        setGroup(0, 3, strobeOn ? CRGB::Blue : CRGB::Black);
        setGroup(6, 3, strobeOn ? CRGB::Blue : CRGB::Black);
        setGroup(3, 3, CRGB::Black);
        setGroup(9, 4, CRGB::Black);
      } else {
        setGroup(3, 3, strobeOn ? CRGB::Blue : CRGB::Black);
        setGroup(9, 4, strobeOn ? CRGB::Blue : CRGB::Black);
        setGroup(0, 3, CRGB::Black);
        setGroup(6, 3, CRGB::Black);
      }
    } else if (!phase) {
      setGroup(0, 3, CRGB::Blue);
      setGroup(6, 3, CRGB::Blue);
      setGroup(3, 3, CRGB::Black);
      setGroup(9, 4, CRGB::Black);
    } else {
      setGroup(3, 3, CRGB::Blue);
      setGroup(9, 4, CRGB::Blue);
      setGroup(0, 3, CRGB::Black);
      setGroup(6, 3, CRGB::Black);
    }
    break;
  }

  case PresetType::POLICE_USA: {
    static uint8_t step = 0;
    static uint32_t lastPulse = 0;
    const uint8_t pulses = 3;
    const uint16_t pulseInterval = 150;  // ms per on/off step

    if (millis() - lastPulse >= pulseInterval) {
      lastPulse = millis();
      step = (step + 1) % (pulses * 4);
    }

    bool on = (step % 2 == 0);
    bool left = (step / (pulses * 2)) % 2 == 0;

    auto setRange = [&](int start, int count, CRGB color) {
      for (int i = start; i < start + count; ++i) {
        leds[i] = color;
      }
    };

    if (on) {
      setRange(0, 6, left ? CRGB::Red : CRGB::Black);
      setRange(6, 1, CRGB::White);
      setRange(7, 6, left ? CRGB::Black : CRGB::Blue);
    } else {
      fill_solid(leds, cfg::NUM_LEDS, CRGB::Black);
    }
    break;
  }

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

  case PresetType::CUSTOM: {
    if (!presets[currentPreset].leds.empty()) {
      for (int i = 0; i < cfg::NUM_LEDS; ++i) {
        leds[i] = presets[currentPreset].leds[i];
      }
    } else {
      fill_solid(leds, cfg::NUM_LEDS, CRGB::Black);
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
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700&display=swap" rel="stylesheet">
  <style>
    body {
      font-family: 'Orbitron', sans-serif;
      background: radial-gradient(circle at top, #0d1b2a, #000);
      color: #d8e3ff;
    }
    h1 {
      letter-spacing: 2px;
    }
    .btn-primary,
    .btn-secondary {
      background-color: #1282a2;
      border-color: #1282a2;
    }
    .btn-secondary {
      background-color: #1e2d45;
    }
    .list-group-item {
      background-color: rgba(255,255,255,0.03);
      border-color: #1282a2;
    }
    a.btn-link {
      color: #5bc0eb;
    }
  </style>
</head>
<body class="container py-4">
  <h1 class="mb-4 text-center">Solder Goggles</h1>
  <div class='mb-3 text-center'>
    <a class='btn btn-secondary me-2' href='/prev'>Prev</a>
    <a class='btn btn-secondary' href='/next'>Next</a>
  </div>
  <div class='mb-4'>
    <label for='bright' class='form-label'>Brightness</label>
    <input type='range' class='form-range' id='bright' min='0' max='255'
           value='%BRIGHT%'
           onchange="location.href='/bright?b=' + this.value">
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
  <form method='GET' action='/hold' class='row row-cols-lg-auto g-3 align-items-center mt-3'>
    <div class='col-12'>
      <select class='form-select' id='hold' name='i'>
        %HOLD_OPTIONS%
      </select>
    </div>
    <div class='col-12'>
      <button class='btn btn-secondary'>Set Hold Preset</button>
    </div>
  </form>
  <a class='btn btn-link mt-3' href='/wifi'>WiFi setup</a>
  <a class='btn btn-link mt-3' href='/update'>OTA update</a>
</body>
</html>
)html";

void handleRoot() {
  String presetList;
  // Reserve some space to avoid repeated reallocations while building the
  // preset list. Each list element is roughly 80 characters long.
  presetList.reserve(presets.size() * 80);
  for (size_t i = 0; i < presets.size(); ++i) {
    presetList += "<li class='list-group-item position-relative'>";
      presetList += "<a href='/set?i=" + String(i) +
                     "' class='d-flex justify-content-between align-items-center text-reset text-decoration-none stretched-link'>";  // NOLINT
#if KEEP_NAMES_IN_FLASH
    if (presets[i].flashName)
      presetList += FPSTR(presets[i].flashName);
    else
      presetList += presets[i].name;
#else
    presetList += presets[i].name;
#endif
      if (i == currentPreset)
        presetList += " <span class='badge bg-success position-relative z-3'>active</span>";  // NOLINT
    presetList += "</a></li>";
  }

  String holdOpts;
  holdOpts.reserve(presets.size() * 40);
  for (size_t i = 0; i < presets.size(); ++i) {
    holdOpts += "<option value='" + String(i) + "'";
    if (i == holdPreset)
      holdOpts += " selected";
    holdOpts += ">";
#if KEEP_NAMES_IN_FLASH
    if (presets[i].flashName)
      holdOpts += String(FPSTR(presets[i].flashName));
    else
      holdOpts += presets[i].name;
#else
    holdOpts += presets[i].name;
#endif
    holdOpts += "</option>";
  }

  String html = FPSTR(HTML_PAGE);
  html.replace("%PRESETS%", presetList);
  html.replace("%BRIGHT%", String(brightness));
  html.replace("%HOLD_OPTIONS%", holdOpts);
  server.send(200, "text/html", html);
}

/**
 * Add a new static preset from form input
 */
void handleAdd() {
#if USE_AUTH
  if (!server.hasArg("token") || server.arg("token") != cfg::TOKEN) {
    server.send(403, "text/html",
                "<html><body><p>Invalid token.</p><a href='/' >Back</a></body></html>");  // NOLINT
    return;
  }
#endif
  if (!server.hasArg("name") || !server.hasArg("color")) {
    server.send(400, "text/html",
                "<html><body><p>Missing name or color.</p><a href='/' >Back</a></body></html>");  // NOLINT
    return;
  }

  String name = server.arg("name");
  String colorStr = server.arg("color");

  if (colorStr.length() != 7 || colorStr[0] != '#') {
    server.send(400, "text/html",
                "<html><body><p>Color must be in format #RRGGBB.</p><a href='/' >Back</a></body></html>");  // NOLINT
    return;
  }

  uint32_t colorVal;
  if (!parseHexColor(colorStr.c_str() + 1, colorVal)) {
    server.send(400, "text/html",
                "<html><body><p>Color contains invalid hex characters.</p><a href='/' >Back</a></body></html>");  // NOLINT
    return;
  }

  {
    Preset p;
#if KEEP_NAMES_IN_FLASH
    p.flashName = nullptr;
#endif
    p.name = name;
    p.type = PresetType::STATIC;
    p.color = CRGB((colorVal >> 16) & 0xFF, (colorVal >> 8) & 0xFF,
                   colorVal & 0xFF);
    presets.insert(presets.end() - 1, p);
  }
  currentPreset = presets.size() - 2;
  saveCustomPresets();
  applyPreset();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleCommand(String msg) {
  if (msg == "next") {
    nextPreset();
  } else if (msg == "prev") {
    previousPreset();
  } else if (msg.startsWith("set:")) {
    String idxStr = msg.substring(4);
    bool digitsOnly = idxStr.length() > 0;
    for (size_t i = 0; i < idxStr.length() && digitsOnly; ++i) {
      digitsOnly = isDigit(idxStr[i]);
    }
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
      uint32_t val;
      if (parseHexColor(colorStr.c_str() + 1, val)) {
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
  } else if (msg.startsWith("leds:")) {
    String data = msg.substring(5);
    presets[currentPreset].type = PresetType::CUSTOM;
    if (presets[currentPreset].leds.size() != cfg::NUM_LEDS)
      presets[currentPreset].leds.assign(cfg::NUM_LEDS, CRGB::Black);
    else
      fill_solid(presets[currentPreset].leds.data(), cfg::NUM_LEDS,
                 CRGB::Black);
    if (presets[currentPreset].effects.size() != cfg::NUM_LEDS)
      presets[currentPreset].effects.assign(cfg::NUM_LEDS, 0);
    else
      std::fill(presets[currentPreset].effects.begin(),
                presets[currentPreset].effects.end(), 0);
    int idx = 0;
    while (idx < cfg::NUM_LEDS && data.length()) {
      int sep = data.indexOf(',');
      String tok = sep == -1 ? data : data.substring(0, sep);
      if (tok.startsWith("#"))
        tok.remove(0, 1);
      uint32_t val;
      if (parseHexColor(tok.c_str(), val)) {
        presets[currentPreset].leds[idx] =
            CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
      }
      if (sep == -1)
        data = "";
      else
        data = data.substring(sep + 1);
      ++idx;
    }
    saveCustomPresets();
    applyPreset();
  }
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

/** Set the preset used when BTN_HOLD is pressed */
void handleHold() {
  if (!server.hasArg("i")) {
    server.send(400, "text/plain", "Missing index");
    return;
  }
  int idx = server.arg("i").toInt();
  if (idx < 0 || idx >= presets.size()) {
    server.send(400, "text/plain", "Invalid index");
    return;
  }
  holdPreset = idx;
  saveHoldPreset();
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

/** Adjust brightness via query parameter 'b' (0-255) */
void handleBright() {
  if (!server.hasArg("b")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  int val = server.arg("b").toInt();
  if (val < 0 || val > 255) {
    server.send(400, "text/plain", "Invalid value");
    return;
  }
  brightness = val;
  FastLED.setBrightness(brightness);
  applyPreset();
  server.sendHeader("Location", "/");
  server.send(303);
}

/**
 * Display form for WiFi credentials
 */
const char WIFI_FORM_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head><title>WiFi Setup</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css'></head>
<body class='container py-4'>
<h1 class='mb-3'>WiFi Credentials</h1>
<form method='POST' action='/wifi' class='row g-3'>
<div class='col-12'><label class='form-label' for='ssid'>SSID</label>
<input class='form-control' id='ssid' name='ssid' value='%SSID%'></div>
<div class='col-12'><label class='form-label' for='password'>Password</label>
<input class='form-control' id='password' name='password' type='password'></div>
<div class='form-group'><label for='host'>Device name</label>
<input class='form-control' id='host' name='host' value='%HOST%'></div>
<button class='btn btn-primary'>Save</button></form>
</body></html>
)html";

/**
 * Simple OTA update page
 */
const char UPDATE_FORM_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head><title>OTA Update</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css'></head>
<body class='container py-4'>
<h1 class='mb-3'>OTA Update</h1>
<form method='POST' action='/update' enctype='multipart/form-data' class='row g-3'>
<div class='col-12'><input class='form-control' type='file' name='firmware'></div>
<div class='col-12'><button class='btn btn-primary'>Upload</button></div>
</form>
</body></html>
)html";

void handleWifiForm() {
  loadCredentials();
  String html = FPSTR(WIFI_FORM_HTML);
  html.replace("%SSID%", storedSSID);
  html.replace("%HOST%", storedHostname);
  server.send(200, "text/html", html);
}

/**
 * Save WiFi credentials from form
 */
void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password") ||
      !server.hasArg("host")) {
    server.send(400, "text/html",
                "<html><body><p>Missing SSID, password, or device name.</p><a href='/wifi'>Back</a></body></html>");  // NOLINT
    return;
  }
  saveCredentials(server.arg("ssid"), server.arg("password"),
                  server.arg("host"));
  connectWiFi();
  server.sendHeader("Location", "/");
  server.send(303);
}

/** Display OTA upload form */
void handleUpdateForm() {
  server.send_P(200, "text/html", UPDATE_FORM_HTML);
}

/** Process uploaded firmware */
void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true))
      Serial.printf("Update Success: %u bytes\n", up.totalSize);
    else
      Update.printError(Serial);
  }
}

/** Return update status and reboot */
void handleUpdateResult() {
  server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  delay(1000);
  if (!Update.hasError())
    ESP.restart();
}

/**
 * Handle incoming WebSocket messages
 */
void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  if (type != WStype_TEXT)
    return;
  String msg = String(reinterpret_cast<char *>(payload));
#if USE_AUTH
  String prefix = String(cfg::TOKEN) + ":";
  if (!msg.startsWith(prefix))
    return;
  msg = msg.substring(prefix.length());
#endif
  handleCommand(msg);
}

/**
 * Initialize hardware and network services
 */
void setup() {
  Serial.begin(115200);
  loadCredentials();
  loadHoldPreset();
  const char *btName =
      storedHostname.length() ? storedHostname.c_str() : DEFAULT_HOST;
  bt.begin(btName);
  pinMode(cfg::BTN_PREV, INPUT_PULLUP);
  // Buttons are active-low. Pins 34-39 do not support internal pull-ups,
  // so fall back to plain INPUT when necessary.
  if (cfg::BTN_NEXT >= 34)
    pinMode(cfg::BTN_NEXT, INPUT);
  else
    pinMode(cfg::BTN_NEXT, INPUT_PULLUP);
  if (cfg::BTN_HOLD >= 34)
    pinMode(cfg::BTN_HOLD, INPUT);
  else
    pinMode(cfg::BTN_HOLD, INPUT_PULLUP);
  FastLED.addLeds<WS2812, cfg::LED_PIN, GRB>(leds, cfg::NUM_LEDS);
  FastLED.setBrightness(brightness);

  SPIFFS.begin(true);
  loadDefaultPresets();
  loadCustomPresets();
  {
    Preset p;
#if KEEP_NAMES_IN_FLASH
    p.flashName = reinterpret_cast<const __FlashStringHelper *>(F("Off"));
#else
    p.name = "Off";
#endif
    p.type = PresetType::STATIC;
    p.color = CRGB::Black;
    presets.push_back(p);
  }
  currentPreset = presets.size() - 1;

  connectWiFi();

  server.on("/", handleRoot);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/hold", HTTP_GET, handleHold);
  server.on("/next", HTTP_GET, handleNext);
  server.on("/prev", HTTP_GET, handlePrev);
  server.on("/bright", HTTP_GET, handleBright);
  server.on("/wifi", HTTP_GET, handleWifiForm);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/update", HTTP_GET, handleUpdateForm);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
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
  static bool lastBtnHold = HIGH;
  static uint32_t lastDebounce = 0;
  static wl_status_t lastWiFiStatus = WL_IDLE_STATUS;
  const uint32_t debounceDelay = 50;

  bool btnPrev = digitalRead(cfg::BTN_PREV) == LOW;
  bool btnNext = digitalRead(cfg::BTN_NEXT) == LOW;
  bool btnHold = digitalRead(cfg::BTN_HOLD) == LOW;

  wl_status_t status = WiFi.status();
  if (status != lastWiFiStatus) {
    if (status == WL_DISCONNECTED && !wifiConnecting) {
      Serial.println("\nWiFi disconnected, reconnecting...");
      delay(500);
      connectWiFi();
    }
    lastWiFiStatus = status;
  }

  handleWiFi();

  if (millis() - lastDebounce > debounceDelay) {
    if (btnPrev && !lastBtnPrev)
      previousPreset();
    if (btnNext && !lastBtnNext)
      nextPreset();
    if (btnHold && !lastBtnHold) {
      savedPreset = currentPreset;
      if (holdPreset >= 0 && holdPreset < presets.size()) {
        currentPreset = holdPreset;
        applyPreset();
      }
    }
    if (!btnHold && lastBtnHold && savedPreset != -1) {
      currentPreset = savedPreset;
      savedPreset = -1;
      applyPreset();
    }
    lastDebounce = millis();
  }

  lastBtnPrev = btnPrev;
  lastBtnNext = btnNext;
  lastBtnHold = btnHold;

  server.handleClient();
  ws.loop();
  if (bt.available()) {
    String line = bt.readStringUntil('\n');
    line.trim();
    if (line.length())
      handleCommand(line);
  }
  ArduinoOTA.handle();

  if (millis() - lastAnim > animInterval) {
    applyPreset();
    lastAnim = millis();
  }
}
