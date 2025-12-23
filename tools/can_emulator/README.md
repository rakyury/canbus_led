# CAN Bus Message Emulator

Desktop application to test the ESP32 CAN LED controller without real vehicle/ECU data.

## Features

- **Multi-protocol support**: Custom Protocol, Link ECU Generic Dashboard, Link ECU Generic Dashboard 2
- **Configurable messages**: Enable/disable individual messages, adjust all field values
- **Cyclic transmission**: Set individual intervals per message (10-5000ms)
- **Sequence control**: Reorder message transmission priority via drag/drop
- **Presets**: Quick configurations for common scenarios (idle, cruise, acceleration, etc.)
- **Logging**: Real-time log of all transmitted messages with export capability
- **Configuration save/load**: Save and restore emulator settings as JSON

## Requirements

- Python 3.8+
- USB-CAN adapter (SLCAN, SocketCAN, PCAN, Kvaser, or Vector)

## Installation

```bash
cd tools/can_emulator
pip install -r requirements.txt
```

## Usage

```bash
python can_emulator.py
```

### Connection Setup

1. Select your CAN interface type:
   - **slcan**: For USB-CAN adapters like CANable, USBtin, UCAN
   - **socketcan**: For Linux SocketCAN (e.g., can0)
   - **pcan**: For PEAK-System PCAN adapters
   - **kvaser**: For Kvaser adapters
   - **vector**: For Vector adapters
   - **virtual**: For testing without hardware

2. Enter the channel/port:
   - Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`, or `can0`
   - Windows: `COM3`, `PCAN_USBBUS1`, etc.
   - macOS: `/dev/cu.usbmodem*`, `/dev/cu.wchusbserial*`

3. Set bitrate to **1000000** (1 Mbps) to match the ESP32 firmware

4. Click **Connect**

### Sending Messages

1. Select the protocol matching your firmware build (check `CAN_PROTOCOL` in `config.h`)
2. Configure message values using sliders and spinboxes
3. Enable/disable specific messages with checkboxes
4. Set individual transmission intervals per message
5. Click **Start Sending** for cyclic transmission or **Send Once** for a single burst

### Message Sequence

The sequence panel shows the order in which messages are transmitted. Use **Move Up/Down** buttons to prioritize certain messages. Higher priority messages (top of list) are sent first in each cycle.

### Presets

Quick presets to simulate common vehicle states:

- **Idle Engine**: 800 RPM, no throttle, warm engine
- **Cruising**: 3000 RPM, 25% throttle, 80 km/h
- **Hard Acceleration**: 5500 RPM, 100% throttle
- **Rev Limiter**: 6500 RPM with rev limiter flag active
- **Cold Start**: Low coolant temp (20°C) for warming animation
- **Oil Pressure Warning**: Low oil pressure to trigger panic mode

## Supported CAN Adapters

### Recommended Budget Options

- **CANable** (SLCAN): ~$25, open-source USB-CAN adapter
- **USBtin** (SLCAN): Similar to CANable
- **MCP2515 + USB-Serial**: DIY option

### Professional Options

- **PEAK-System PCAN-USB**: Reliable, well-supported
- **Kvaser Leaf Light**: Industrial quality
- **Vector CANcaseXL**: Professional grade

### Virtual Testing

Use the `virtual` interface to test the emulator without hardware. Messages are logged but not transmitted.

## Protocol Reference

### Custom Protocol (CAN_PROTOCOL = 0)

| ID    | Name         | Fields                           |
|-------|--------------|----------------------------------|
| 0x100 | Throttle     | throttle (0-100%)                |
| 0x101 | Pedals       | brake, handbrake, clutch (0-100%)|
| 0x102 | RPM          | rpm (0-10000)                    |
| 0x103 | Coolant      | coolant (0-150°C)                |
| 0x104 | Oil Pressure | oil_pressure (0-10 bar)          |
| 0x105 | Flags        | rev_limiter, als_active          |
| 0x106 | Ignition     | ignition (on/off)                |

### Link ECU Generic Dashboard (CAN_PROTOCOL = 1)

| ID    | Name              | Fields                              |
|-------|-------------------|-------------------------------------|
| 0x5F0 | RPM & TPS         | rpm, tps                            |
| 0x5F1 | Fuel & Ignition   | fuel_pressure, ign_timing           |
| 0x5F2 | Pressures         | map, baro, lambda                   |
| 0x5F3 | Temperatures      | coolant, air_temp                   |
| 0x5F4 | Voltage & Flags   | battery, rev_limiter, launch, etc.  |
| 0x5F5 | Gear & Oil        | gear, oil_pressure                  |
| 0x5F6 | Vehicle Speed     | speed                               |
| 0x5F7 | Throttle Sensors  | tps1, tps2                          |

### Link ECU Generic Dashboard 2 (CAN_PROTOCOL = 2)

| ID     | Name            | Fields                              |
|--------|-----------------|-------------------------------------|
| 0x2000 | Engine Data 1   | rpm, tps, coolant, air_temp         |
| 0x2001 | Engine Data 2   | map, battery, fuel_pressure, oil    |
| 0x2002 | Engine Data 3   | lambda, ign_timing, fuel_level      |
| 0x2003 | Engine Data 4   | boost_duty, idle_duty               |
| 0x2004 | Vehicle Data 1  | speed, gear, launch, flat_shift     |
| 0x2005 | Vehicle Data 2  | wheel speeds (FL, FR, RL, RR)       |
| 0x2006 | Flags & Warnings| rev_limiter, ignition, protection   |
| 0x2007 | Analog Inputs   | an1-an4 (0-5V)                      |

## Troubleshooting

### "python-can not installed"
```bash
pip install python-can
```

### "Permission denied" (Linux)
```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

### No messages received on device
1. Check CAN wiring (CANH, CANL)
2. Verify 120Ω termination resistors
3. Confirm bitrate matches (1 Mbps)
4. Check protocol selection matches firmware `CAN_PROTOCOL`

### SLCAN adapter not responding
```bash
# Reset adapter
stty -F /dev/ttyUSB0 raw
echo -e "C\r" > /dev/ttyUSB0  # Close channel
echo -e "S8\r" > /dev/ttyUSB0  # Set 1Mbps
echo -e "O\r" > /dev/ttyUSB0  # Open channel
```
