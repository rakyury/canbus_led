#!/usr/bin/env python3
"""
Serial CAN Emulator for ESP32 CAN LED Controller
GUI application to send simulated CAN frames over Serial port.
"""

import serial
import serial.tools.list_ports
import struct
import time
import threading
import math
import tkinter as tk
from tkinter import ttk, messagebox

class SerialCanEmulator:
    def __init__(self, root):
        self.ser = None
        self.running = False
        self.cyclic_thread = None

        # Vehicle state (requires tkinter root to be created first)
        self.rpm = tk.IntVar(root, value=800)
        self.throttle = tk.IntVar(root, value=0)
        self.coolant = tk.DoubleVar(root, value=85.0)
        self.oil_pressure = tk.DoubleVar(root, value=4.5)
        self.air_temp = tk.DoubleVar(root, value=25.0)
        self.battery = tk.DoubleVar(root, value=14.0)
        self.gear = tk.IntVar(root, value=0)
        self.speed = tk.IntVar(root, value=0)
        self.brake = tk.IntVar(root, value=0)
        self.ignition = tk.BooleanVar(root, value=True)
        self.rev_limiter = tk.BooleanVar(root, value=False)
        self.launch_control = tk.BooleanVar(root, value=False)

        self.protocol = tk.IntVar(root, value=1)  # 0=Custom, 1=Link Generic, 2=Link Generic 2

    def connect(self, port, baudrate=115200):
        """Connect to serial port"""
        try:
            self.ser = serial.Serial(port, baudrate, timeout=0.1)
            return True, f"Connected to {port}"
        except serial.SerialException as e:
            return False, str(e)

    def disconnect(self):
        """Disconnect from serial port"""
        self.stop_cyclic()
        if self.ser:
            self.ser.close()
            self.ser = None

    def is_connected(self):
        return self.ser is not None and self.ser.is_open

    def send_can_frame(self, can_id, data):
        """Send CAN frame over serial"""
        if not self.is_connected():
            return

        dlc = len(data)
        hex_data = ''.join(f'{b:02X}' for b in data)
        cmd = f"CAN:{can_id:03X}:{dlc}:{hex_data}\n"
        self.ser.write(cmd.encode())

    def send_link_generic_dashboard(self):
        """Send Link ECU Generic Dashboard frames (Protocol 1)"""
        # 0x5F0: RPM & TPS
        rpm_bytes = struct.pack('<I', self.rpm.get())
        tps_bytes = struct.pack('<H', int(self.throttle.get() * 10))
        self.send_can_frame(0x5F0, rpm_bytes + tps_bytes + bytes(2))

        # 0x5F3: Temperatures
        coolant_bytes = struct.pack('<H', int(self.coolant.get() * 10))
        air_bytes = struct.pack('<H', int(self.air_temp.get() * 10))
        self.send_can_frame(0x5F3, coolant_bytes + air_bytes + bytes(4))

        # 0x5F4: Voltage & Flags
        voltage_bytes = struct.pack('<H', int(self.battery.get() * 100))
        flags = 0
        if self.rev_limiter.get(): flags |= 0x01
        if self.launch_control.get(): flags |= 0x02
        if self.ignition.get(): flags |= 0x80
        self.send_can_frame(0x5F4, voltage_bytes + bytes([flags]) + bytes(5))

        # 0x5F5: Gear & Oil pressure
        oil_kpa = int(self.oil_pressure.get() * 100)
        oil_bytes = struct.pack('<H', oil_kpa)
        self.send_can_frame(0x5F5, bytes([self.gear.get(), 0]) + oil_bytes + bytes(4))

        # 0x5F6: Vehicle speed
        speed_bytes = struct.pack('<H', int(self.speed.get() * 10))
        self.send_can_frame(0x5F6, speed_bytes + bytes(6))

    def send_link_generic_dashboard2(self):
        """Send Link ECU Generic Dashboard 2 frames (Protocol 2)"""
        # 0x2000: Engine Data 1
        data = struct.pack('<HHHH',
            self.rpm.get(),
            int(self.throttle.get() * 10),
            int(self.coolant.get() * 10),
            int(self.air_temp.get() * 10))
        self.send_can_frame(0x2000, data)

        # 0x2001: Engine Data 2
        data = struct.pack('<HHHH',
            1000,
            int(self.battery.get() * 100),
            300,
            int(self.oil_pressure.get() * 10))
        self.send_can_frame(0x2001, data)

        # 0x2004: Vehicle Data 1
        flags = 0
        if self.launch_control.get(): flags |= 0x01
        data = struct.pack('<HBB', int(self.speed.get() * 10), self.gear.get(), flags) + bytes(4)
        self.send_can_frame(0x2004, data)

        # 0x2006: Flags
        flags = 0
        if self.rev_limiter.get(): flags |= 0x01
        if self.ignition.get(): flags |= 0x80
        self.send_can_frame(0x2006, bytes([flags]) + bytes(7))

    def send_custom_protocol(self):
        """Send Custom protocol frames (Protocol 0)"""
        self.send_can_frame(0x100, bytes([self.throttle.get()]))
        self.send_can_frame(0x101, bytes([self.brake.get(), 0, 0]))
        self.send_can_frame(0x102, struct.pack('<H', self.rpm.get()))
        self.send_can_frame(0x103, struct.pack('<H', int(self.coolant.get() * 10)))
        self.send_can_frame(0x104, struct.pack('<H', int(self.oil_pressure.get() * 10)))
        flags = 0x01 if self.rev_limiter.get() else 0
        self.send_can_frame(0x105, bytes([flags]))
        self.send_can_frame(0x106, bytes([1 if self.ignition.get() else 0]))

    def send_all_frames(self):
        """Send all frames for current protocol"""
        if not self.is_connected():
            return

        proto = self.protocol.get()
        if proto == 0:
            self.send_custom_protocol()
        elif proto == 1:
            self.send_link_generic_dashboard()
        else:
            self.send_link_generic_dashboard2()

    def start_cyclic(self, interval=0.1):
        """Start cyclic transmission"""
        if self.running:
            return
        self.running = True
        self.cyclic_thread = threading.Thread(target=self._cyclic_loop, args=(interval,), daemon=True)
        self.cyclic_thread.start()

    def stop_cyclic(self):
        """Stop cyclic transmission"""
        self.running = False
        if self.cyclic_thread:
            self.cyclic_thread.join(timeout=1)
            self.cyclic_thread = None

    def _cyclic_loop(self, interval):
        while self.running:
            self.send_all_frames()
            time.sleep(interval)

    def set_preset_idle(self):
        self.rpm.set(800)
        self.throttle.set(0)
        self.speed.set(0)
        self.gear.set(0)
        self.brake.set(0)
        self.coolant.set(85.0)
        self.oil_pressure.set(4.5)
        self.ignition.set(True)
        self.rev_limiter.set(False)

    def set_preset_cruise(self):
        self.rpm.set(3000)
        self.throttle.set(25)
        self.speed.set(80)
        self.gear.set(4)
        self.coolant.set(90.0)
        self.oil_pressure.set(4.0)

    def set_preset_redline(self):
        self.rpm.set(6500)
        self.throttle.set(100)
        self.rev_limiter.set(True)

    def set_preset_cold(self):
        self.rpm.set(1200)
        self.throttle.set(0)
        self.coolant.set(20.0)
        self.oil_pressure.set(5.0)

    def set_preset_low_oil(self):
        self.rpm.set(4000)
        self.throttle.set(50)
        self.oil_pressure.set(1.5)


class EmulatorGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.emulator = SerialCanEmulator(self.root)
        self.root.title("Serial CAN Emulator - Link ECU")
        self.root.geometry("600x700")
        self.root.resizable(True, True)

        self.create_widgets()
        self.refresh_ports()

    def create_widgets(self):
        # Connection frame
        conn_frame = ttk.LabelFrame(self.root, text="Connection", padding=10)
        conn_frame.pack(fill='x', padx=10, pady=5)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, sticky='w')
        self.port_combo = ttk.Combobox(conn_frame, width=30, state='readonly')
        self.port_combo.grid(row=0, column=1, padx=5)

        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=5)

        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=3, padx=5)

        self.status_label = ttk.Label(conn_frame, text="Disconnected", foreground='red')
        self.status_label.grid(row=1, column=0, columnspan=4, sticky='w', pady=5)

        # Protocol frame
        proto_frame = ttk.LabelFrame(self.root, text="Protocol", padding=10)
        proto_frame.pack(fill='x', padx=10, pady=5)

        ttk.Radiobutton(proto_frame, text="Custom (0)", variable=self.emulator.protocol, value=0).pack(side='left', padx=10)
        ttk.Radiobutton(proto_frame, text="Link Generic Dashboard (1)", variable=self.emulator.protocol, value=1).pack(side='left', padx=10)
        ttk.Radiobutton(proto_frame, text="Link Generic Dashboard 2 (2)", variable=self.emulator.protocol, value=2).pack(side='left', padx=10)

        # Engine data frame
        engine_frame = ttk.LabelFrame(self.root, text="Engine Data", padding=10)
        engine_frame.pack(fill='x', padx=10, pady=5)

        # RPM
        ttk.Label(engine_frame, text="RPM:").grid(row=0, column=0, sticky='w')
        self.rpm_scale = ttk.Scale(engine_frame, from_=0, to=8000, variable=self.emulator.rpm, orient='horizontal', length=300)
        self.rpm_scale.grid(row=0, column=1, padx=5)
        self.rpm_label = ttk.Label(engine_frame, text="800")
        self.rpm_label.grid(row=0, column=2)
        self.emulator.rpm.trace_add('write', lambda *_: self.rpm_label.config(text=str(self.emulator.rpm.get())))

        # Throttle
        ttk.Label(engine_frame, text="Throttle %:").grid(row=1, column=0, sticky='w')
        ttk.Scale(engine_frame, from_=0, to=100, variable=self.emulator.throttle, orient='horizontal', length=300).grid(row=1, column=1, padx=5)
        self.tps_label = ttk.Label(engine_frame, text="0")
        self.tps_label.grid(row=1, column=2)
        self.emulator.throttle.trace_add('write', lambda *_: self.tps_label.config(text=str(self.emulator.throttle.get())))

        # Coolant
        ttk.Label(engine_frame, text="Coolant °C:").grid(row=2, column=0, sticky='w')
        ttk.Scale(engine_frame, from_=0, to=130, variable=self.emulator.coolant, orient='horizontal', length=300).grid(row=2, column=1, padx=5)
        self.coolant_label = ttk.Label(engine_frame, text="85.0")
        self.coolant_label.grid(row=2, column=2)
        self.emulator.coolant.trace_add('write', lambda *_: self.coolant_label.config(text=f"{self.emulator.coolant.get():.1f}"))

        # Oil pressure
        ttk.Label(engine_frame, text="Oil bar:").grid(row=3, column=0, sticky='w')
        ttk.Scale(engine_frame, from_=0, to=10, variable=self.emulator.oil_pressure, orient='horizontal', length=300).grid(row=3, column=1, padx=5)
        self.oil_label = ttk.Label(engine_frame, text="4.5")
        self.oil_label.grid(row=3, column=2)
        self.emulator.oil_pressure.trace_add('write', lambda *_: self.oil_label.config(text=f"{self.emulator.oil_pressure.get():.1f}"))

        # Vehicle data frame
        vehicle_frame = ttk.LabelFrame(self.root, text="Vehicle Data", padding=10)
        vehicle_frame.pack(fill='x', padx=10, pady=5)

        # Speed
        ttk.Label(vehicle_frame, text="Speed km/h:").grid(row=0, column=0, sticky='w')
        ttk.Scale(vehicle_frame, from_=0, to=300, variable=self.emulator.speed, orient='horizontal', length=300).grid(row=0, column=1, padx=5)
        self.speed_label = ttk.Label(vehicle_frame, text="0")
        self.speed_label.grid(row=0, column=2)
        self.emulator.speed.trace_add('write', lambda *_: self.speed_label.config(text=str(self.emulator.speed.get())))

        # Gear
        ttk.Label(vehicle_frame, text="Gear:").grid(row=1, column=0, sticky='w')
        gear_frame = ttk.Frame(vehicle_frame)
        gear_frame.grid(row=1, column=1, sticky='w')
        for i in range(7):
            ttk.Radiobutton(gear_frame, text=str(i) if i > 0 else "N", variable=self.emulator.gear, value=i).pack(side='left', padx=3)

        # Brake
        ttk.Label(vehicle_frame, text="Brake %:").grid(row=2, column=0, sticky='w')
        ttk.Scale(vehicle_frame, from_=0, to=100, variable=self.emulator.brake, orient='horizontal', length=300).grid(row=2, column=1, padx=5)

        # Flags frame
        flags_frame = ttk.LabelFrame(self.root, text="Flags", padding=10)
        flags_frame.pack(fill='x', padx=10, pady=5)

        ttk.Checkbutton(flags_frame, text="Ignition ON", variable=self.emulator.ignition).pack(side='left', padx=10)
        ttk.Checkbutton(flags_frame, text="Rev Limiter", variable=self.emulator.rev_limiter).pack(side='left', padx=10)
        ttk.Checkbutton(flags_frame, text="Launch Control", variable=self.emulator.launch_control).pack(side='left', padx=10)

        # Presets frame
        presets_frame = ttk.LabelFrame(self.root, text="Presets", padding=10)
        presets_frame.pack(fill='x', padx=10, pady=5)

        ttk.Button(presets_frame, text="Idle", command=self.emulator.set_preset_idle).pack(side='left', padx=5)
        ttk.Button(presets_frame, text="Cruise", command=self.emulator.set_preset_cruise).pack(side='left', padx=5)
        ttk.Button(presets_frame, text="Redline", command=self.emulator.set_preset_redline).pack(side='left', padx=5)
        ttk.Button(presets_frame, text="Cold Start", command=self.emulator.set_preset_cold).pack(side='left', padx=5)
        ttk.Button(presets_frame, text="Low Oil!", command=self.emulator.set_preset_low_oil).pack(side='left', padx=5)

        # Control frame
        control_frame = ttk.LabelFrame(self.root, text="Transmission", padding=10)
        control_frame.pack(fill='x', padx=10, pady=5)

        ttk.Button(control_frame, text="Send Once", command=self.send_once).pack(side='left', padx=10)

        self.cyclic_btn = ttk.Button(control_frame, text="Start Cyclic (10 Hz)", command=self.toggle_cyclic)
        self.cyclic_btn.pack(side='left', padx=10)

        self.cyclic_status = ttk.Label(control_frame, text="")
        self.cyclic_status.pack(side='left', padx=10)

    def refresh_ports(self):
        """Refresh available COM ports"""
        ports = serial.tools.list_ports.comports()
        port_list = [f"{p.device} - {p.description}" for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)

    def toggle_connection(self):
        """Connect or disconnect"""
        if self.emulator.is_connected():
            self.emulator.disconnect()
            self.connect_btn.config(text="Connect")
            self.status_label.config(text="Disconnected", foreground='red')
        else:
            selection = self.port_combo.get()
            if not selection:
                messagebox.showerror("Error", "Please select a port")
                return

            port = selection.split(' - ')[0]
            success, msg = self.emulator.connect(port)

            if success:
                self.connect_btn.config(text="Disconnect")
                self.status_label.config(text=msg, foreground='green')
            else:
                messagebox.showerror("Connection Error", msg)

    def send_once(self):
        """Send all frames once"""
        if not self.emulator.is_connected():
            messagebox.showwarning("Not connected", "Please connect to a port first")
            return
        self.emulator.send_all_frames()

    def toggle_cyclic(self):
        """Start or stop cyclic transmission"""
        if not self.emulator.is_connected():
            messagebox.showwarning("Not connected", "Please connect to a port first")
            return

        if self.emulator.running:
            self.emulator.stop_cyclic()
            self.cyclic_btn.config(text="Start Cyclic (10 Hz)")
            self.cyclic_status.config(text="Stopped")
        else:
            self.emulator.start_cyclic(0.1)
            self.cyclic_btn.config(text="Stop Cyclic")
            self.cyclic_status.config(text="Running...")

    def run(self):
        """Run the GUI"""
        self.root.mainloop()
        self.emulator.disconnect()


def main():
    app = EmulatorGUI()
    app.run()


if __name__ == '__main__':
    main()
