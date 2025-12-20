# CAN LED status for TTGO T-CAN48

Example firmware for LilyGO® TTGO T-CAN48 (ESP32 + CAN) that shows vehicle statuses on a WS2812/Neopixel strip. The strip appearance changes based on commands received over CAN. A built-in Wi‑Fi web interface shows current values and the latest CAN frames.

## Features
- CAN bus speed 1 Mbps (ESP32 TWAI/CAN driver).
- Renders throttle, brake, handbrake, RPM, coolant temperature, and rev limiter status.
- Separate visuals for ALS (anti-lag), engine warmup (t<60°C), and panic if oil pressure is low while throttle is open.
- Configurable CAN pins and LED pin.
- Simple HTTP server to view the active lighting mode and recently received frames (JSON API `/api/state`).
- Bluetooth SPP configurator to set Wi‑Fi from a phone/laptop.
- Simple CAN protocol — see [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md).

## Wiring
See the diagram and notes in [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md). In short:
- CANH/CANL go to the vehicle bus, GND to chassis ground.
- LED DIN goes to `GPIO4`, LED power is 5 V.
- Serial logs at 115200 baud, including received CAN frame debug lines.

## Build and flash
PlatformIO is used for the project.

```bash
# Install dependencies and build
pio run

# Flash the firmware (set the correct port)
pio run --target upload --upload-port /dev/ttyUSB0

# Open the serial monitor
pio device monitor -b 115200
```

## Logic tuning
Key parameters live in [`src/main.cpp`](src/main.cpp):
- `CAN_TX_PIN`, `CAN_RX_PIN` — CAN transceiver pins.
- `LED_PIN`, `LED_COUNT`, `LED_BRIGHTNESS` — LED strip parameters.
- `ID_*` — CAN protocol identifiers.
- `rpmRedline` inside `VehicleState` — redline point for visuals.
- `WIFI_SSID`, `WIFI_PASSWORD` — Wi‑Fi credentials. After connecting, open `http://<device_ip>/` for the web page or `http://<device_ip>/api/state` for JSON.
- Bluetooth: device `TCAN48-CFG`. Connect and send text commands: `HELP`, `STATUS`, `SSID <name>`, `PASS <password>`, `SAVE` (persists to flash). After changing SSID/password the module attempts to reconnect to Wi‑Fi.

To adjust visuals edit `drawThrottleBar`, `drawRpmGradient`, `drawCoolantIndicator`, `applyBrakeOverlays`, `drawRevLimiter`.
