"""
CAN Protocol definitions for CANBUS LED Controller.
Supports Custom Protocol, Link ECU Generic Dashboard, and Link ECU Generic Dashboard 2.
"""

from dataclasses import dataclass, field
from typing import List, Callable, Dict, Any, Optional
import struct


@dataclass
class MessageField:
    """Represents a single field within a CAN message."""
    name: str
    description: str
    min_value: float
    max_value: float
    default_value: float
    unit: str
    byte_offset: int
    byte_length: int
    scale: float = 1.0  # Value is multiplied by this before encoding
    signed: bool = False
    bit_position: Optional[int] = None  # For bit flags: which bit within the byte(s)

    def encode(self, value: float) -> bytes:
        """Encode a value to bytes."""
        scaled = int(value * self.scale)
        if self.signed:
            if self.byte_length == 1:
                return struct.pack('<b', max(-128, min(127, scaled)))
            elif self.byte_length == 2:
                return struct.pack('<h', max(-32768, min(32767, scaled)))
            elif self.byte_length == 4:
                return struct.pack('<i', scaled)
        else:
            if self.byte_length == 1:
                return struct.pack('<B', max(0, min(255, scaled)))
            elif self.byte_length == 2:
                return struct.pack('<H', max(0, min(65535, scaled)))
            elif self.byte_length == 4:
                return struct.pack('<I', max(0, min(0xFFFFFFFF, scaled)))
        return b'\x00' * self.byte_length

    def is_bit_flag(self) -> bool:
        """Check if this field is a single bit flag."""
        return self.bit_position is not None


@dataclass
class CANMessage:
    """Represents a CAN message with its fields."""
    id: int
    name: str
    description: str
    dlc: int  # Data Length Code (1-8 bytes)
    fields: List[MessageField]
    enabled: bool = True
    interval_ms: int = 100  # Default sending interval

    def build_data(self, values: Dict[str, float]) -> bytes:
        """Build the CAN data payload from field values."""
        data = bytearray(self.dlc)

        # First pass: handle non-bit fields
        for f in self.fields:
            if f.is_bit_flag():
                continue
            val = values.get(f.name, f.default_value)
            encoded = f.encode(val)
            for i, b in enumerate(encoded):
                if f.byte_offset + i < self.dlc:
                    data[f.byte_offset + i] = b

        # Second pass: handle bit fields (OR them together)
        for f in self.fields:
            if not f.is_bit_flag():
                continue
            val = values.get(f.name, f.default_value)
            if val:
                if f.byte_offset < self.dlc:
                    data[f.byte_offset] |= (1 << f.bit_position)

        return bytes(data)


# ============== Custom Protocol (CAN_PROTOCOL = 0) ==============
CUSTOM_PROTOCOL_MESSAGES = [
    CANMessage(
        id=0x100,
        name="Throttle",
        description="Throttle pedal position",
        dlc=1,
        fields=[
            MessageField("throttle", "Throttle %", 0, 100, 0, "%", 0, 1),
        ]
    ),
    CANMessage(
        id=0x101,
        name="Pedals",
        description="Brake, Handbrake, Clutch positions",
        dlc=3,
        fields=[
            MessageField("brake", "Brake %", 0, 100, 0, "%", 0, 1),
            MessageField("handbrake", "Handbrake %", 0, 100, 0, "%", 1, 1),
            MessageField("clutch", "Clutch %", 0, 100, 0, "%", 2, 1),
        ]
    ),
    CANMessage(
        id=0x102,
        name="RPM",
        description="Engine RPM",
        dlc=2,
        fields=[
            MessageField("rpm", "Engine RPM", 0, 10000, 800, "rpm", 0, 2),
        ]
    ),
    CANMessage(
        id=0x103,
        name="Coolant",
        description="Coolant temperature",
        dlc=2,
        fields=[
            MessageField("coolant", "Coolant Temp", 0, 150, 85, "C", 0, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x104,
        name="Oil Pressure",
        description="Oil pressure",
        dlc=2,
        fields=[
            MessageField("oil_pressure", "Oil Pressure", 0, 10, 4.0, "bar", 0, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x105,
        name="Flags",
        description="Rev limiter and ALS flags",
        dlc=1,
        fields=[
            MessageField("rev_limiter", "Rev Limiter Active", 0, 1, 0, "", 0, 1, bit_position=0),
            MessageField("als_active", "ALS Active", 0, 1, 0, "", 0, 1, bit_position=1),
        ]
    ),
    CANMessage(
        id=0x106,
        name="Ignition",
        description="Ignition state",
        dlc=1,
        fields=[
            MessageField("ignition", "Ignition On", 0, 1, 1, "", 0, 1),
        ]
    ),
]


# ============== Link ECU Generic Dashboard (CAN_PROTOCOL = 1) ==============
LINK_GENERIC_MESSAGES = [
    CANMessage(
        id=0x5F0,
        name="RPM & TPS",
        description="Engine RPM and Throttle Position",
        dlc=6,
        fields=[
            MessageField("rpm", "Engine RPM", 0, 15000, 800, "rpm", 0, 4),
            MessageField("tps", "Throttle Position", 0, 100, 0, "%", 4, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x5F1,
        name="Fuel & Ignition",
        description="Fuel pressure and Ignition timing",
        dlc=4,
        fields=[
            MessageField("fuel_pressure", "Fuel Pressure", 0, 10, 3.0, "bar", 0, 2, scale=10),
            MessageField("ign_timing", "Ignition Timing", -20, 60, 15, "deg", 2, 2, scale=10, signed=True),
        ]
    ),
    CANMessage(
        id=0x5F2,
        name="Pressures",
        description="MAP, Baro, Lambda",
        dlc=6,
        fields=[
            MessageField("map", "MAP", 0, 300, 100, "kPa", 0, 2, scale=10),
            MessageField("baro", "Barometric", 90, 110, 101, "kPa", 2, 2, scale=10),
            MessageField("lambda", "Lambda", 0.5, 1.5, 1.0, "", 4, 2, scale=100),
        ]
    ),
    CANMessage(
        id=0x5F3,
        name="Temperatures",
        description="Coolant and Air temperatures",
        dlc=4,
        fields=[
            MessageField("coolant", "Coolant Temp", -40, 150, 85, "C", 0, 2, scale=10),
            MessageField("air_temp", "Air Temp", -40, 80, 25, "C", 2, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x5F4,
        name="Voltage & Flags",
        description="Battery voltage and status flags",
        dlc=4,
        fields=[
            MessageField("battery", "Battery Voltage", 8, 18, 14.0, "V", 0, 2, scale=100),
            MessageField("rev_limiter", "Rev Limiter", 0, 1, 0, "", 2, 1, bit_position=0),
            MessageField("launch_control", "Launch Control", 0, 1, 0, "", 2, 1, bit_position=1),
            MessageField("flat_shift", "Flat Shift", 0, 1, 0, "", 2, 1, bit_position=2),
            MessageField("ignition", "Ignition On", 0, 1, 1, "", 2, 1, bit_position=7),
        ]
    ),
    CANMessage(
        id=0x5F5,
        name="Gear & Oil",
        description="Gear position and Oil pressure",
        dlc=4,
        fields=[
            MessageField("gear", "Gear", 0, 8, 0, "", 0, 1),
            MessageField("oil_pressure", "Oil Pressure", 0, 10, 4.0, "bar", 2, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x5F6,
        name="Vehicle Speed",
        description="Vehicle speed",
        dlc=2,
        fields=[
            MessageField("speed", "Vehicle Speed", 0, 300, 0, "km/h", 0, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x5F7,
        name="Throttle Sensors",
        description="Throttle position sensors",
        dlc=4,
        fields=[
            MessageField("tps1", "TPS 1", 0, 100, 0, "%", 0, 2, scale=10),
            MessageField("tps2", "TPS 2", 0, 100, 0, "%", 2, 2, scale=10),
        ]
    ),
]


# ============== Link ECU Generic Dashboard 2 (CAN_PROTOCOL = 2) ==============
LINK_GENERIC2_MESSAGES = [
    CANMessage(
        id=0x2000,
        name="Engine Data 1",
        description="RPM, TPS, ECT, IAT",
        dlc=8,
        fields=[
            MessageField("rpm", "Engine RPM", 0, 15000, 800, "rpm", 0, 2),
            MessageField("tps", "Throttle Position", 0, 100, 0, "%", 2, 2, scale=10),
            MessageField("coolant", "Coolant Temp", -40, 150, 85, "C", 4, 2, scale=10),
            MessageField("air_temp", "Air Temp", -40, 80, 25, "C", 6, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x2001,
        name="Engine Data 2",
        description="MAP, Battery, Fuel Pressure, Oil Pressure",
        dlc=8,
        fields=[
            MessageField("map", "MAP", 0, 300, 100, "kPa", 0, 2, scale=10),
            MessageField("battery", "Battery Voltage", 8, 18, 14.0, "V", 2, 2, scale=100),
            MessageField("fuel_pressure", "Fuel Pressure", 0, 10, 3.0, "bar", 4, 2, scale=10),
            MessageField("oil_pressure", "Oil Pressure", 0, 10, 4.0, "bar", 6, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x2002,
        name="Engine Data 3",
        description="Lambda, Ignition timing, Fuel level",
        dlc=6,
        fields=[
            MessageField("lambda", "Lambda", 0.5, 1.5, 1.0, "", 0, 2, scale=100),
            MessageField("ign_timing", "Ignition Timing", -20, 60, 15, "deg", 2, 2, scale=10, signed=True),
            MessageField("fuel_level", "Fuel Level", 0, 100, 50, "%", 4, 2),
        ]
    ),
    CANMessage(
        id=0x2003,
        name="Engine Data 4",
        description="Boost control, Idle control",
        dlc=4,
        fields=[
            MessageField("boost_duty", "Boost Duty", 0, 100, 0, "%", 0, 2, scale=10),
            MessageField("idle_duty", "Idle Valve", 0, 100, 30, "%", 2, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x2004,
        name="Vehicle Data 1",
        description="Speed, Gear, Launch/Flat shift status",
        dlc=4,
        fields=[
            MessageField("speed", "Vehicle Speed", 0, 300, 0, "km/h", 0, 2, scale=10),
            MessageField("gear", "Gear", 0, 8, 0, "", 2, 1),
            MessageField("launch_control", "Launch Control", 0, 1, 0, "", 3, 1, bit_position=0),
            MessageField("flat_shift", "Flat Shift", 0, 1, 0, "", 3, 1, bit_position=1),
        ]
    ),
    CANMessage(
        id=0x2005,
        name="Vehicle Data 2",
        description="Wheel speeds",
        dlc=8,
        fields=[
            MessageField("wheel_fl", "Wheel FL", 0, 300, 0, "km/h", 0, 2, scale=10),
            MessageField("wheel_fr", "Wheel FR", 0, 300, 0, "km/h", 2, 2, scale=10),
            MessageField("wheel_rl", "Wheel RL", 0, 300, 0, "km/h", 4, 2, scale=10),
            MessageField("wheel_rr", "Wheel RR", 0, 300, 0, "km/h", 6, 2, scale=10),
        ]
    ),
    CANMessage(
        id=0x2006,
        name="Flags & Warnings",
        description="Engine protection flags, warnings",
        dlc=2,
        fields=[
            MessageField("rev_limiter", "Rev Limiter", 0, 1, 0, "", 0, 1, bit_position=0),
            MessageField("ignition", "Ignition On", 0, 1, 1, "", 0, 1, bit_position=7),
            MessageField("engine_protection", "Engine Protection", 0, 1, 0, "", 1, 1),
        ]
    ),
    CANMessage(
        id=0x2007,
        name="Analog Inputs",
        description="User-configurable analog inputs",
        dlc=8,
        fields=[
            MessageField("an1", "Analog 1", 0, 5, 0, "V", 0, 2, scale=1000),
            MessageField("an2", "Analog 2", 0, 5, 0, "V", 2, 2, scale=1000),
            MessageField("an3", "Analog 3", 0, 5, 0, "V", 4, 2, scale=1000),
            MessageField("an4", "Analog 4", 0, 5, 0, "V", 6, 2, scale=1000),
        ]
    ),
]


# Protocol registry
PROTOCOLS = {
    "Custom Protocol": CUSTOM_PROTOCOL_MESSAGES,
    "Link ECU Generic Dashboard": LINK_GENERIC_MESSAGES,
    "Link ECU Generic Dashboard 2": LINK_GENERIC2_MESSAGES,
}


def get_protocol_messages(protocol_name: str) -> List[CANMessage]:
    """Get messages for a specific protocol."""
    return PROTOCOLS.get(protocol_name, [])


def get_protocol_names() -> List[str]:
    """Get list of available protocol names."""
    return list(PROTOCOLS.keys())
