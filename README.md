# Solder-goggles

Pair of specs with magnifier glass upgraded with 13 LEDs.

## Firmware

Firmware lives in `src` and is built with [PlatformIO](https://platformio.org/).
Copy `include/secrets_example.h` to `include/secrets.h` and add your WiFi
credentials as `WIFI_SSID` and `WIFI_PASSWORD`.
To protect the web interface and WebSocket API you can set `USE_AUTH` to `1`
and choose an `AUTH_TOKEN` in `secrets.h`. When enabled, the `/add` endpoint
expects a `token` parameter and WebSocket commands must be prefixed with
`<token>:`.

### Features
- Web interface for switching LED presets
- OTA updates over WiFi
- Simple WebSocket API for remote control
- Runtime WiFi configuration at `/wifi`

### Hardware
The previous and next buttons are wired as active-low and rely on the microcontroller's internal pull-up resistors.

### WebSocket API
The firmware also runs a WebSocket server on port `81`. Send plain text commands
to control the active preset. Example using [`wscat`](https://github.com/websockets/wscat):

```bash
wscat -c ws://<device_ip>:81/ -x next
```
# If authentication is enabled prepend the token to each command,
# e.g. `wscat -c ws://<device_ip>:81/ -x <token>:next`.

Available commands:

* `next` &mdash; switch to the next preset.
* `prev` &mdash; switch to the previous preset.
* `set:<n>` &mdash; activate the preset with index `<n>` (zero based), e.g. `set:0`.

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
