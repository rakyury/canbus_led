# CAN LED status for TTGO T-CAN48

Example firmware for LilyGO® TTGO T-CAN48 (ESP32 + CAN) that shows vehicle statuses on a WS2812/Neopixel strip. The strip appearance changes based on commands received over CAN. A built-in Wi‑Fi access point hosts a web interface that shows current values and the latest CAN frames.

## Features
- CAN bus speed 1 Mbps (ESP32 TWAI/CAN driver).
- Renders throttle, brake, handbrake, clutch pressures, RPM, coolant temperature, and rev limiter status.
- Separate visuals for ALS (anti-lag), engine warmup (t<60°C), and panic if oil pressure is low while throttle is open.
- Standby visualization when ignition is on but the engine is stopped.
- Configurable CAN pins and LED pin.
- Simple HTTP server to view the active lighting mode and recently received frames (JSON API `/api/state`) via the onboard access point.
- Suitable for high-density WS2811-based 12 V FCOB addressable strips when powered separately (data remains 5 V logic).
- Simple CAN protocol — see [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md) for pedal percentages and ignition state frames.

### Error Handling & Monitoring
- **CAN bus health monitoring** — Detects bus-off, error-passive states, and automatically attempts recovery.
- **Data validation** — Validates incoming CAN values for RPM (<12000), coolant temperature (<150°C), and oil pressure (<10 bar) against reasonable ranges.
- **Staleness detection** — Warns if vehicle data hasn't been updated for 2 seconds.
- **LED error visualization** — Visual feedback for CAN errors (blinking red) and stale data (yellow blink).
- **LED self-test** — Brief RGB cycle on first LED during startup to verify strip functionality.
- **Wi-Fi retry logic** — Attempts to start access point up to 5 times with 5-second backoff, then gracefully fails.
- **Web interface diagnostics** — Displays prominent warnings for CAN errors, Wi-Fi failures, stale data, and LED issues.
- **Enhanced JSON API** — Status fields include `can_bus_status`, `can_error_message`, `data_stale`, `led_strip_ok`, `wifi_status`.

## Startup Sequence

On boot, the firmware performs the following initialization:

1. **Serial output** — Opens at 115200 baud for diagnostics
2. **LED self-test** — First LED briefly cycles through red, green, blue (450ms total)
3. **CAN bus initialization** — Installs TWAI driver and starts at 1 Mbps
4. **Wi-Fi access point** — Starts `CANLED_AP` network (retries up to 5 times if needed)
5. **HTTP server** — Starts on port 80 if Wi-Fi succeeds

Watch the serial output to verify successful initialization. Each subsystem reports its status.

## LED Error Patterns

The LED strip provides visual feedback for system errors (highest priority first):

| Pattern | Meaning | Action Required |
|---------|---------|-----------------|
| **Blinking red** (4 Hz) | CAN bus error (driver install failed, start failed, or bus-off) | Check wiring, GPIO pins, termination resistor. See serial logs for details. |
| **Red/white strobe** (fast) | Oil pressure panic (oil <2 bar @ throttle >40%) | Critical! Check vehicle oil system immediately. |
| **Dim yellow blink** (3 Hz, every 4th LED) | Stale data (no CAN updates for 2+ seconds) | Check CAN transmitter or bus connection. |
| **Normal patterns** | System operating correctly | — |

The first LED shows red-green-blue briefly on startup to verify LED strip is working.

## Wiring
See the diagram and notes in [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md). In short:
- CANH/CANL go to the vehicle bus, GND to chassis ground.
- LED DIN goes to `GPIO4`, LED power is 5 V.
- Serial logs at 115200 baud, including received CAN frame debug lines and system status.

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

> The `platformio.ini` trims RTTI/exceptions and enables aggressive dead-code elimination to keep the final firmware as small as possible while remaining compatible with the stock ESP32 toolchain. Bluetooth has been removed to save flash; the device now starts its own access point defined in `src/main.cpp`.

## Configuration

Key parameters live in [`src/main.cpp`](src/main.cpp):

### Hardware Configuration
- `CAN_TX_PIN`, `CAN_RX_PIN` — CAN transceiver pins (default GPIO21/GPIO22)
- `LED_PIN`, `LED_COUNT`, `LED_BRIGHTNESS` — LED strip parameters

### CAN Protocol
- `ID_*` — CAN message identifiers (see [`docs/CAN_PROTOCOL.md`](docs/CAN_PROTOCOL.md))

### Validation Ranges
- `MAX_REASONABLE_RPM` — 12000 (values above this are rejected)
- `MAX_REASONABLE_COOLANT` — 1500 (150°C)
- `MAX_REASONABLE_OIL_PRESSURE` — 1000 (10 bar)

### Monitoring Thresholds
- `DATA_STALE_THRESHOLD_MS` — 2000 (ms without updates triggers stale warning)
- `CAN_TIMEOUT_WARNING_MS` — 5000 (ms without any CAN messages)
- `MAX_WIFI_RETRIES` — 5 attempts
- `WIFI_RETRY_INTERVAL_MS` — 5000 (ms between retry attempts)

### Network
- `WIFI_SSID`, `WIFI_PASSWORD` — Access point credentials (compile-time constant)
- Connect to `CANLED_AP` and open `http://192.168.4.1/` for web UI
- JSON API available at `http://192.168.4.1/api/state`

### Vehicle Parameters
- `rpmRedline` inside `VehicleState` — Redline for visual effects (default 6500)

### Visual Customization
To adjust LED patterns, edit these functions:
- `drawThrottleBar` — Green bar for throttle position
- `drawRpmGradient` — Blue-to-yellow gradient for RPM
- `drawCoolantIndicator` — Last LED shows temperature
- `applyPedalOverlays` — Brake/handbrake/clutch overlays
- `drawRevLimiter` — Yellow pulsing when limiter active

## Troubleshooting

### LED strip shows blinking red
**Problem:** CAN bus initialization failed or bus-off condition.

**Solutions:**
1. Check serial output (115200 baud) for error details
2. Verify CANH/CANL wiring and polarity
3. Check GPIO pin configuration matches your hardware
4. Verify 120Ω termination resistor is present
5. Ensure GND is connected to vehicle chassis

### Web interface not accessible
**Problem:** Wi-Fi access point failed to start.

**Solutions:**
1. Check serial logs for Wi-Fi error messages
2. Verify `WIFI_SSID` and `WIFI_PASSWORD` are valid (min 8 chars)
3. Try reflashing firmware
4. Check for Wi-Fi hardware conflicts

### LED strip shows yellow blinking pattern
**Problem:** Data staleness - no CAN messages received for 2+ seconds.

**Solutions:**
1. Verify CAN transmitter is sending messages
2. Check CAN bus for shorts or disconnection
3. Verify correct CAN bitrate (1 Mbps)

### Invalid data warnings in serial output
**Problem:** CAN messages contain out-of-range values.

**Solutions:**
1. Check CAN transmitter for data corruption
2. Verify sensor calibration on sending device
3. Adjust `MAX_REASONABLE_*` constants if using different sensors

### No LED output at all
**Problem:** LED strip not initializing.

**Solutions:**
1. Check if first LED shows RGB cycle on startup
2. Verify LED strip power (5V) and data (GPIO4) connections
3. Try different LED strip or check for hardware damage
4. Verify `LED_PIN` and `LED_COUNT` are correct
