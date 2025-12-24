#!/usr/bin/env python3
"""
LED Strip Emulator for ESP32 CAN LED Controller
GUI application that visualizes LED strip output from the ESP32 firmware.
Also supports sending CAN messages for testing without hardware.

Features:
- Real-time LED strip visualization (60 LEDs)
- Serial port connection for bidirectional communication
- Receives LED data stream from firmware (LED:count:HEXDATA format)
- Sends CAN messages to firmware (CAN:ID:DLC:DATA format)
- Built-in CAN protocol templates (Link Generic Dashboard, Custom)
- Adjustable simulation parameters (RPM, Throttle, etc.)
"""

import serial
import serial.tools.list_ports
import time
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox
import math


class LEDStripVisualizer(tk.Canvas):
    """Canvas widget that visualizes an LED strip"""

    def __init__(self, parent, led_count=60, led_size=14, spacing=2, **kwargs):
        self.led_count = led_count
        self.led_size = led_size
        self.spacing = spacing
        self.leds_per_row = 30
        self.rows = math.ceil(led_count / self.leds_per_row)

        width = self.leds_per_row * (led_size + spacing) + spacing
        height = self.rows * (led_size + spacing) + spacing + 30  # +30 for labels

        super().__init__(parent, width=width, height=height, bg='#1a1a1a', **kwargs)

        self.led_colors = [(0, 0, 0)] * led_count
        self.led_items = []
        self.label_items = []

        self._create_leds()

    def _create_leds(self):
        """Create LED circle objects"""
        for i in range(self.led_count):
            row = i // self.leds_per_row
            col = i % self.leds_per_row

            x = self.spacing + col * (self.led_size + self.spacing) + self.led_size // 2
            y = self.spacing + row * (self.led_size + self.spacing) + self.led_size // 2

            # Create LED circle with glow effect
            item = self.create_oval(
                x - self.led_size // 2, y - self.led_size // 2,
                x + self.led_size // 2, y + self.led_size // 2,
                fill='#000000', outline='#333333', width=1
            )
            self.led_items.append(item)

        # Add index labels every 10 LEDs
        for i in range(0, self.led_count, 10):
            row = i // self.leds_per_row
            col = i % self.leds_per_row
            x = self.spacing + col * (self.led_size + self.spacing) + self.led_size // 2
            y = self.rows * (self.led_size + self.spacing) + 15

            label = self.create_text(x, y, text=str(i), fill='#666666', font=('Arial', 8))
            self.label_items.append(label)

    def set_led_colors(self, colors):
        """Update LED colors from list of (r, g, b) tuples"""
        self.led_colors = colors[:self.led_count]

        for i, (r, g, b) in enumerate(self.led_colors):
            if i < len(self.led_items):
                # Convert RGB to hex color
                color = f'#{r:02x}{g:02x}{b:02x}'

                # Calculate brightness for outline glow effect
                brightness = max(r, g, b)
                if brightness > 50:
                    outline = f'#{min(255, r + 50):02x}{min(255, g + 50):02x}{min(255, b + 50):02x}'
                else:
                    outline = '#333333'

                self.itemconfig(self.led_items[i], fill=color, outline=outline)

    def clear(self):
        """Turn off all LEDs"""
        self.set_led_colors([(0, 0, 0)] * self.led_count)


class CANMessageBuilder:
    """Builds CAN messages for Link ECU Generic Dashboard protocol"""

    @staticmethod
    def build_link_generic_messages(rpm, tps, coolant, oil_pressure, speed, gear, ignition):
        """Build Link Generic Dashboard protocol messages"""
        messages = []

        # 0x5F0: RPM (u32 LE) + TPS*10 (u16 LE)
        rpm_bytes = rpm.to_bytes(4, 'little')
        tps_bytes = int(tps * 10).to_bytes(2, 'little')
        data = rpm_bytes + tps_bytes + bytes([0, 0])
        messages.append((0x5F0, 8, data))

        # 0x5F3: Coolant*10 (u16) + AirTemp*10 (u16)
        coolant_bytes = int(coolant * 10).to_bytes(2, 'little')
        airtemp_bytes = int(25 * 10).to_bytes(2, 'little')  # Fixed air temp
        data = coolant_bytes + airtemp_bytes + bytes([0, 0, 0, 0])
        messages.append((0x5F3, 8, data))

        # 0x5F4: Battery*100 (u16) + Flags
        battery_bytes = int(14.0 * 100).to_bytes(2, 'little')
        flags = 0x80 if ignition else 0x00  # Bit 7 = ignition
        data = battery_bytes + bytes([flags, 0, 0, 0, 0, 0])
        messages.append((0x5F4, 8, data))

        # 0x5F5: Gear + pad + OilPressure*100 (u16)
        oil_bytes = int(oil_pressure * 100).to_bytes(2, 'little')
        data = bytes([gear, 0]) + oil_bytes + bytes([0, 0, 0, 0])
        messages.append((0x5F5, 8, data))

        # 0x5F6: Speed*10 (u16)
        speed_bytes = int(speed * 10).to_bytes(2, 'little')
        data = speed_bytes + bytes([0, 0, 0, 0, 0, 0])
        messages.append((0x5F6, 8, data))

        return messages

    @staticmethod
    def format_serial_command(can_id, dlc, data):
        """Format CAN message for serial protocol"""
        hex_data = ''.join(f'{b:02X}' for b in data[:dlc])
        return f"CAN:{can_id:03X}:{dlc}:{hex_data}"


class LEDStripEmulator:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LED Strip Emulator")
        self.root.geometry("1100x800")
        self.root.minsize(900, 700)
        self.root.configure(bg='#2d2d2d')

        self.ser = None
        self.running = False
        self.read_thread = None
        self.send_thread = None
        self.cyclic_running = False

        self.frame_count = 0
        self.last_fps_time = time.time()
        self.fps = 0

        self.create_widgets()
        self.refresh_ports()

    def create_widgets(self):
        # Apply dark theme
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TFrame', background='#2d2d2d')
        style.configure('TLabelframe', background='#2d2d2d', foreground='#ffffff')
        style.configure('TLabelframe.Label', background='#2d2d2d', foreground='#ffffff')
        style.configure('TLabel', background='#2d2d2d', foreground='#ffffff')
        style.configure('TButton', background='#404040', foreground='#ffffff')
        style.configure('TCheckbutton', background='#2d2d2d', foreground='#ffffff')
        style.configure('TScale', background='#2d2d2d')

        # Main container
        main_frame = ttk.Frame(self.root, padding=10)
        main_frame.pack(fill='both', expand=True)

        # Top section - LED Strip Visualization
        led_frame = ttk.LabelFrame(main_frame, text="LED Strip Visualization", padding=10)
        led_frame.pack(fill='x', pady=(0, 10))

        self.led_strip = LEDStripVisualizer(led_frame, led_count=60)
        self.led_strip.pack()

        # Status bar under LED strip
        status_bar = ttk.Frame(led_frame)
        status_bar.pack(fill='x', pady=(10, 0))

        self.fps_label = ttk.Label(status_bar, text="FPS: 0")
        self.fps_label.pack(side='left')

        self.frame_label = ttk.Label(status_bar, text="Frames: 0")
        self.frame_label.pack(side='left', padx=20)

        self.data_label = ttk.Label(status_bar, text="No data")
        self.data_label.pack(side='right')

        # Bottom section - split into left and right
        bottom_frame = ttk.Frame(main_frame)
        bottom_frame.pack(fill='both', expand=True)

        # Left panel - Connection & Controls
        left_panel = ttk.Frame(bottom_frame, width=350)
        left_panel.pack(side='left', fill='y', padx=(0, 10))
        left_panel.pack_propagate(False)

        # Connection frame
        conn_frame = ttk.LabelFrame(left_panel, text="Connection", padding=10)
        conn_frame.pack(fill='x', pady=(0, 10))

        port_row = ttk.Frame(conn_frame)
        port_row.pack(fill='x')
        ttk.Label(port_row, text="Port:").pack(side='left')
        self.port_combo = ttk.Combobox(port_row, width=20, state='readonly')
        self.port_combo.pack(side='left', padx=5)
        ttk.Button(port_row, text="Refresh", command=self.refresh_ports, width=8).pack(side='left')

        btn_row = ttk.Frame(conn_frame)
        btn_row.pack(fill='x', pady=(10, 0))
        self.connect_btn = ttk.Button(btn_row, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side='left')
        self.status_label = ttk.Label(btn_row, text="Disconnected", foreground='#ff6666')
        self.status_label.pack(side='left', padx=10)

        # Vehicle Simulation frame
        sim_frame = ttk.LabelFrame(left_panel, text="Vehicle Simulation", padding=10)
        sim_frame.pack(fill='x', pady=(0, 10))

        # RPM slider
        rpm_row = ttk.Frame(sim_frame)
        rpm_row.pack(fill='x', pady=2)
        ttk.Label(rpm_row, text="RPM:", width=10).pack(side='left')
        self.rpm_var = tk.IntVar(value=800)
        self.rpm_scale = ttk.Scale(rpm_row, from_=0, to=8000, variable=self.rpm_var,
                                   orient='horizontal', length=150)
        self.rpm_scale.pack(side='left', padx=5)
        self.rpm_label = ttk.Label(rpm_row, text="800", width=6)
        self.rpm_label.pack(side='left')
        self.rpm_var.trace_add('write', lambda *_: self.rpm_label.config(text=str(self.rpm_var.get())))

        # Throttle slider
        tps_row = ttk.Frame(sim_frame)
        tps_row.pack(fill='x', pady=2)
        ttk.Label(tps_row, text="Throttle %:", width=10).pack(side='left')
        self.tps_var = tk.IntVar(value=0)
        self.tps_scale = ttk.Scale(tps_row, from_=0, to=100, variable=self.tps_var,
                                   orient='horizontal', length=150)
        self.tps_scale.pack(side='left', padx=5)
        self.tps_label = ttk.Label(tps_row, text="0", width=6)
        self.tps_label.pack(side='left')
        self.tps_var.trace_add('write', lambda *_: self.tps_label.config(text=str(self.tps_var.get())))

        # Brake slider
        brake_row = ttk.Frame(sim_frame)
        brake_row.pack(fill='x', pady=2)
        ttk.Label(brake_row, text="Brake %:", width=10).pack(side='left')
        self.brake_var = tk.IntVar(value=0)
        self.brake_scale = ttk.Scale(brake_row, from_=0, to=100, variable=self.brake_var,
                                     orient='horizontal', length=150)
        self.brake_scale.pack(side='left', padx=5)
        self.brake_label = ttk.Label(brake_row, text="0", width=6)
        self.brake_label.pack(side='left')
        self.brake_var.trace_add('write', lambda *_: self.brake_label.config(text=str(self.brake_var.get())))

        # Coolant slider
        coolant_row = ttk.Frame(sim_frame)
        coolant_row.pack(fill='x', pady=2)
        ttk.Label(coolant_row, text="Coolant C:", width=10).pack(side='left')
        self.coolant_var = tk.IntVar(value=85)
        self.coolant_scale = ttk.Scale(coolant_row, from_=40, to=120, variable=self.coolant_var,
                                       orient='horizontal', length=150)
        self.coolant_scale.pack(side='left', padx=5)
        self.coolant_label = ttk.Label(coolant_row, text="85", width=6)
        self.coolant_label.pack(side='left')
        self.coolant_var.trace_add('write', lambda *_: self.coolant_label.config(text=str(self.coolant_var.get())))

        # Oil pressure slider
        oil_row = ttk.Frame(sim_frame)
        oil_row.pack(fill='x', pady=2)
        ttk.Label(oil_row, text="Oil bar:", width=10).pack(side='left')
        self.oil_var = tk.DoubleVar(value=4.5)
        self.oil_scale = ttk.Scale(oil_row, from_=0, to=8, variable=self.oil_var,
                                   orient='horizontal', length=150)
        self.oil_scale.pack(side='left', padx=5)
        self.oil_label = ttk.Label(oil_row, text="4.5", width=6)
        self.oil_label.pack(side='left')
        self.oil_var.trace_add('write', lambda *_: self.oil_label.config(text=f"{self.oil_var.get():.1f}"))

        # Checkboxes row
        check_row = ttk.Frame(sim_frame)
        check_row.pack(fill='x', pady=(10, 5))
        self.ignition_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(check_row, text="Ignition", variable=self.ignition_var).pack(side='left')
        self.revlim_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(check_row, text="Rev Limiter", variable=self.revlim_var).pack(side='left', padx=10)

        # Gear selector
        gear_row = ttk.Frame(sim_frame)
        gear_row.pack(fill='x', pady=5)
        ttk.Label(gear_row, text="Gear:").pack(side='left')
        self.gear_var = tk.StringVar(value="N")
        gear_combo = ttk.Combobox(gear_row, textvariable=self.gear_var,
                                  values=["N", "1", "2", "3", "4", "5", "6"],
                                  width=5, state='readonly')
        gear_combo.pack(side='left', padx=5)

        # Transmission controls
        tx_frame = ttk.LabelFrame(left_panel, text="Transmission", padding=10)
        tx_frame.pack(fill='x', pady=(0, 10))

        tx_row1 = ttk.Frame(tx_frame)
        tx_row1.pack(fill='x')
        ttk.Button(tx_row1, text="Send Once", command=self.send_once).pack(side='left', padx=2)
        self.cyclic_btn = ttk.Button(tx_row1, text="Start Cyclic", command=self.toggle_cyclic)
        self.cyclic_btn.pack(side='left', padx=2)

        tx_row2 = ttk.Frame(tx_frame)
        tx_row2.pack(fill='x', pady=(10, 0))
        ttk.Label(tx_row2, text="Interval ms:").pack(side='left')
        self.interval_var = tk.StringVar(value="100")
        ttk.Entry(tx_row2, textvariable=self.interval_var, width=6).pack(side='left', padx=5)
        self.tx_status = ttk.Label(tx_row2, text="")
        self.tx_status.pack(side='left', padx=10)

        # Preset buttons
        preset_frame = ttk.LabelFrame(left_panel, text="Presets", padding=10)
        preset_frame.pack(fill='x')

        preset_row1 = ttk.Frame(preset_frame)
        preset_row1.pack(fill='x', pady=2)
        ttk.Button(preset_row1, text="Idle", command=self.preset_idle, width=8).pack(side='left', padx=2)
        ttk.Button(preset_row1, text="Cruise", command=self.preset_cruise, width=8).pack(side='left', padx=2)
        ttk.Button(preset_row1, text="Redline", command=self.preset_redline, width=8).pack(side='left', padx=2)

        preset_row2 = ttk.Frame(preset_frame)
        preset_row2.pack(fill='x', pady=2)
        ttk.Button(preset_row2, text="Cold Start", command=self.preset_cold, width=8).pack(side='left', padx=2)
        ttk.Button(preset_row2, text="Overheat", command=self.preset_overheat, width=8).pack(side='left', padx=2)
        ttk.Button(preset_row2, text="Low Oil", command=self.preset_low_oil, width=8).pack(side='left', padx=2)

        # Right panel - Log
        right_panel = ttk.Frame(bottom_frame)
        right_panel.pack(side='left', fill='both', expand=True)

        log_frame = ttk.LabelFrame(right_panel, text="Communication Log", padding=5)
        log_frame.pack(fill='both', expand=True)

        log_controls = ttk.Frame(log_frame)
        log_controls.pack(fill='x')
        ttk.Button(log_controls, text="Clear", command=self.clear_log).pack(side='left')
        self.autoscroll_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(log_controls, text="Auto-scroll", variable=self.autoscroll_var).pack(side='left', padx=10)
        self.show_led_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(log_controls, text="Show LED data", variable=self.show_led_var).pack(side='left')
        self.log_count_label = ttk.Label(log_controls, text="Lines: 0")
        self.log_count_label.pack(side='right')

        log_scroll = ttk.Scrollbar(log_frame)
        log_scroll.pack(side='right', fill='y')

        self.log_text = tk.Text(log_frame, height=15, font=('Consolas', 9),
                                 bg='#1a1a1a', fg='#ffffff', insertbackground='white',
                                 state='disabled', yscrollcommand=log_scroll.set)
        self.log_text.pack(fill='both', expand=True)
        log_scroll.config(command=self.log_text.yview)

        # Configure log colors
        self.log_text.tag_config('timestamp', foreground='#888888')
        self.log_text.tag_config('tx', foreground='#66ff66')
        self.log_text.tag_config('rx', foreground='#6699ff')
        self.log_text.tag_config('led', foreground='#ff66ff')
        self.log_text.tag_config('error', foreground='#ff6666')

        self.log_line_count = 0

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [f"{p.device} - {p.description}" for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        selection = self.port_combo.get()
        if not selection:
            messagebox.showerror("Error", "Please select a port")
            return

        port = selection.split(' - ')[0]
        try:
            self.ser = serial.Serial(port, 115200, timeout=0.1)
            self.running = True
            self.read_thread = threading.Thread(target=self.read_loop, daemon=True)
            self.read_thread.start()

            self.connect_btn.config(text="Disconnect")
            self.status_label.config(text="Connected", foreground='#66ff66')
            self.log_message("Connected to " + port, 'rx')

        except serial.SerialException as e:
            messagebox.showerror("Connection Error", str(e))

    def disconnect(self):
        self.running = False
        self.cyclic_running = False

        if self.read_thread:
            self.read_thread.join(timeout=1)
        if self.send_thread:
            self.send_thread.join(timeout=1)

        if self.ser:
            self.ser.close()
            self.ser = None

        self.connect_btn.config(text="Connect")
        self.status_label.config(text="Disconnected", foreground='#ff6666')
        self.cyclic_btn.config(text="Start Cyclic")
        self.tx_status.config(text="")
        self.log_message("Disconnected", 'rx')

    def read_loop(self):
        """Background thread that reads serial data"""
        buffer = ""
        while self.running:
            try:
                if self.ser and self.ser.is_open and self.ser.in_waiting:
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data

                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self.root.after(0, lambda l=line: self.process_line(l))
                else:
                    time.sleep(0.01)
            except Exception as e:
                self.root.after(0, lambda: self.log_message(f"Read error: {e}", 'error'))
                time.sleep(0.1)

    def process_line(self, line):
        """Process received line from serial"""
        if line.startswith("LED:"):
            self.process_led_data(line)
            if self.show_led_var.get():
                self.log_message(f"RX: {line[:80]}...", 'led')
        else:
            self.log_message(f"RX: {line}", 'rx')

    def process_led_data(self, line):
        """Parse LED data and update visualization"""
        try:
            parts = line.split(':')
            if len(parts) >= 3:
                led_count = int(parts[1])
                hex_data = parts[2]

                colors = []
                for i in range(0, len(hex_data), 6):
                    if i + 6 <= len(hex_data):
                        r = int(hex_data[i:i+2], 16)
                        g = int(hex_data[i+2:i+4], 16)
                        b = int(hex_data[i+4:i+6], 16)
                        colors.append((r, g, b))

                self.led_strip.set_led_colors(colors)

                # Update stats
                self.frame_count += 1
                self.frame_label.config(text=f"Frames: {self.frame_count}")

                now = time.time()
                if now - self.last_fps_time >= 1.0:
                    self.fps = self.frame_count / (now - self.last_fps_time)
                    self.frame_count = 0
                    self.last_fps_time = now
                    self.fps_label.config(text=f"FPS: {self.fps:.1f}")

                self.data_label.config(text=f"LEDs: {len(colors)}")

        except Exception as e:
            self.log_message(f"LED parse error: {e}", 'error')

    def send_can_messages(self):
        """Send current vehicle state as CAN messages"""
        if not self.ser or not self.ser.is_open:
            return

        rpm = self.rpm_var.get()
        tps = self.tps_var.get()
        coolant = self.coolant_var.get()
        oil = self.oil_var.get()
        ignition = self.ignition_var.get()

        gear_map = {"N": 0, "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6}
        gear = gear_map.get(self.gear_var.get(), 0)

        # Build and send messages
        messages = CANMessageBuilder.build_link_generic_messages(
            rpm, tps, coolant, oil, 0, gear, ignition
        )

        for can_id, dlc, data in messages:
            cmd = CANMessageBuilder.format_serial_command(can_id, dlc, data)
            self.ser.write((cmd + '\n').encode())
            self.log_message(f"TX: {cmd}", 'tx')

    def send_once(self):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("Not connected", "Please connect first")
            return
        self.send_can_messages()

    def toggle_cyclic(self):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("Not connected", "Please connect first")
            return

        if self.cyclic_running:
            self.cyclic_running = False
            self.cyclic_btn.config(text="Start Cyclic")
            self.tx_status.config(text="Stopped", foreground='#888888')
        else:
            self.cyclic_running = True
            self.cyclic_btn.config(text="Stop Cyclic")
            self.tx_status.config(text="Running...", foreground='#66ff66')

            self.send_thread = threading.Thread(target=self.cyclic_send_loop, daemon=True)
            self.send_thread.start()

    def cyclic_send_loop(self):
        """Background thread for cyclic transmission"""
        while self.cyclic_running and self.running:
            try:
                interval = int(self.interval_var.get()) / 1000.0
            except ValueError:
                interval = 0.1

            self.root.after(0, self.send_can_messages)
            time.sleep(interval)

    def log_message(self, text, tag='rx'):
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]

        self.log_text.config(state='normal')
        self.log_text.insert(tk.END, f"[{timestamp}] ", 'timestamp')
        self.log_text.insert(tk.END, f"{text}\n", tag)
        self.log_text.config(state='disabled')

        if self.autoscroll_var.get():
            self.log_text.see(tk.END)

        self.log_line_count += 1
        self.log_count_label.config(text=f"Lines: {self.log_line_count}")

        # Limit log size
        if self.log_line_count > 1000:
            self.log_text.config(state='normal')
            self.log_text.delete('1.0', '100.0')
            self.log_text.config(state='disabled')
            self.log_line_count -= 100

    def clear_log(self):
        self.log_text.config(state='normal')
        self.log_text.delete('1.0', tk.END)
        self.log_text.config(state='disabled')
        self.log_line_count = 0
        self.log_count_label.config(text="Lines: 0")

    # Preset functions
    def preset_idle(self):
        self.rpm_var.set(800)
        self.tps_var.set(0)
        self.brake_var.set(0)
        self.coolant_var.set(85)
        self.oil_var.set(4.5)
        self.ignition_var.set(True)
        self.gear_var.set("N")

    def preset_cruise(self):
        self.rpm_var.set(3000)
        self.tps_var.set(25)
        self.brake_var.set(0)
        self.coolant_var.set(90)
        self.oil_var.set(4.0)
        self.ignition_var.set(True)
        self.gear_var.set("4")

    def preset_redline(self):
        self.rpm_var.set(7000)
        self.tps_var.set(100)
        self.brake_var.set(0)
        self.coolant_var.set(95)
        self.oil_var.set(5.0)
        self.ignition_var.set(True)
        self.gear_var.set("3")

    def preset_cold(self):
        self.rpm_var.set(1200)
        self.tps_var.set(0)
        self.brake_var.set(0)
        self.coolant_var.set(45)
        self.oil_var.set(5.5)
        self.ignition_var.set(True)
        self.gear_var.set("N")

    def preset_overheat(self):
        self.rpm_var.set(3500)
        self.tps_var.set(40)
        self.brake_var.set(0)
        self.coolant_var.set(115)
        self.oil_var.set(3.0)
        self.ignition_var.set(True)
        self.gear_var.set("2")

    def preset_low_oil(self):
        self.rpm_var.set(4000)
        self.tps_var.set(60)
        self.brake_var.set(0)
        self.coolant_var.set(95)
        self.oil_var.set(1.0)
        self.ignition_var.set(True)
        self.gear_var.set("3")

    def run(self):
        self.root.mainloop()
        self.disconnect()


def main():
    app = LEDStripEmulator()
    app.run()


if __name__ == '__main__':
    main()
