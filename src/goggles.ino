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

std::vector<Preset> presets{{"White", PresetType::STATIC, CRGB::White},
                            {"Rainbow", PresetType::RAINBOW, CRGB::Black},
                            {"Police NL", PresetType::POLICE_NL, CRGB::Black},
                            {"Police USA", PresetType::POLICE_USA, CRGB::Black},
                            {"Strobe", PresetType::STROBE, CRGB::Black},
                            {"Lavalamp", PresetType::LAVALAMP, CRGB::Black}};

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
void handleRoot() {
  String html = "<html><body><h1>Presets:</h1><ul>";
  for (size_t i = 0; i < presets.size(); ++i) {
    html += "<li>" + presets[i].name;
    if (i == currentPreset)
      html += " <b>(active)</b>";
    html += "</li>";
  }
  html += "</ul><form method='POST' action='/add'>Name: <input name='name'> "
          "Color (hex like #ff00ff): <input name='color'> "
          "<button>Add</button></form></body></html>";
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
    int idx = msg.substring(4).toInt();
    if (idx >= 0 && idx < presets.size()) {
      currentPreset = idx;
      applyPreset();
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
