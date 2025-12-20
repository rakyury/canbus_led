# CAN LED status for TTGO T-CAN48

Example firmware for LilyGO® TTGO T-CAN48 (ESP32 + CAN) that shows vehicle statuses on a WS2812/Neopixel strip. The strip appearance changes based on commands received over CAN. A built-in Wi‑Fi web interface shows current values and the latest CAN frames.

## Features
- CAN bus speed 1 Mbps (ESP32 TWAI/CAN driver).
- Renders throttle, brake, handbrake, RPM, coolant temperature, and rev limiter status.
- Separate visuals for ALS (anti-lag), engine warmup (t<60°C), and panic if oil pressure is low while throttle is open.
- Configurable CAN pins and LED pin.
- Simple HTTP server to view the active lighting mode and recently received frames (JSON API `/api/state`).
- Suitable for high-density WS2811-based 12 V FCOB addressable strips when powered separately (data remains 5 V logic).
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

> The `platformio.ini` trims RTTI/exceptions and enables aggressive dead-code elimination to keep the final firmware as small as possible while remaining compatible with the stock ESP32 toolchain. Bluetooth has been removed to save flash; set Wi‑Fi credentials in `src/main.cpp`.

## Logic tuning
Key parameters live in [`src/main.cpp`](src/main.cpp):
- `CAN_TX_PIN`, `CAN_RX_PIN` — CAN transceiver pins.
- `LED_PIN`, `LED_COUNT`, `LED_BRIGHTNESS` — LED strip parameters.
- `ID_*` — CAN protocol identifiers.
- `rpmRedline` inside `VehicleState` — redline point for visuals.
- `WIFI_SSID`, `WIFI_PASSWORD` — Wi‑Fi credentials defined at build time. After connecting, open `http://<device_ip>/` for the web page or `http://<device_ip>/api/state` for JSON.

To adjust visuals edit `drawThrottleBar`, `drawRpmGradient`, `drawCoolantIndicator`, `applyBrakeOverlays`, `drawRevLimiter`.
