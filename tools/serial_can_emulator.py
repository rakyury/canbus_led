#!/usr/bin/env python3
"""
Serial CAN Emulator for ESP32 CAN LED Controller
Sends simulated CAN frames over Serial port for testing without CAN hardware.

Usage:
    python serial_can_emulator.py COM3
    python serial_can_emulator.py /dev/ttyUSB0
"""

import serial
import struct
import time
import sys
import threading
import math

class SerialCanEmulator:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.running = False
        self.protocol = 1  # 0=Custom, 1=Link Generic, 2=Link Generic 2

        # Vehicle state
        self.rpm = 800
        self.throttle = 0
        self.coolant = 85.0
        self.oil_pressure = 4.5
        self.air_temp = 25.0
        self.battery = 14.0
        self.gear = 0
        self.speed = 0
        self.brake = 0
        self.ignition = True
        self.rev_limiter = False
        self.launch_control = False

    def connect(self):
        """Connect to serial port"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Error: {e}")
            return False

    def disconnect(self):
        """Disconnect from serial port"""
        if self.ser:
            self.ser.close()
            self.ser = None

    def send_can_frame(self, can_id, data):
        """Send CAN frame over serial
        Format: CAN:XXX:D:HHHHHHHHHHHHHHHH
        """
        dlc = len(data)
        hex_data = ''.join(f'{b:02X}' for b in data)
        cmd = f"CAN:{can_id:03X}:{dlc}:{hex_data}\n"

        if self.ser:
            self.ser.write(cmd.encode())
            # Read response
            time.sleep(0.01)
            while self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line and not line.startswith('OK:'):
                    print(f"  <- {line}")

    def send_link_generic_dashboard(self):
        """Send Link ECU Generic Dashboard frames (Protocol 1)"""
        # 0x5F0: RPM & TPS
        rpm_bytes = struct.pack('<I', int(self.rpm))  # 32-bit RPM
        tps_bytes = struct.pack('<H', int(self.throttle * 10))  # 16-bit TPS * 10
        self.send_can_frame(0x5F0, rpm_bytes + tps_bytes + bytes(2))

        # 0x5F3: Temperatures (Coolant, Air temp)
        coolant_bytes = struct.pack('<H', int(self.coolant * 10))
        air_bytes = struct.pack('<H', int(self.air_temp * 10))
        self.send_can_frame(0x5F3, coolant_bytes + air_bytes + bytes(4))

        # 0x5F4: Voltage & Flags
        voltage_bytes = struct.pack('<H', int(self.battery * 100))
        flags = 0
        if self.rev_limiter:
            flags |= 0x01
        if self.launch_control:
            flags |= 0x02
        if self.ignition:
            flags |= 0x80
        self.send_can_frame(0x5F4, voltage_bytes + bytes([flags]) + bytes(5))

        # 0x5F5: Gear & Oil pressure
        oil_kpa = int(self.oil_pressure * 100)  # bar to kPa * 10
        oil_bytes = struct.pack('<H', oil_kpa)
        self.send_can_frame(0x5F5, bytes([self.gear, 0]) + oil_bytes + bytes(4))

        # 0x5F6: Vehicle speed
        speed_bytes = struct.pack('<H', int(self.speed * 10))
        self.send_can_frame(0x5F6, speed_bytes + bytes(6))

    def send_link_generic_dashboard2(self):
        """Send Link ECU Generic Dashboard 2 frames (Protocol 2)"""
        # 0x2000: Engine Data 1 (RPM, TPS, ECT, IAT)
        data = struct.pack('<HHHH',
            int(self.rpm),
            int(self.throttle * 10),
            int(self.coolant * 10),
            int(self.air_temp * 10))
        self.send_can_frame(0x2000, data)

        # 0x2001: Engine Data 2 (MAP, Battery, Fuel P, Oil P)
        data = struct.pack('<HHHH',
            1000,  # MAP (dummy)
            int(self.battery * 100),
            300,   # Fuel pressure (dummy)
            int(self.oil_pressure * 10))
        self.send_can_frame(0x2001, data)

        # 0x2004: Vehicle Data 1 (Speed, Gear, Flags)
        flags = 0
        if self.launch_control:
            flags |= 0x01
        data = struct.pack('<HBB', int(self.speed * 10), self.gear, flags) + bytes(4)
        self.send_can_frame(0x2004, data)

        # 0x2006: Flags & Warnings
        flags = 0
        if self.rev_limiter:
            flags |= 0x01
        if self.ignition:
            flags |= 0x80
        self.send_can_frame(0x2006, bytes([flags]) + bytes(7))

    def send_custom_protocol(self):
        """Send Custom protocol frames (Protocol 0)"""
        # 0x100: Throttle
        self.send_can_frame(0x100, bytes([int(self.throttle)]))

        # 0x101: Pedals (brake, handbrake, clutch)
        self.send_can_frame(0x101, bytes([int(self.brake), 0, 0]))

        # 0x102: RPM
        rpm_bytes = struct.pack('<H', int(self.rpm))
        self.send_can_frame(0x102, rpm_bytes)

        # 0x103: Coolant
        coolant_bytes = struct.pack('<H', int(self.coolant * 10))
        self.send_can_frame(0x103, coolant_bytes)

        # 0x104: Oil pressure
        oil_bytes = struct.pack('<H', int(self.oil_pressure * 10))
        self.send_can_frame(0x104, oil_bytes)

        # 0x105: Flags
        flags = 0
        if self.rev_limiter:
            flags |= 0x01
        self.send_can_frame(0x105, bytes([flags]))

        # 0x106: Ignition
        self.send_can_frame(0x106, bytes([1 if self.ignition else 0]))

    def send_all_frames(self):
        """Send all frames for current protocol"""
        if self.protocol == 0:
            self.send_custom_protocol()
        elif self.protocol == 1:
            self.send_link_generic_dashboard()
        else:
            self.send_link_generic_dashboard2()

    def simulate_acceleration(self, duration=5.0):
        """Simulate acceleration cycle"""
        print(f"\nSimulating acceleration for {duration}s...")
        start = time.time()

        while time.time() - start < duration:
            t = (time.time() - start) / duration
            self.throttle = 100 * math.sin(t * math.pi)
            self.rpm = 1000 + 5500 * t
            self.speed = 150 * t
            self.gear = min(6, int(t * 7))
            self.rev_limiter = self.rpm > 6400

            self.send_all_frames()
            time.sleep(0.05)

        self.throttle = 0
        self.rpm = 800
        self.speed = 0
        self.gear = 0
        self.rev_limiter = False
        print("Acceleration complete")

    def simulate_idle(self):
        """Set idle state"""
        self.rpm = 800
        self.throttle = 0
        self.speed = 0
        self.gear = 0
        self.brake = 0
        self.coolant = 85.0
        self.oil_pressure = 4.5
        self.ignition = True
        self.rev_limiter = False

    def simulate_redline(self):
        """Simulate hitting redline"""
        self.rpm = 6500
        self.throttle = 100
        self.rev_limiter = True

    def simulate_cold_start(self):
        """Simulate cold engine start"""
        self.rpm = 1200
        self.throttle = 0
        self.coolant = 20.0
        self.oil_pressure = 5.0
        self.ignition = True

    def simulate_low_oil(self):
        """Simulate low oil pressure warning"""
        self.rpm = 4000
        self.throttle = 50
        self.oil_pressure = 1.5

    def cyclic_send(self, interval=0.1):
        """Send frames cyclically"""
        self.running = True
        print(f"Starting cyclic transmission at {1/interval:.0f} Hz")
        print("Press Ctrl+C to stop")

        try:
            while self.running:
                self.send_all_frames()
                time.sleep(interval)
        except KeyboardInterrupt:
            self.running = False
            print("\nStopped")

    def interactive_mode(self):
        """Interactive command mode"""
        print("\n=== Serial CAN Emulator ===")
        print(f"Protocol: {['Custom', 'Link Generic Dashboard', 'Link Generic Dashboard 2'][self.protocol]}")
        print("\nCommands:")
        print("  s     - Send all frames once")
        print("  c     - Start cyclic sending (10 Hz)")
        print("  idle  - Set idle state")
        print("  accel - Simulate acceleration")
        print("  rev   - Simulate redline")
        print("  cold  - Simulate cold start")
        print("  oil   - Simulate low oil pressure")
        print("  p0/p1/p2 - Switch protocol")
        print("  rpm N   - Set RPM to N")
        print("  tps N   - Set throttle to N%")
        print("  temp N  - Set coolant to N°C")
        print("  q     - Quit")
        print()

        while True:
            try:
                cmd = input(f"[RPM:{self.rpm:.0f} TPS:{self.throttle:.0f}%] > ").strip().lower()

                if cmd == 'q':
                    break
                elif cmd == 's':
                    self.send_all_frames()
                    print("Sent")
                elif cmd == 'c':
                    self.cyclic_send()
                elif cmd == 'idle':
                    self.simulate_idle()
                    self.send_all_frames()
                    print("Idle state set")
                elif cmd == 'accel':
                    self.simulate_acceleration()
                elif cmd == 'rev':
                    self.simulate_redline()
                    self.send_all_frames()
                    print("Redline!")
                elif cmd == 'cold':
                    self.simulate_cold_start()
                    self.send_all_frames()
                    print("Cold start state set")
                elif cmd == 'oil':
                    self.simulate_low_oil()
                    self.send_all_frames()
                    print("Low oil pressure!")
                elif cmd == 'p0':
                    self.protocol = 0
                    print("Switched to Custom protocol")
                elif cmd == 'p1':
                    self.protocol = 1
                    print("Switched to Link Generic Dashboard")
                elif cmd == 'p2':
                    self.protocol = 2
                    print("Switched to Link Generic Dashboard 2")
                elif cmd.startswith('rpm '):
                    self.rpm = float(cmd.split()[1])
                    self.send_all_frames()
                elif cmd.startswith('tps '):
                    self.throttle = float(cmd.split()[1])
                    self.send_all_frames()
                elif cmd.startswith('temp '):
                    self.coolant = float(cmd.split()[1])
                    self.send_all_frames()
                elif cmd:
                    print("Unknown command")

            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error: {e}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python serial_can_emulator.py <COM_PORT> [protocol]")
        print("  COM_PORT: COM3, /dev/ttyUSB0, etc.")
        print("  protocol: 0=Custom, 1=Link Generic (default), 2=Link Generic 2")
        print("\nExample:")
        print("  python serial_can_emulator.py COM3")
        print("  python serial_can_emulator.py /dev/ttyUSB0 1")
        sys.exit(1)

    port = sys.argv[1]
    protocol = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    emulator = SerialCanEmulator(port)
    emulator.protocol = protocol

    if not emulator.connect():
        sys.exit(1)

    try:
        emulator.interactive_mode()
    finally:
        emulator.disconnect()
        print("Disconnected")


if __name__ == '__main__':
    main()
