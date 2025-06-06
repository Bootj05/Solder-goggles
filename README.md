# Solder-goggles

Pair of specs with magnifier glass upgraded with 13 LEDs.

## Firmware

Firmware lives in `src` and is built with [PlatformIO](https://platformio.org/).
Create `include/secrets.h` with your WiFi credentials as `WIFI_SSID` and
`WIFI_PASSWORD`.

### Features
- Web interface for switching LED presets
- OTA updates over WiFi
- Simple WebSocket API for remote control

### Building
Install [PlatformIO](https://platformio.org/) and run:

```bash
pio run
```

Upload to the device with:

```bash
pio run --target upload
```

While running, check the serial output at `115200` baud:

```bash
pio device monitor
```
