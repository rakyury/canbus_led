# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based CAN bus LED controller for LilyGO TTGO T-CAN48 boards. The firmware receives vehicle telemetry over CAN bus (1 Mbps) and visualizes it on a WS2812/Neopixel addressable LED strip. It also hosts a Wi-Fi access point with a web interface for real-time monitoring.

## Build and Development Commands

### Build the firmware
```bash
pio run
```

### Flash to device
```bash
# Replace /dev/ttyUSB0 with the correct port for your system
# On macOS, typically /dev/cu.usbserial-* or /dev/cu.wchusbserial*
pio run --target upload --upload-port /dev/ttyUSB0
```

### Monitor serial output
```bash
pio device monitor -b 115200
```

### Clean build
```bash
pio run --target clean
```

## Architecture and Code Structure

### Core Components

**Main loop (src/main.cpp):** The firmware follows a simple polling architecture in `loop()`:
1. Poll CAN bus for new frames (10ms timeout)
2. Process received frames and update `VehicleState`
3. Clear LED buffer
4. Render LED pattern based on current state (layered rendering)
5. Update FastLED display
6. Service HTTP server and Wi-Fi AP

**State management:** All vehicle telemetry is stored in a single `VehicleState` struct that is updated by CAN frame handlers. LED rendering functions read from this global state.

**Layered rendering:** LED patterns are composited through multiple draw functions that blend colors:
- Base layer: `drawThrottleBar()` or `drawIgnitionStandby()`
- Overlays: `drawRpmGradient()`, `drawCoolantIndicator()`, `applyPedalOverlays()`
- Special conditions: `drawRevLimiter()`, `drawAlsOverlay()`, `drawWarmingOverlay()`, `drawPanicError()`

Each render function uses FastLED's `blend()` to layer colors rather than overwriting, creating rich composite effects.

**CAN protocol:** The firmware expects simple standard 11-bit CAN frames with vehicle data. Frame identifiers and data formats are documented in `docs/CAN_PROTOCOL.md`. The `processFrame()` function maps CAN IDs to state updates.

**Telemetry logging:** Recent CAN frames are stored in a circular buffer (`frameLog[]`) for display in the web interface. The web server streams JSON or HTML without blocking the main loop.

### Key Configuration Parameters

All user-configurable values are `constexpr` at the top of `src/main.cpp`:
- **CAN pins:** `CAN_TX_PIN`, `CAN_RX_PIN` (default GPIO21/GPIO22)
- **LED configuration:** `LED_PIN`, `LED_COUNT`, `LED_BRIGHTNESS`
- **CAN protocol:** `ID_THROTTLE`, `ID_PEDALS`, `ID_RPM`, etc.
- **Vehicle parameters:** `state.rpmRedline` (default 6500)
- **Wi-Fi AP:** `WIFI_SSID`, `WIFI_PASSWORD` (hardcoded at compile time)

### Web Interface

The ESP32 runs a minimal HTTP server on port 80:
- **/** - HTML dashboard showing current vehicle state and recent CAN frames
- **/api/state** - JSON API endpoint with telemetry and frame log

Access by connecting to the `CANLED_AP` Wi-Fi network and navigating to `http://192.168.4.1/`

## Visual Logic

The LED strip shows multiple overlapping states simultaneously:

**Primary visualization:**
- Throttle: Green bar proportional to pedal position
- RPM: Blue-to-yellow gradient, red pulsing when above redline
- Coolant: Last LED shows temp gradient (blue 60°C → green 85°C → red 110°C)

**Pedal overlays:**
- Brake: Red overlay on entire strip (intensity scales with pressure)
- Handbrake: Purple overlay on first quarter
- Clutch: Cyan overlay on last section

**Special modes:**
- Rev limiter active: Yellow pulsing across strip
- ALS (anti-lag): Amber pulsing
- Engine warming (<60°C): Breathing blue overlay
- Low oil pressure panic: Red/white strobe (when throttle >40% and oil <2 bar)
- Ignition on, engine off: Amber breathing standby pattern

## Hardware Details

**Platform:** ESP32-based TTGO T-CAN48 with onboard CAN transceiver and terminal block

**CAN bus:** Fixed at 1 Mbps using ESP32 TWAI driver. Accepts all frames (no filtering).

**LED strip:** Any FastLED-compatible addressable strip (WS2812, WS2811, Neopixel). For 12V FCOB WS2811 strips, power LEDs from 12V but keep ESP32 and data line at 5V logic levels.

**Power:** USB or external 5V to T-CAN48. LED strip requires separate power supply for high density/count strips.

## Build Configuration

The `platformio.ini` uses aggressive optimization flags:
- `-Os` size optimization
- `-fno-exceptions -fno-rtti` to reduce binary size
- `-ffunction-sections -fdata-sections -Wl,--gc-sections` for dead code elimination
- `CORE_DEBUG_LEVEL=0` disables debug logging

These settings minimize firmware size for ESP32 while maintaining compatibility with Arduino framework and FastLED library.

## Typical Modification Workflows

**Changing CAN frame IDs:** Update `ID_*` constants and corresponding cases in `processFrame()`.

**Adjusting visual effects:** Modify the draw functions (`drawThrottleBar`, `drawRpmGradient`, etc.). Each uses FastLED color blending and the `beatsin8()` function for pulsing effects.

**Adding new CAN messages:** Add a new `ID_*` constant, add a field to `VehicleState`, handle it in `processFrame()`, and create a corresponding draw function if needed.

**Tuning redline or temp thresholds:** Adjust `state.rpmRedline`, `isWarmingUp()` threshold, or `isPanicError()` conditions in `src/main.cpp`.

## Important Notes

- Serial logs at 115200 baud include CAN frame debug output
- The access point starts automatically on boot; no client-mode Wi-Fi
- CAN receive uses a 10ms timeout to avoid blocking LED updates
- All LED rendering is non-blocking; patterns update at FastLED's maximum rate
- The web server uses chunked transfer encoding to stream responses without buffering
