# Solder-goggles

Pair of specs with magnifier glass upgraded with 13 LEDs.

## Firmware

Firmware lives in `src` and is built with [PlatformIO](https://platformio.org/).
Copy `include/secrets_example.h` to `include/secrets.h` and add your WiFi
credentials as `WIFI_SSID` and `WIFI_PASSWORD`.

### Features
- Web interface for switching LED presets
- OTA updates over WiFi
- Simple WebSocket API for remote control

### Building
Run `setup.sh` once to install PlatformIO and build the firmware:

```bash
./setup.sh
```

If you already have PlatformIO installed you can build manually with:

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
