#!/usr/bin/env python3
"""
Serial CAN Emulator for ESP32 CAN LED Controller
GUI application to send simulated CAN frames over Serial port.
Features real-time log, custom message editor, and message queue management.
"""

import serial
import serial.tools.list_ports
import struct
import time
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox

# Predefined protocol templates with default values
# RPM=800, TPS=0%, Coolant=85°C, AirTemp=25°C, Battery=14V, Oil=4.5bar, Speed=0, Gear=N, Ignition=ON
PROTOCOL_TEMPLATES = {
    "Link Generic Dashboard": [
        # 0x5F0: RPM (u32 LE) + TPS*10 (u16 LE) = 800, 0
        {"id": 0x5F0, "name": "RPM & TPS", "dlc": 8, "data": [0x20,0x03,0x00,0x00, 0x00,0x00, 0x00,0x00], "enabled": True,
         "fields": [("RPM", 0, 4, "u32", 1), ("TPS", 4, 2, "u16", 0.1)]},
        # 0x5F3: Coolant*10 (u16) + AirTemp*10 (u16) = 850, 250
        {"id": 0x5F3, "name": "Temperatures", "dlc": 8, "data": [0x52,0x03, 0xFA,0x00, 0x00,0x00,0x00,0x00], "enabled": True,
         "fields": [("Coolant", 0, 2, "u16", 0.1), ("AirTemp", 2, 2, "u16", 0.1)]},
        # 0x5F4: Battery*100 (u16) + Flags = 1400, 0x80 (ignition on)
        {"id": 0x5F4, "name": "Voltage & Flags", "dlc": 8, "data": [0x78,0x05, 0x80, 0x00,0x00,0x00,0x00,0x00], "enabled": True,
         "fields": [("Battery", 0, 2, "u16", 0.01), ("Flags", 2, 1, "u8", 1)]},
        # 0x5F5: Gear + pad + OilPressure*100 (u16) = 0, 450
        {"id": 0x5F5, "name": "Gear & Oil", "dlc": 8, "data": [0x00,0x00, 0xC2,0x01, 0x00,0x00,0x00,0x00], "enabled": True,
         "fields": [("Gear", 0, 1, "u8", 1), ("OilPressure", 2, 2, "u16", 0.01)]},
        # 0x5F6: Speed*10 (u16) = 0
        {"id": 0x5F6, "name": "Speed", "dlc": 8, "data": [0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00], "enabled": True,
         "fields": [("Speed", 0, 2, "u16", 0.1)]},
    ],
    "Link Generic Dashboard 2": [
        # 0x2000: RPM (u16) + TPS*10 (u16) + Coolant*10 (u16) + AirTemp*10 (u16)
        {"id": 0x2000, "name": "Engine Data 1", "dlc": 8, "data": [0x20,0x03, 0x00,0x00, 0x52,0x03, 0xFA,0x00], "enabled": True,
         "fields": [("RPM", 0, 2, "u16", 1), ("TPS", 2, 2, "u16", 0.1), ("Coolant", 4, 2, "u16", 0.1), ("AirTemp", 6, 2, "u16", 0.1)]},
        # 0x2001: MAP (u16) + Battery*100 (u16) + FuelPressure (u16) + OilPressure*10 (u16)
        {"id": 0x2001, "name": "Engine Data 2", "dlc": 8, "data": [0xE8,0x03, 0x78,0x05, 0x2C,0x01, 0x2D,0x00], "enabled": True,
         "fields": [("MAP", 0, 2, "u16", 1), ("Battery", 2, 2, "u16", 0.01), ("FuelPressure", 4, 2, "u16", 1), ("OilPressure", 6, 2, "u16", 0.1)]},
        # 0x2004: Speed*10 (u16) + Gear (u8) + Flags (u8)
        {"id": 0x2004, "name": "Vehicle Data", "dlc": 8, "data": [0x00,0x00, 0x00, 0x80, 0x00,0x00,0x00,0x00], "enabled": True,
         "fields": [("Speed", 0, 2, "u16", 0.1), ("Gear", 2, 1, "u8", 1), ("Flags", 3, 1, "u8", 1)]},
    ],
    "Custom": [
        {"id": 0x100, "name": "Throttle", "dlc": 1, "data": [0x00], "enabled": True, "fields": []},
        {"id": 0x101, "name": "Brake", "dlc": 3, "data": [0x00,0x00,0x00], "enabled": True, "fields": []},
        {"id": 0x102, "name": "RPM", "dlc": 2, "data": [0x20,0x03], "enabled": True, "fields": []},  # 800
        {"id": 0x103, "name": "Coolant", "dlc": 2, "data": [0x52,0x03], "enabled": True, "fields": []},  # 850 = 85.0°C
        {"id": 0x104, "name": "Oil Pressure", "dlc": 2, "data": [0x2D,0x00], "enabled": True, "fields": []},  # 45 = 4.5bar
        {"id": 0x105, "name": "Flags", "dlc": 1, "data": [0x00], "enabled": True, "fields": []},
        {"id": 0x106, "name": "Ignition", "dlc": 1, "data": [0x01], "enabled": True, "fields": []},
    ],
}


class CANMessage:
    def __init__(self, can_id=0x100, name="New Message", dlc=8, data=None, enabled=True):
        self.id = can_id
        self.name = name
        self.dlc = dlc
        self.data = data if data else [0] * dlc
        self.enabled = enabled

    def to_serial_format(self):
        """Convert to serial protocol format: CAN:ID:DLC:HEXDATA"""
        hex_data = ''.join(f'{b:02X}' for b in self.data[:self.dlc])
        return f"CAN:{self.id:03X}:{self.dlc}:{hex_data}"

    def get_display_string(self):
        """Get formatted display string"""
        hex_data = ' '.join(f'{b:02X}' for b in self.data[:self.dlc])
        return f"0x{self.id:03X} [{self.dlc}] {hex_data}"


class SerialCANEmulator:
    def __init__(self):
        self.ser = None
        self.messages = []
        self.running = False
        self.cyclic_thread = None
        self.log_callback = None
        self.interval = 0.1

    def connect(self, port, baudrate=115200):
        try:
            self.ser = serial.Serial(port, baudrate, timeout=0.1)
            return True, f"Connected to {port}"
        except serial.SerialException as e:
            return False, str(e)

    def disconnect(self):
        self.stop_cyclic()
        if self.ser:
            self.ser.close()
            self.ser = None

    def is_connected(self):
        return self.ser is not None and self.ser.is_open

    def send_message(self, msg):
        if not self.is_connected() or not msg.enabled:
            return
        cmd = msg.to_serial_format() + "\n"
        self.ser.write(cmd.encode())
        if self.log_callback:
            self.log_callback(msg)

    def send_all_enabled(self):
        for msg in self.messages:
            if msg.enabled:
                self.send_message(msg)

    def start_cyclic(self, interval=0.1):
        if self.running:
            return
        self.interval = interval
        self.running = True
        self.cyclic_thread = threading.Thread(target=self._cyclic_loop, daemon=True)
        self.cyclic_thread.start()

    def stop_cyclic(self):
        self.running = False
        if self.cyclic_thread:
            self.cyclic_thread.join(timeout=1)
            self.cyclic_thread = None

    def _cyclic_loop(self):
        while self.running:
            self.send_all_enabled()
            time.sleep(self.interval)

    def load_protocol(self, protocol_name):
        self.messages.clear()
        if protocol_name in PROTOCOL_TEMPLATES:
            for tmpl in PROTOCOL_TEMPLATES[protocol_name]:
                msg = CANMessage(
                    can_id=tmpl["id"],
                    name=tmpl["name"],
                    dlc=tmpl["dlc"],
                    data=tmpl["data"].copy(),
                    enabled=tmpl["enabled"]
                )
                self.messages.append(msg)


class EmulatorGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Serial CAN Emulator")
        self.root.geometry("1000x750")
        self.root.minsize(900, 600)

        self.emulator = SerialCANEmulator()
        self.emulator.log_callback = self.log_message
        self.selected_msg_idx = None
        self.log_counter = 0

        self.create_widgets()
        self.refresh_ports()
        self.load_protocol("Link Generic Dashboard")

    def create_widgets(self):
        # Main paned window
        main_paned = ttk.PanedWindow(self.root, orient='horizontal')
        main_paned.pack(fill='both', expand=True, padx=5, pady=5)

        # Left panel - Message Queue
        left_frame = ttk.Frame(main_paned, width=400)
        main_paned.add(left_frame, weight=1)

        # Right panel - Editor and Log
        right_frame = ttk.Frame(main_paned, width=550)
        main_paned.add(right_frame, weight=2)

        # === LEFT PANEL ===
        # Connection frame
        conn_frame = ttk.LabelFrame(left_frame, text="Connection", padding=5)
        conn_frame.pack(fill='x', padx=5, pady=5)

        port_row = ttk.Frame(conn_frame)
        port_row.pack(fill='x')
        ttk.Label(port_row, text="Port:").pack(side='left')
        self.port_combo = ttk.Combobox(port_row, width=25, state='readonly')
        self.port_combo.pack(side='left', padx=5)
        ttk.Button(port_row, text="Refresh", command=self.refresh_ports, width=8).pack(side='left')

        btn_row = ttk.Frame(conn_frame)
        btn_row.pack(fill='x', pady=5)
        self.connect_btn = ttk.Button(btn_row, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side='left')
        self.status_label = ttk.Label(btn_row, text="Disconnected", foreground='red')
        self.status_label.pack(side='left', padx=10)

        # Protocol frame
        proto_frame = ttk.LabelFrame(left_frame, text="Protocol Template", padding=5)
        proto_frame.pack(fill='x', padx=5, pady=5)

        self.protocol_var = tk.StringVar(value="Link Generic Dashboard")
        proto_combo = ttk.Combobox(proto_frame, textvariable=self.protocol_var,
                                   values=list(PROTOCOL_TEMPLATES.keys()), state='readonly', width=30)
        proto_combo.pack(side='left', padx=5)
        ttk.Button(proto_frame, text="Load", command=self.on_load_protocol).pack(side='left')

        # Message Queue frame
        queue_frame = ttk.LabelFrame(left_frame, text="Message Queue", padding=5)
        queue_frame.pack(fill='both', expand=True, padx=5, pady=5)

        # Queue listbox with scrollbar
        queue_scroll = ttk.Scrollbar(queue_frame)
        queue_scroll.pack(side='right', fill='y')

        self.queue_listbox = tk.Listbox(queue_frame, height=12, font=('Consolas', 9),
                                         selectmode='single', yscrollcommand=queue_scroll.set)
        self.queue_listbox.pack(fill='both', expand=True)
        queue_scroll.config(command=self.queue_listbox.yview)
        self.queue_listbox.bind('<<ListboxSelect>>', self.on_message_select)

        # Queue control buttons
        queue_btns = ttk.Frame(queue_frame)
        queue_btns.pack(fill='x', pady=5)

        ttk.Button(queue_btns, text="Add", command=self.add_message, width=6).pack(side='left', padx=2)
        ttk.Button(queue_btns, text="Delete", command=self.delete_message, width=6).pack(side='left', padx=2)
        ttk.Button(queue_btns, text="Up", command=lambda: self.move_message(-1), width=4).pack(side='left', padx=2)
        ttk.Button(queue_btns, text="Down", command=lambda: self.move_message(1), width=5).pack(side='left', padx=2)
        ttk.Button(queue_btns, text="Toggle", command=self.toggle_message, width=6).pack(side='left', padx=2)

        # Transmission frame
        tx_frame = ttk.LabelFrame(left_frame, text="Transmission", padding=5)
        tx_frame.pack(fill='x', padx=5, pady=5)

        tx_row1 = ttk.Frame(tx_frame)
        tx_row1.pack(fill='x')
        ttk.Button(tx_row1, text="Send Once", command=self.send_once).pack(side='left', padx=5)
        self.cyclic_btn = ttk.Button(tx_row1, text="Start Cyclic", command=self.toggle_cyclic)
        self.cyclic_btn.pack(side='left', padx=5)

        tx_row2 = ttk.Frame(tx_frame)
        tx_row2.pack(fill='x', pady=5)
        ttk.Label(tx_row2, text="Interval ms:").pack(side='left')
        self.interval_var = tk.StringVar(value="100")
        ttk.Entry(tx_row2, textvariable=self.interval_var, width=6).pack(side='left', padx=5)
        self.tx_status = ttk.Label(tx_row2, text="")
        self.tx_status.pack(side='left', padx=10)

        # === RIGHT PANEL ===
        # Message Editor frame
        editor_frame = ttk.LabelFrame(right_frame, text="Message Editor", padding=10)
        editor_frame.pack(fill='x', padx=5, pady=5)

        # ID and Name row
        row1 = ttk.Frame(editor_frame)
        row1.pack(fill='x', pady=2)

        ttk.Label(row1, text="ID (hex):").pack(side='left')
        self.id_var = tk.StringVar(value="100")
        ttk.Entry(row1, textvariable=self.id_var, width=8).pack(side='left', padx=5)

        ttk.Label(row1, text="Name:").pack(side='left', padx=(20,0))
        self.name_var = tk.StringVar(value="New Message")
        ttk.Entry(row1, textvariable=self.name_var, width=20).pack(side='left', padx=5)

        ttk.Label(row1, text="DLC:").pack(side='left', padx=(20,0))
        self.dlc_var = tk.StringVar(value="8")
        dlc_spin = ttk.Spinbox(row1, textvariable=self.dlc_var, from_=0, to=8, width=3)
        dlc_spin.pack(side='left', padx=5)

        # Data bytes row
        row2 = ttk.Frame(editor_frame)
        row2.pack(fill='x', pady=5)

        ttk.Label(row2, text="Data (hex):").pack(side='left')
        self.data_entries = []
        for i in range(8):
            lbl = ttk.Label(row2, text=f"B{i}:", font=('Consolas', 8))
            lbl.pack(side='left', padx=(10,0))
            entry = ttk.Entry(row2, width=4, font=('Consolas', 10))
            entry.insert(0, "00")
            entry.pack(side='left', padx=2)
            self.data_entries.append(entry)

        # Apply button
        row3 = ttk.Frame(editor_frame)
        row3.pack(fill='x', pady=5)
        ttk.Button(row3, text="Apply Changes", command=self.apply_message_changes).pack(side='left')
        ttk.Button(row3, text="Send This Message", command=self.send_selected).pack(side='left', padx=10)

        # Preview
        ttk.Label(row3, text="Preview:").pack(side='left', padx=(20,5))
        self.preview_var = tk.StringVar(value="CAN:100:8:0000000000000000")
        ttk.Label(row3, textvariable=self.preview_var, font=('Consolas', 9), foreground='blue').pack(side='left')

        # Bind data entries to update preview
        for entry in self.data_entries:
            entry.bind('<KeyRelease>', self.update_preview)
        self.id_var.trace_add('write', lambda *_: self.update_preview())
        self.dlc_var.trace_add('write', lambda *_: self.update_preview())

        # Real-time Log frame
        log_frame = ttk.LabelFrame(right_frame, text="Real-time Log", padding=5)
        log_frame.pack(fill='both', expand=True, padx=5, pady=5)

        # Log controls
        log_controls = ttk.Frame(log_frame)
        log_controls.pack(fill='x')
        ttk.Button(log_controls, text="Clear Log", command=self.clear_log).pack(side='left')
        self.autoscroll_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(log_controls, text="Auto-scroll", variable=self.autoscroll_var).pack(side='left', padx=10)
        self.log_count_label = ttk.Label(log_controls, text="Messages: 0")
        self.log_count_label.pack(side='right')

        # Log text with scrollbar
        log_scroll = ttk.Scrollbar(log_frame)
        log_scroll.pack(side='right', fill='y')

        self.log_text = tk.Text(log_frame, height=15, font=('Consolas', 9),
                                 state='disabled', yscrollcommand=log_scroll.set)
        self.log_text.pack(fill='both', expand=True)
        log_scroll.config(command=self.log_text.yview)

        # Configure log colors
        self.log_text.tag_config('timestamp', foreground='gray')
        self.log_text.tag_config('id', foreground='blue', font=('Consolas', 9, 'bold'))
        self.log_text.tag_config('data', foreground='green')
        self.log_text.tag_config('raw', foreground='purple')

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [f"{p.device} - {p.description}" for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.current(0)

    def toggle_connection(self):
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
                self.status_label.config(text="Connected", foreground='green')
            else:
                messagebox.showerror("Connection Error", msg)

    def on_load_protocol(self):
        proto = self.protocol_var.get()
        self.emulator.load_protocol(proto)
        self.refresh_queue_list()
        if self.emulator.messages:
            self.queue_listbox.selection_set(0)
            self.on_message_select(None)

    def load_protocol(self, name):
        self.protocol_var.set(name)
        self.on_load_protocol()

    def refresh_queue_list(self):
        self.queue_listbox.delete(0, tk.END)
        for i, msg in enumerate(self.emulator.messages):
            status = "+" if msg.enabled else "-"
            display = f"{status} 0x{msg.id:03X} [{msg.dlc}] {msg.name}"
            self.queue_listbox.insert(tk.END, display)
            if not msg.enabled:
                self.queue_listbox.itemconfig(i, foreground='gray')

    def on_message_select(self, event):
        selection = self.queue_listbox.curselection()
        if not selection:
            return
        idx = selection[0]
        self.selected_msg_idx = idx
        msg = self.emulator.messages[idx]

        # Update editor
        self.id_var.set(f"{msg.id:X}")
        self.name_var.set(msg.name)
        self.dlc_var.set(str(msg.dlc))

        for i, entry in enumerate(self.data_entries):
            entry.delete(0, tk.END)
            if i < len(msg.data):
                entry.insert(0, f"{msg.data[i]:02X}")
            else:
                entry.insert(0, "00")

        self.update_preview()

    def add_message(self):
        msg = CANMessage()
        self.emulator.messages.append(msg)
        self.refresh_queue_list()
        # Select the new message
        self.queue_listbox.selection_clear(0, tk.END)
        self.queue_listbox.selection_set(len(self.emulator.messages) - 1)
        self.on_message_select(None)

    def delete_message(self):
        if self.selected_msg_idx is None:
            return
        if len(self.emulator.messages) <= 1:
            messagebox.showwarning("Warning", "Cannot delete the last message")
            return
        del self.emulator.messages[self.selected_msg_idx]
        self.refresh_queue_list()
        self.selected_msg_idx = None

    def move_message(self, direction):
        if self.selected_msg_idx is None:
            return
        idx = self.selected_msg_idx
        new_idx = idx + direction
        if 0 <= new_idx < len(self.emulator.messages):
            msgs = self.emulator.messages
            msgs[idx], msgs[new_idx] = msgs[new_idx], msgs[idx]
            self.selected_msg_idx = new_idx
            self.refresh_queue_list()
            self.queue_listbox.selection_set(new_idx)

    def toggle_message(self):
        if self.selected_msg_idx is None:
            return
        msg = self.emulator.messages[self.selected_msg_idx]
        msg.enabled = not msg.enabled
        self.refresh_queue_list()
        self.queue_listbox.selection_set(self.selected_msg_idx)

    def apply_message_changes(self):
        if self.selected_msg_idx is None:
            messagebox.showwarning("Warning", "Select a message first")
            return

        try:
            msg = self.emulator.messages[self.selected_msg_idx]
            msg.id = int(self.id_var.get(), 16)
            msg.name = self.name_var.get()
            msg.dlc = int(self.dlc_var.get())

            new_data = []
            for i in range(msg.dlc):
                val = self.data_entries[i].get().strip()
                new_data.append(int(val, 16) if val else 0)
            msg.data = new_data

            self.refresh_queue_list()
            self.queue_listbox.selection_set(self.selected_msg_idx)
        except ValueError as e:
            messagebox.showerror("Error", f"Invalid value: {e}")

    def update_preview(self, event=None):
        try:
            can_id = int(self.id_var.get(), 16)
            dlc = int(self.dlc_var.get())
            data_hex = ''.join(
                self.data_entries[i].get().strip().zfill(2).upper()[:2]
                for i in range(dlc)
            )
            self.preview_var.set(f"CAN:{can_id:03X}:{dlc}:{data_hex}")
        except ValueError:
            self.preview_var.set("Invalid input")

    def send_selected(self):
        if not self.emulator.is_connected():
            messagebox.showwarning("Not connected", "Please connect first")
            return
        if self.selected_msg_idx is not None:
            msg = self.emulator.messages[self.selected_msg_idx]
            self.emulator.send_message(msg)

    def send_once(self):
        if not self.emulator.is_connected():
            messagebox.showwarning("Not connected", "Please connect first")
            return
        self.emulator.send_all_enabled()

    def toggle_cyclic(self):
        if not self.emulator.is_connected():
            messagebox.showwarning("Not connected", "Please connect first")
            return

        if self.emulator.running:
            self.emulator.stop_cyclic()
            self.cyclic_btn.config(text="Start Cyclic")
            self.tx_status.config(text="Stopped", foreground='gray')
        else:
            try:
                interval = int(self.interval_var.get()) / 1000.0
            except ValueError:
                interval = 0.1
            self.emulator.start_cyclic(interval)
            self.cyclic_btn.config(text="Stop Cyclic")
            self.tx_status.config(text="Running...", foreground='green')

    def log_message(self, msg):
        self.log_counter += 1
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        raw = msg.to_serial_format()
        hex_data = ' '.join(f'{b:02X}' for b in msg.data[:msg.dlc])

        self.log_text.config(state='normal')
        self.log_text.insert(tk.END, f"[{timestamp}] ", 'timestamp')
        self.log_text.insert(tk.END, f"0x{msg.id:03X} ", 'id')
        self.log_text.insert(tk.END, f"[{msg.dlc}] ", '')
        self.log_text.insert(tk.END, f"{hex_data} ", 'data')
        self.log_text.insert(tk.END, f"| {raw}\n", 'raw')
        self.log_text.config(state='disabled')

        if self.autoscroll_var.get():
            self.log_text.see(tk.END)

        self.log_count_label.config(text=f"Messages: {self.log_counter}")

    def clear_log(self):
        self.log_text.config(state='normal')
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state='disabled')
        self.log_counter = 0
        self.log_count_label.config(text="Messages: 0")

    def run(self):
        self.root.mainloop()
        self.emulator.disconnect()


def main():
    app = EmulatorGUI()
    app.run()


if __name__ == '__main__':
    main()
