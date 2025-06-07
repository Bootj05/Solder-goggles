/**
 * Firmware for LED goggles
 * Provides WiFi control and OTA updates
 */
#include "secrets.h"
#include "utils.h"
#include <pgmspace.h>
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

#ifndef KEEP_NAMES_IN_FLASH
#define KEEP_NAMES_IN_FLASH 0
#endif

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
  CRGB color; // Only for STATIC
  CRGB leds[cfg::NUM_LEDS]; // Only for CUSTOM
  uint8_t effects[cfg::NUM_LEDS]; // Optional per-LED effect
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
const size_t DEFAULT_PRESET_COUNT = sizeof(defaultPresets) / sizeof(defaultPresets[0]);

constexpr char DEFAULT_HOST[] = "JohannesBril";


CRGB leds[cfg::NUM_LEDS];
int currentPreset = 0;
uint8_t rainbowHue = 0;

unsigned long lastAnim = 0;
uint8_t brightness = 255;
unsigned long animInterval = 50;

bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;
unsigned long wifiLastPrint = 0;

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
  wifiConnectStart = millis();
  wifiConnecting = true;
  wifiLastPrint = 0;
  Serial.print("Connecting to WiFi");
}

void handleWiFi() {
  if (!wifiConnecting)
    return;
  const char *host = storedHostname.length() ? storedHostname.c_str() : DEFAULT_HOST;
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
      for (int i = 0; i < cfg::NUM_LEDS; ++i) {
        p.leds[i] = CRGB::Black;
        p.effects[i] = 0;
      }
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
        sprintf(buf, "%02x%02x%02x", presets[i].leds[j].r,
                presets[i].leds[j].g, presets[i].leds[j].b);
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
      sprintf(buf, "%s,%d,%02x%02x%02x\n", n.c_str(),
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
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      leds[i] = presets[currentPreset].leds[i];
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
  <a class='btn btn-link mt-3' href='/wifi'>WiFi setup</a>
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
                   "' class='d-flex justify-content-between align-items-center text-reset text-decoration-none stretched-link'>";
#if KEEP_NAMES_IN_FLASH
    if (presets[i].flashName)
      presetList += FPSTR(presets[i].flashName);
    else
      presetList += presets[i].name;
#else
    presetList += presets[i].name;
#endif
    if (i == currentPreset)
      presetList += " <span class='badge bg-success position-relative z-3'>active</span>";
    presetList += "</a></li>";
  }

  String html = FPSTR(HTML_PAGE);
  html.replace("%PRESETS%", presetList);
  html.replace("%BRIGHT%", String(brightness));
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
<input class='form-control' id='ssid' name='ssid'></div>
<div class='col-12'><label class='form-label' for='password'>Password</label>
<input class='form-control' id='password' name='password' type='password'></div>
<div class='form-group'><label for='host'>Device name</label>
<input class='form-control' id='host' name='host'></div>
<button class='btn btn-primary'>Save</button></form>
</body></html>
)html";

void handleWifiForm() {
  server.send_P(200, "text/html", WIFI_FORM_HTML);
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
  } else if (msg.startsWith("leds:")) {
    String data = msg.substring(5);
    presets[currentPreset].type = PresetType::CUSTOM;
    for (int i = 0; i < cfg::NUM_LEDS; ++i) {
      presets[currentPreset].leds[i] = CRGB::Black;
      presets[currentPreset].effects[i] = 0;
    }
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
  server.on("/next", HTTP_GET, handleNext);
  server.on("/prev", HTTP_GET, handlePrev);
  server.on("/bright", HTTP_GET, handleBright);
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

  handleWiFi();

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
