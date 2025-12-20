# Refactoring Summary

## Overview

The codebase has been refactored from a monolithic 1600+ line `main.cpp` file into a modular architecture with separate files for different functional areas. This improves code readability, maintainability, and makes future development easier.

## New File Structure

```
src/
├── config.h              # All configuration constants and feature flags
├── types.h               # Data structures (VehicleState, TripStatistics, UserConfig, etc.)
├── can_handler.h/cpp     # CAN bus communication and message processing
├── led_renderer.h/cpp    # LED visualization and rendering logic
├── web_server.h/cpp      # HTTP server, WebSocket, OTA updates, and configuration
└── main.cpp              # Main entry point and coordination (140 lines)
```

## Module Descriptions

### config.h
**Purpose**: Central configuration file
**Contains**:
- Feature flags (ENABLE_OTA, ENABLE_WEBSOCKET, ENABLE_DEMO_MODE, etc.)
- Hardware pin definitions (CAN_TX_PIN, CAN_RX_PIN, LED_PIN, LED_COUNT)
- Network configuration (WIFI_SSID, WIFI_PASSWORD)
- CAN message IDs (ID_THROTTLE, ID_PEDALS, ID_RPM, etc.)
- Timing constants (CAN_TIMEOUT_MS, WS_BROADCAST_INTERVAL, etc.)

### types.h
**Purpose**: Data structure definitions
**Contains**:
- `VehicleState` - Current vehicle telemetry (rpm, throttle, coolant, etc.)
- `TripStatistics` - Trip tracking (max RPM, avg RPM, rev limiter hits, etc.)
- `UserConfig` - User preferences (redline RPM, brightness, night mode, etc.)
- `LookupTables` - Pre-computed values for performance optimization
- `CanStatus` - CAN bus status enumeration

### can_handler.h/cpp
**Purpose**: CAN bus communication
**Key Functions**:
- `configureCan()` - Initialize TWAI driver with filtering
- `processFrame()` - Parse CAN messages and update VehicleState
- `receiveAndProcessCan()` - Batch process up to 5 CAN messages per loop
- `monitorCanHealth()` - Check for bus-off and error-passive conditions
- `attemptCanRecovery()` - Automatic recovery from CAN errors
- `simulateDemoData()` - Demo mode for testing without CAN hardware

**Features**:
- Hardware CAN filtering to reduce CPU load
- Frame logging for web interface
- Validation and error handling
- Demo mode simulation

### led_renderer.h/cpp
**Purpose**: LED strip visualization
**Key Functions**:
- `setupLeds()` - Initialize FastLED with self-test
- `drawThrottleBar()` - Green bar showing throttle position
- `drawRpmGradient()` - Blue-to-yellow gradient for RPM
- `drawCoolantIndicator()` - Temperature indicator on last LED
- `applyPedalOverlays()` - Brake (red), handbrake (purple), clutch (cyan)
- `drawRevLimiter()` - Yellow pulsing at redline
- `drawShiftLight()` - Blue flashing LEDs at shift point
- `drawWarmingOverlay()` - Blue breathing when engine cold
- `drawPanicError()` - Red/white strobe for low oil pressure
- `updateAdaptiveBrightness()` - Time-based night mode

**Rendering Architecture**:
Layered rendering system that composites multiple effects:
1. Base layer (throttle bar or standby)
2. RPM gradient overlay
3. Coolant temperature
4. Pedal overlays (brake, handbrake, clutch)
5. Special effects (rev limiter, ALS, shift light)
6. Critical errors (panic, CAN errors)

### web_server.h/cpp
**Purpose**: Network services
**Key Functions**:
- `ensureAccessPoint()` - Start WiFi AP
- `setupServer()` - Initialize HTTP and WebSocket servers
- `setupOTA()` - Configure Over-The-Air updates with LED progress
- `loadConfig()` / `saveConfig()` - NVS persistent storage
- `handleRoot()` - HTML dashboard
- `handleApiState()` - JSON API for current state
- `handleApiStats()` - Trip statistics API
- `handleApiConfig()` - Configuration GET/POST
- `handleApiExportCsv()` - CSV data export
- `broadcastWebSocketData()` - Real-time updates at 10Hz

**API Endpoints**:
- `GET /` - HTML dashboard
- `GET /api/state` - Current vehicle state (JSON)
- `GET /api/stats` - Trip statistics (JSON)
- `POST /api/stats/reset` - Reset trip statistics
- `GET /api/config` - Get current configuration
- `POST /api/config` - Update configuration
- `GET /api/export/csv` - Download CSV data
- `WebSocket on port 81` - Real-time telemetry stream

### main.cpp
**Purpose**: Main coordination and system initialization
**Size**: Reduced from 1600+ lines to ~140 lines
**Responsibilities**:
- Global state instantiation
- System initialization (watchdog, NVS, hardware, networking)
- Main loop coordination
- Call appropriate modules at the right time

## Benefits of Refactoring

### Improved Readability
- Each file has a clear, single responsibility
- Functions are logically grouped
- Easy to find specific functionality
- Clear separation of concerns

### Better Maintainability
- Changes to CAN protocol only affect `can_handler.cpp`
- New LED effects only require editing `led_renderer.cpp`
- Web UI changes isolated to `web_server.cpp`
- Configuration changes centralized in `config.h`

### Easier Testing
- Individual modules can be tested in isolation
- Mock implementations can replace modules for testing
- Clear interfaces between components

### Simplified Navigation
- Jump directly to the file for the subsystem you need
- No scrolling through 1600 lines to find a function
- Clear file names indicate purpose

### Future Development
- Easy to add new features to specific modules
- Clear extension points
- Minimal coupling between modules

## Migration Notes

If you had local modifications to the old `main.cpp`, here's where to find equivalent code:

| Old Location | New Location |
|--------------|--------------|
| Configuration constants | `config.h` |
| CAN message IDs | `config.h` |
| VehicleState struct | `types.h` |
| processFrame() | `can_handler.cpp` |
| drawThrottleBar() | `led_renderer.cpp` |
| handleRoot() | `web_server.cpp` |
| loadConfig() | `web_server.cpp` |
| setup() and loop() | `main.cpp` (simplified) |

## Building

The refactored code builds exactly the same way as before:

```bash
# Build firmware
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor -b 115200
```

No changes to `platformio.ini` are required beyond what was already done.

## File Sizes (approximate)

- `config.h`: ~80 lines
- `types.h`: ~90 lines
- `can_handler.h`: ~45 lines
- `can_handler.cpp`: ~280 lines
- `led_renderer.h`: ~45 lines
- `led_renderer.cpp`: ~260 lines
- `web_server.h`: ~50 lines
- `web_server.cpp`: ~530 lines
- `main.cpp`: ~140 lines

**Total**: ~1520 lines (vs 1600+ in old monolithic file)

The slight reduction comes from eliminating duplicate declarations and better code organization.
