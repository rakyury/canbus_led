# CAN LED Emulator Tools

This folder contains GUI emulator tools for testing the ESP32 CAN LED Controller without physical hardware.

## Quick Start

### Installation

```bash
cd tools
pip install -r requirements.txt
```

### Running Emulators

```bash
# Serial CAN Emulator - send CAN messages to firmware
python serial_can_emulator.py

# LED Strip Emulator - visualize LED output + send CAN messages
python led_strip_emulator.py
```

## Tools Overview

### 1. Serial CAN Emulator (`serial_can_emulator.py`)

GUI application to send simulated CAN frames over Serial port.

**Features:**
- COM port selection with auto-refresh
- Protocol templates: Link Generic Dashboard, Link Generic Dashboard 2, Custom
- Message Queue with Add/Delete/Up/Down/Toggle controls
- Message Editor: edit ID, Name, DLC, Data bytes (B0-B7) in hex
- Real-time preview of serial format
- Real-time Log with timestamps and color-coded output
- Single send or cyclic transmission with configurable interval

**Use case:** Testing CAN message parsing without CAN hardware.

### 2. LED Strip Emulator (`led_strip_emulator.py`)

GUI application that visualizes LED strip output from the ESP32 firmware.

**Features:**
- Real-time 60-LED strip visualization with color display
- Bidirectional serial communication (send CAN, receive LED data)
- Vehicle simulation sliders: RPM, Throttle, Brake, Coolant, Oil Pressure
- Preset buttons: Idle, Cruise, Redline, Cold Start, Overheat, Low Oil
- Communication log with TX/RX color coding
- ~30 FPS LED updates

**Use case:** Testing LED visualization effects without physical LED strip.

## Firmware Configuration

To use these emulators, enable the following in `src/config.h`:

```cpp
#define ENABLE_SERIAL_CAN_BRIDGE true  // For receiving CAN from serial
#define ENABLE_LED_STREAM true          // For streaming LED colors
```

## Serial Protocols

### CAN Message Format (PC -> ESP32)
```
CAN:ID:DLC:HEXDATA\n
```
Example: `CAN:5F0:8:E803000064000000\n`

### LED Stream Format (ESP32 -> PC)
```
LED:count:RRGGBBRRGGBB...\n
```
Example: `LED:60:FF0000FF000000FF00...`

## Requirements

- Python 3.7+
- pyserial
- tkinter (included with Python)

## Workflow

1. Flash firmware to ESP32 with `ENABLE_SERIAL_CAN_BRIDGE` and `ENABLE_LED_STREAM` enabled
2. Connect ESP32 via USB
3. Run `led_strip_emulator.py`
4. Select COM port and click Connect
5. Use sliders or presets to simulate vehicle data
6. Watch LED strip visualization update in real-time

## Screenshots

### Serial CAN Emulator
- Protocol template selector
- Message queue with enable/disable
- Hex data editor
- Real-time transmission log

### LED Strip Emulator
- 60-LED visualization (2 rows of 30)
- Vehicle simulation controls
- Preset scenarios
- Communication log with TX/RX
