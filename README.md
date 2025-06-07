# Solder-goggles

Pair of specs with magnifier glass upgraded with 13 LEDs.

## Firmware

Firmware lives in `src` and is built with [PlatformIO](https://platformio.org/).
You can copy `include/secrets_example.h` to `include/secrets.h` and add your
WiFi credentials manually, or simply run one of the helper scripts which will
prompt for the SSID and password if the file is missing.
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
# set brightness to half
wscat -c ws://<device_ip>:81/ -x bright:128
```
# If authentication is enabled prepend the token to each command,
# e.g. `wscat -c ws://<device_ip>:81/ -x <token>:next`.

Available commands:

* `next` &mdash; switch to the next preset.
* `prev` &mdash; switch to the previous preset.
* `set:<n>` &mdash; activate the preset with index `<n>` (zero based), e.g. `set:0`.
* `bright:<0-255>` &mdash; set global LED brightness.
* `color:#RRGGBB` &mdash; change the color of the active preset.
* `speed:<ms>` &mdash; change animation update interval in milliseconds.

### WebSocket Commands

Below are example messages for each command when using [`wscat`](https://github.com/websockets/wscat):

```bash
# Advance to the next preset
wscat -c ws://<device_ip>:81/ -x next

# Go back to the previous preset
wscat -c ws://<device_ip>:81/ -x prev

# Explicitly select preset number 2
wscat -c ws://<device_ip>:81/ -x set:2
```

### Building
Run `setup.sh` once to install PlatformIO and build the firmware. If
`include/secrets.h` is missing the script will offer to create it and ask for
your WiFi SSID and password. It also offers to create or use a Python virtual
environment before installing dependencies:

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

Alternatively use `install.sh` to build the project, optionally export the
compiled binary and flash a connected ESP32 automatically. If
`include/secrets.h` is missing it will prompt for your WiFi details just like
`setup.sh`. Both scripts also allow using a virtual environment:

```bash
./install.sh
```

While running, check the serial output at `115200` baud:

```bash
pio device monitor
```

### Linting
Arduino sources are checked with [cpplint](https://github.com/cpplint/cpplint).
Run the linter after making changes:

```bash
cpplint --recursive src include test
```
