# Solder-goggles
Pair of specs with magnifier glass upgraded with 13 LEDs.

## Firmware
Firmware lives in `src` and is built with [PlatformIO](https://platformio.org/).
Create `include/secrets.h` and define your WiFi credentials as `WIFI_SSID` and
`WIFI_PASSWORD`. The code drives the LEDs, exposes a small web interface and
supports OTA updates through WiFi.
