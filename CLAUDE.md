# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based CAN bus LED controller for LilyGO TTGO T-CAN48 boards. The firmware receives vehicle telemetry over CAN bus (1 Mbps) and visualizes it on a WS2812/Neopixel addressable LED strip. It also hosts a Wi-Fi access point with a web interface for real-time monitoring.

**Supported CAN Protocols:**
- Custom Protocol (original)
- Link ECU Generic Dashboard (Fury X compatible)
- Link ECU Generic Dashboard 2 (newer protocol)

## Build and Development Commands

### Build the firmware
```bash
pio run
```

### Flash to device
```bash
pio run --target upload --upload-port COM3
```

### Monitor serial output
```bash
pio device monitor -b 115200
```

### Clean build
```bash
pio run --target clean
```

### Test with Serial CAN Emulator GUI (no CAN hardware needed)
```bash
python tools/serial_can_emulator.py
```

## Firmware Output

After building, firmware files are located in:
```
.pio/build/lilygo_tcan48/
├── firmware.bin      # Main firmware binary (upload this)
├── firmware.elf      # ELF with debug symbols
├── bootloader.bin    # ESP32 bootloader
└── partitions.bin    # Partition table
```

**Memory usage (typical):**
- RAM: ~16% (53 KB / 320 KB)
- Flash: ~63% (826 KB / 1.3 MB)

## Architecture and Code Structure

The codebase uses a modular architecture:

```
src/
├── config.h          # All configuration and feature flags
├── types.h           # Data structures (VehicleState, TripStatistics, etc.)
├── can_handler.h/cpp # CAN communication and protocol parsing
├── led_renderer.h/cpp # LED visualization functions
├── web_server.h/cpp  # HTTP, WebSocket, OTA, NVS config
└── main.cpp          # Main entry point (minimal coordinator)
```

### Core Modules

**config.h** - Central configuration:
- Feature flags (`ENABLE_OTA`, `ENABLE_WEBSOCKET`, `ENABLE_SERIAL_CAN_BRIDGE`, etc.)
- Hardware pins (CAN, LED)
- CAN protocol selection (`CAN_PROTOCOL` = 0, 1, or 2)
- CAN message IDs for all protocols

**types.h** - Data structures:
- `VehicleState` - all vehicle telemetry (RPM, throttle, coolant, oil, gear, etc.)
- `TripStatistics` - tracking max values, averages
- `UserConfig` - user preferences (redline, brightness, night mode)
- `LookupTables` - pre-computed values for performance

**can_handler.cpp** - CAN communication:
- `configureCan()` - initialize TWAI driver with filtering
- `processFrame()` - route to protocol-specific handler
- `processFrameCustom()` - Custom protocol parser
- `processFrameLinkGeneric()` - Link Generic Dashboard parser
- `processFrameLinkGeneric2()` - Link Generic Dashboard 2 parser
- `processSerialCanBridge()` - parse CAN frames from Serial port

**led_renderer.cpp** - LED visualization:
- Layered rendering with `blend()` for compositing
- `drawThrottleBar()`, `drawRpmGradient()`, `drawCoolantIndicator()`
- `applyPedalOverlays()` - brake, handbrake, clutch
- `drawShiftLight()`, `drawRevLimiter()`, `drawPanicError()`

**web_server.cpp** - Network services:
- HTTP server with REST API
- WebSocket for real-time updates (10 Hz)
- OTA updates with LED progress visualization
- NVS configuration persistence

### Main Loop Flow

1. Reset watchdog timer
2. Handle OTA updates
3. Process WebSocket events
4. Receive CAN messages (or Serial CAN Bridge)
5. Monitor CAN health
6. Update trip statistics
7. Render LED layers (6 layers)
8. Update FastLED display
9. Handle HTTP requests

## Configuration

### Switching CAN Protocol

In `src/config.h`:
```cpp
#define CAN_PROTOCOL 1  // 0=Custom, 1=Link Generic, 2=Link Generic 2
```

### Feature Flags

```cpp
#define ENABLE_DEBUG_SERIAL true
#define ENABLE_OTA true
#define ENABLE_WATCHDOG true
#define ENABLE_SHIFT_LIGHT true
#define ENABLE_WEBSOCKET true
#define ENABLE_SERIAL_CAN_BRIDGE true  // Testing without CAN hardware
```

### Link ECU CAN IDs

**Generic Dashboard (Protocol 1):**
- `0x5F0` - RPM & TPS
- `0x5F3` - Coolant & Air temp
- `0x5F4` - Battery & Flags
- `0x5F5` - Gear & Oil pressure
- `0x5F6` - Vehicle speed

**Generic Dashboard 2 (Protocol 2):**
- `0x2000` - RPM, TPS, ECT, IAT
- `0x2001` - MAP, Battery, Fuel/Oil pressure
- `0x2004` - Speed, Gear, Flags

## Web Interface

Connect to `CANLED_AP` WiFi (password: `canled123`):

- `http://192.168.4.1/` - Dashboard
- `GET /api/state` - Current telemetry (JSON)
- `GET /api/stats` - Trip statistics
- `POST /api/stats/reset` - Reset trip stats
- `GET/POST /api/config` - Configuration
- `GET /api/export/csv` - Export data
- `WebSocket ws://192.168.4.1:81` - Real-time stream (10 Hz)

## Testing with Serial CAN Bridge

For testing without CAN hardware, enable `ENABLE_SERIAL_CAN_BRIDGE` and use the GUI emulator:

```bash
python tools/serial_can_emulator.py
```

**GUI Emulator Features:**
- COM port selection with auto-refresh
- Protocol templates: Link Generic Dashboard, Link Generic Dashboard 2, Custom
- Message Queue with Add/Delete/Up/Down/Toggle controls
- Message Editor: edit ID, Name, DLC, Data bytes (B0-B7) in hex
- Real-time preview of serial format
- Real-time Log with timestamps and color-coded output
- Single send or cyclic transmission with configurable interval

**Serial Protocol Format:**
```
CAN:5F0:8:E803000064000000
```
Format: `CAN:ID:DLC:HEXDATA`

## Visual Logic

LED strip shows multiple overlapping states:

**Base layer:** Throttle bar (green) or ignition standby (amber breathing)

**Overlays:**
- RPM gradient: Blue → Yellow, red pulsing above redline
- Coolant indicator: Last LED (blue 60°C → green 85°C → red 110°C)
- Brake: Red overlay
- Shift light: Blue flashing at shift point

**Special effects:**
- Rev limiter: Yellow pulsing
- Engine warming: Blue breathing (<60°C)
- Low oil panic: Red/white strobe

## Hardware

- **MCU:** ESP32 (TTGO T-CAN48)
- **CAN:** 1 Mbps, GPIO21/GPIO22
- **LED:** WS2812/Neopixel, GPIO4, 60 LEDs

## Typical Modifications

**Change CAN protocol:** Set `CAN_PROTOCOL` in config.h

**Add new Link ECU parameter:** Add field to `VehicleState`, parse in `processFrameLinkGeneric()`

**Change redline:** Modify `userConfig.rpmRedline` or use web API

**Add LED effect:** Create new draw function in led_renderer.cpp, call from main loop

## Tools

- `tools/serial_can_emulator.py` - GUI emulator for testing via Serial (no CAN hardware needed)
- `tools/can_emulator/` - Alternative CAN adapter emulator

## Documentation

- `docs/LINK_ECU_INTEGRATION.md` - Link ECU setup guide
- `docs/CAN_PROTOCOL.md` - CAN frame specifications
