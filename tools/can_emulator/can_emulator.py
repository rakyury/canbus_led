#!/usr/bin/env python3
"""
CAN Bus Message Emulator for CANBUS LED Controller.

Desktop application to test the ESP32 CAN LED controller without real vehicle data.
Supports Custom Protocol, Link ECU Generic Dashboard, and Link ECU Generic Dashboard 2.

Requirements:
    pip install python-can

Usage:
    python can_emulator.py
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, filedialog
import threading
import time
import logging
import json
import os
from datetime import datetime
from typing import Dict, List, Optional, Any
from dataclasses import dataclass, asdict
from copy import deepcopy

try:
    import can
    CAN_AVAILABLE = True
except ImportError:
    CAN_AVAILABLE = False
    print("Warning: python-can not installed. Install with: pip install python-can")

from protocols import (
    CANMessage, MessageField, PROTOCOLS,
    get_protocol_messages, get_protocol_names
)


# Logging configuration
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s.%(msecs)03d [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)


@dataclass
class MessageState:
    """Runtime state for a CAN message."""
    message: CANMessage
    enabled: bool = True
    interval_ms: int = 100
    values: Dict[str, float] = None
    order: int = 0

    def __post_init__(self):
        if self.values is None:
            self.values = {f.name: f.default_value for f in self.message.fields}


class TextHandler(logging.Handler):
    """Logging handler that outputs to a tkinter Text widget."""

    def __init__(self, text_widget: scrolledtext.ScrolledText):
        super().__init__()
        self.text_widget = text_widget

    def emit(self, record):
        msg = self.format(record)
        self.text_widget.after(0, self._append, msg)

    def _append(self, msg):
        self.text_widget.configure(state='normal')
        self.text_widget.insert(tk.END, msg + '\n')
        self.text_widget.see(tk.END)
        self.text_widget.configure(state='disabled')
        # Limit log size
        lines = int(self.text_widget.index('end-1c').split('.')[0])
        if lines > 1000:
            self.text_widget.configure(state='normal')
            self.text_widget.delete('1.0', '500.0')
            self.text_widget.configure(state='disabled')


class CANEmulatorApp:
    """Main CAN Emulator Application."""

    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("CAN Bus Emulator - CANBUS LED Controller")
        self.root.geometry("1400x900")
        self.root.minsize(1200, 700)

        # State
        self.can_bus: Optional[can.Bus] = None
        self.sending = False
        self.send_thread: Optional[threading.Thread] = None
        self.message_states: Dict[int, MessageState] = {}
        self.current_protocol = "Custom Protocol"
        self.send_sequence: List[int] = []  # Message IDs in send order

        # Statistics
        self.messages_sent = 0
        self.last_send_time = 0

        # Build UI
        self._create_menu()
        self._create_main_layout()
        self._setup_logging()
        self._load_protocol(self.current_protocol)

        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _create_menu(self):
        """Create menu bar."""
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        # File menu
        file_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="Save Configuration...", command=self._save_config)
        file_menu.add_command(label="Load Configuration...", command=self._load_config)
        file_menu.add_separator()
        file_menu.add_command(label="Export Log...", command=self._export_log)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self._on_close)

        # Presets menu
        presets_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Presets", menu=presets_menu)
        presets_menu.add_command(label="Idle Engine", command=lambda: self._apply_preset("idle"))
        presets_menu.add_command(label="Cruising", command=lambda: self._apply_preset("cruise"))
        presets_menu.add_command(label="Hard Acceleration", command=lambda: self._apply_preset("acceleration"))
        presets_menu.add_command(label="Rev Limiter", command=lambda: self._apply_preset("rev_limiter"))
        presets_menu.add_command(label="Cold Start", command=lambda: self._apply_preset("cold_start"))
        presets_menu.add_command(label="Oil Pressure Warning", command=lambda: self._apply_preset("oil_warning"))

        # Help menu
        help_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Help", menu=help_menu)
        help_menu.add_command(label="About", command=self._show_about)

    def _create_main_layout(self):
        """Create main application layout."""
        # Main container with paned windows
        main_paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        main_paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Left panel - Connection and controls
        left_frame = ttk.Frame(main_paned, width=350)
        main_paned.add(left_frame, weight=0)

        # Middle panel - Message configuration
        middle_frame = ttk.Frame(main_paned)
        main_paned.add(middle_frame, weight=1)

        # Right panel - Sequence and log
        right_frame = ttk.Frame(main_paned, width=400)
        main_paned.add(right_frame, weight=0)

        # Build each panel
        self._create_connection_panel(left_frame)
        self._create_message_panel(middle_frame)
        self._create_sequence_log_panel(right_frame)

    def _create_connection_panel(self, parent):
        """Create CAN connection controls."""
        # Connection frame
        conn_frame = ttk.LabelFrame(parent, text="CAN Connection", padding=10)
        conn_frame.pack(fill=tk.X, padx=5, pady=5)

        # Interface type
        ttk.Label(conn_frame, text="Interface:").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.interface_var = tk.StringVar(value="slcan")
        interfaces = ["slcan", "socketcan", "pcan", "kvaser", "vector", "virtual"]
        self.interface_combo = ttk.Combobox(conn_frame, textvariable=self.interface_var, values=interfaces, width=15)
        self.interface_combo.grid(row=0, column=1, sticky=tk.EW, pady=2, padx=5)

        # Channel/Port
        ttk.Label(conn_frame, text="Channel:").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.channel_var = tk.StringVar(value="/dev/ttyUSB0")
        self.channel_entry = ttk.Entry(conn_frame, textvariable=self.channel_var, width=18)
        self.channel_entry.grid(row=1, column=1, sticky=tk.EW, pady=2, padx=5)

        # Bitrate
        ttk.Label(conn_frame, text="Bitrate:").grid(row=2, column=0, sticky=tk.W, pady=2)
        self.bitrate_var = tk.StringVar(value="1000000")
        bitrates = ["125000", "250000", "500000", "1000000"]
        self.bitrate_combo = ttk.Combobox(conn_frame, textvariable=self.bitrate_var, values=bitrates, width=15)
        self.bitrate_combo.grid(row=2, column=1, sticky=tk.EW, pady=2, padx=5)

        conn_frame.columnconfigure(1, weight=1)

        # Connection buttons
        btn_frame = ttk.Frame(conn_frame)
        btn_frame.grid(row=3, column=0, columnspan=2, pady=10)

        self.connect_btn = ttk.Button(btn_frame, text="Connect", command=self._connect)
        self.connect_btn.pack(side=tk.LEFT, padx=5)

        self.disconnect_btn = ttk.Button(btn_frame, text="Disconnect", command=self._disconnect, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=5)

        # Status
        self.status_var = tk.StringVar(value="Disconnected")
        self.status_label = ttk.Label(conn_frame, textvariable=self.status_var, foreground="red")
        self.status_label.grid(row=4, column=0, columnspan=2, pady=5)

        # Protocol selection frame
        proto_frame = ttk.LabelFrame(parent, text="Protocol", padding=10)
        proto_frame.pack(fill=tk.X, padx=5, pady=5)

        self.protocol_var = tk.StringVar(value=self.current_protocol)
        for proto in get_protocol_names():
            rb = ttk.Radiobutton(proto_frame, text=proto, variable=self.protocol_var,
                                value=proto, command=self._on_protocol_change)
            rb.pack(anchor=tk.W, pady=2)

        # Transmission controls
        tx_frame = ttk.LabelFrame(parent, text="Transmission", padding=10)
        tx_frame.pack(fill=tk.X, padx=5, pady=5)

        # Global interval
        ttk.Label(tx_frame, text="Global Interval (ms):").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.global_interval_var = tk.StringVar(value="100")
        self.global_interval_spin = ttk.Spinbox(tx_frame, from_=10, to=5000,
                                                 textvariable=self.global_interval_var, width=10)
        self.global_interval_spin.grid(row=0, column=1, sticky=tk.W, pady=2, padx=5)

        ttk.Button(tx_frame, text="Apply to All", command=self._apply_global_interval).grid(
            row=0, column=2, padx=5)

        # Start/Stop buttons
        btn_frame2 = ttk.Frame(tx_frame)
        btn_frame2.grid(row=1, column=0, columnspan=3, pady=10)

        self.start_btn = ttk.Button(btn_frame2, text="Start Sending", command=self._start_sending)
        self.start_btn.pack(side=tk.LEFT, padx=5)

        self.stop_btn = ttk.Button(btn_frame2, text="Stop", command=self._stop_sending, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=5)

        # Send single message
        self.send_once_btn = ttk.Button(btn_frame2, text="Send Once", command=self._send_once)
        self.send_once_btn.pack(side=tk.LEFT, padx=5)

        # Statistics
        stats_frame = ttk.LabelFrame(parent, text="Statistics", padding=10)
        stats_frame.pack(fill=tk.X, padx=5, pady=5)

        self.stats_var = tk.StringVar(value="Messages sent: 0\nRate: 0 msg/s")
        ttk.Label(stats_frame, textvariable=self.stats_var, justify=tk.LEFT).pack(anchor=tk.W)

        ttk.Button(stats_frame, text="Reset Stats", command=self._reset_stats).pack(anchor=tk.W, pady=5)

    def _create_message_panel(self, parent):
        """Create message configuration panel."""
        # Protocol messages frame with canvas for scrolling
        msg_frame = ttk.LabelFrame(parent, text="Message Configuration", padding=5)
        msg_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Create canvas with scrollbar
        canvas = tk.Canvas(msg_frame, highlightthickness=0)
        scrollbar = ttk.Scrollbar(msg_frame, orient=tk.VERTICAL, command=canvas.yview)
        self.messages_frame = ttk.Frame(canvas)

        self.messages_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=self.messages_frame, anchor=tk.NW)
        canvas.configure(yscrollcommand=scrollbar.set)

        # Mouse wheel scrolling
        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        canvas.bind_all("<MouseWheel>", _on_mousewheel)

        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.message_canvas = canvas
        self.message_widgets: Dict[int, Dict[str, Any]] = {}

    def _create_sequence_log_panel(self, parent):
        """Create sequence ordering and log panel."""
        # Sequence frame
        seq_frame = ttk.LabelFrame(parent, text="Send Sequence (drag to reorder)", padding=5)
        seq_frame.pack(fill=tk.BOTH, expand=False, padx=5, pady=5)

        # Sequence listbox with scrollbar
        seq_scroll = ttk.Scrollbar(seq_frame, orient=tk.VERTICAL)
        self.sequence_listbox = tk.Listbox(seq_frame, height=10, selectmode=tk.SINGLE,
                                           yscrollcommand=seq_scroll.set)
        seq_scroll.config(command=self.sequence_listbox.yview)

        self.sequence_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        seq_scroll.pack(side=tk.RIGHT, fill=tk.Y)

        # Sequence buttons
        seq_btn_frame = ttk.Frame(parent)
        seq_btn_frame.pack(fill=tk.X, padx=5)

        ttk.Button(seq_btn_frame, text="Move Up", command=self._move_sequence_up).pack(side=tk.LEFT, padx=2)
        ttk.Button(seq_btn_frame, text="Move Down", command=self._move_sequence_down).pack(side=tk.LEFT, padx=2)
        ttk.Button(seq_btn_frame, text="Reset Order", command=self._reset_sequence).pack(side=tk.LEFT, padx=2)

        # Log frame
        log_frame = ttk.LabelFrame(parent, text="Message Log", padding=5)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=20, state='disabled',
                                                   font=('Consolas', 9))
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Log controls
        log_btn_frame = ttk.Frame(parent)
        log_btn_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(log_btn_frame, text="Clear Log", command=self._clear_log).pack(side=tk.LEFT, padx=2)

        self.autoscroll_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(log_btn_frame, text="Auto-scroll", variable=self.autoscroll_var).pack(side=tk.LEFT, padx=5)

    def _setup_logging(self):
        """Setup logging to text widget."""
        handler = TextHandler(self.log_text)
        handler.setFormatter(logging.Formatter('%(asctime)s.%(msecs)03d %(message)s', '%H:%M:%S'))
        logger.addHandler(handler)

    def _load_protocol(self, protocol_name: str):
        """Load messages for selected protocol."""
        # Clear existing widgets
        for widget in self.messages_frame.winfo_children():
            widget.destroy()
        self.message_widgets.clear()
        self.message_states.clear()
        self.send_sequence.clear()

        messages = get_protocol_messages(protocol_name)

        for idx, msg in enumerate(messages):
            state = MessageState(message=msg, order=idx)
            self.message_states[msg.id] = state
            self.send_sequence.append(msg.id)
            self._create_message_widget(msg, idx)

        self._update_sequence_listbox()
        self.current_protocol = protocol_name

    def _create_message_widget(self, msg: CANMessage, row: int):
        """Create widget for a single CAN message."""
        frame = ttk.LabelFrame(self.messages_frame, text=f"0x{msg.id:03X} - {msg.name}", padding=5)
        frame.pack(fill=tk.X, padx=5, pady=3)

        widgets = {'frame': frame, 'fields': {}}

        # Header row with enable checkbox and interval
        header = ttk.Frame(frame)
        header.pack(fill=tk.X)

        # Enable checkbox
        enable_var = tk.BooleanVar(value=True)
        enable_cb = ttk.Checkbutton(header, text="Enabled", variable=enable_var,
                                    command=lambda: self._on_message_enable(msg.id, enable_var.get()))
        enable_cb.pack(side=tk.LEFT)
        widgets['enabled'] = enable_var

        # Interval
        ttk.Label(header, text="  Interval (ms):").pack(side=tk.LEFT)
        interval_var = tk.StringVar(value="100")
        interval_spin = ttk.Spinbox(header, from_=10, to=5000, width=6, textvariable=interval_var,
                                     command=lambda: self._on_interval_change(msg.id, interval_var.get()))
        interval_spin.pack(side=tk.LEFT, padx=5)
        widgets['interval'] = interval_var

        # Description
        ttk.Label(header, text=f"  ({msg.description})", foreground='gray').pack(side=tk.LEFT, padx=10)

        # Fields
        fields_frame = ttk.Frame(frame)
        fields_frame.pack(fill=tk.X, pady=5)

        col = 0
        for field in msg.fields:
            field_frame = ttk.Frame(fields_frame)
            field_frame.grid(row=col // 4, column=col % 4, padx=5, pady=2, sticky=tk.W)

            ttk.Label(field_frame, text=f"{field.name}:").pack(side=tk.LEFT)

            if field.max_value <= 1 and field.min_value >= 0:
                # Boolean field - use checkbox
                var = tk.BooleanVar(value=bool(field.default_value))
                widget = ttk.Checkbutton(field_frame, variable=var,
                                          command=lambda f=field, v=var, m=msg: self._on_field_change(m.id, f.name, float(v.get())))
            else:
                # Numeric field - use scale and spinbox
                var = tk.DoubleVar(value=field.default_value)

                # Scale widget
                scale = ttk.Scale(field_frame, from_=field.min_value, to=field.max_value,
                                  variable=var, orient=tk.HORIZONTAL, length=100,
                                  command=lambda v, f=field, m=msg: self._on_field_change(m.id, f.name, float(v)))
                scale.pack(side=tk.LEFT, padx=2)

                # Spinbox for precise control
                step = 1 if field.scale == 1 else 0.1
                spin = ttk.Spinbox(field_frame, from_=field.min_value, to=field.max_value,
                                   textvariable=var, width=6, increment=step,
                                   command=lambda f=field, v=var, m=msg: self._on_field_change(m.id, f.name, v.get()))
                spin.pack(side=tk.LEFT, padx=2)
                widget = spin

            # Unit label
            if field.unit:
                ttk.Label(field_frame, text=field.unit, foreground='gray').pack(side=tk.LEFT)

            widgets['fields'][field.name] = var
            col += 1

        # Preview - show raw CAN data
        preview_frame = ttk.Frame(frame)
        preview_frame.pack(fill=tk.X)

        ttk.Label(preview_frame, text="Raw Data:", foreground='gray').pack(side=tk.LEFT)
        preview_var = tk.StringVar(value="")
        preview_label = ttk.Label(preview_frame, textvariable=preview_var, font=('Consolas', 9))
        preview_label.pack(side=tk.LEFT, padx=5)
        widgets['preview'] = preview_var

        self.message_widgets[msg.id] = widgets
        self._update_preview(msg.id)

    def _on_field_change(self, msg_id: int, field_name: str, value: float):
        """Handle field value change."""
        if msg_id in self.message_states:
            self.message_states[msg_id].values[field_name] = value
            self._update_preview(msg_id)

    def _on_message_enable(self, msg_id: int, enabled: bool):
        """Handle message enable/disable."""
        if msg_id in self.message_states:
            self.message_states[msg_id].enabled = enabled

    def _on_interval_change(self, msg_id: int, interval_str: str):
        """Handle interval change."""
        try:
            interval = int(interval_str)
            if msg_id in self.message_states:
                self.message_states[msg_id].interval_ms = interval
        except ValueError:
            pass

    def _update_preview(self, msg_id: int):
        """Update the raw data preview for a message."""
        if msg_id not in self.message_states or msg_id not in self.message_widgets:
            return

        state = self.message_states[msg_id]
        data = state.message.build_data(state.values)
        hex_str = ' '.join(f'{b:02X}' for b in data)
        self.message_widgets[msg_id]['preview'].set(hex_str)

    def _update_sequence_listbox(self):
        """Update the sequence listbox."""
        self.sequence_listbox.delete(0, tk.END)
        for msg_id in self.send_sequence:
            if msg_id in self.message_states:
                msg = self.message_states[msg_id].message
                enabled = self.message_states[msg_id].enabled
                prefix = "[x]" if enabled else "[ ]"
                self.sequence_listbox.insert(tk.END, f"{prefix} 0x{msg_id:03X} - {msg.name}")

    def _on_protocol_change(self):
        """Handle protocol selection change."""
        if self.sending:
            self._stop_sending()
        self._load_protocol(self.protocol_var.get())

    def _apply_global_interval(self):
        """Apply global interval to all messages."""
        try:
            interval = int(self.global_interval_var.get())
            for msg_id, state in self.message_states.items():
                state.interval_ms = interval
                if msg_id in self.message_widgets:
                    self.message_widgets[msg_id]['interval'].set(str(interval))
        except ValueError:
            messagebox.showerror("Error", "Invalid interval value")

    def _move_sequence_up(self):
        """Move selected message up in sequence."""
        sel = self.sequence_listbox.curselection()
        if sel and sel[0] > 0:
            idx = sel[0]
            self.send_sequence[idx], self.send_sequence[idx - 1] = \
                self.send_sequence[idx - 1], self.send_sequence[idx]
            self._update_sequence_listbox()
            self.sequence_listbox.selection_set(idx - 1)

    def _move_sequence_down(self):
        """Move selected message down in sequence."""
        sel = self.sequence_listbox.curselection()
        if sel and sel[0] < len(self.send_sequence) - 1:
            idx = sel[0]
            self.send_sequence[idx], self.send_sequence[idx + 1] = \
                self.send_sequence[idx + 1], self.send_sequence[idx]
            self._update_sequence_listbox()
            self.sequence_listbox.selection_set(idx + 1)

    def _reset_sequence(self):
        """Reset sequence to default order."""
        messages = get_protocol_messages(self.current_protocol)
        self.send_sequence = [msg.id for msg in messages]
        self._update_sequence_listbox()

    def _connect(self):
        """Connect to CAN bus."""
        if not CAN_AVAILABLE:
            messagebox.showerror("Error",
                "python-can library not installed.\n"
                "Install with: pip install python-can")
            return

        interface = self.interface_var.get()
        channel = self.channel_var.get()
        bitrate = int(self.bitrate_var.get())

        try:
            if interface == "virtual":
                self.can_bus = can.Bus(interface='virtual', channel=channel)
            elif interface == "slcan":
                self.can_bus = can.Bus(interface='slcan', channel=channel, bitrate=bitrate)
            elif interface == "socketcan":
                self.can_bus = can.Bus(interface='socketcan', channel=channel)
            else:
                self.can_bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)

            self.status_var.set("Connected")
            self.status_label.config(foreground="green")
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            logger.info(f"Connected to {interface} on {channel} @ {bitrate} bps")

        except Exception as e:
            messagebox.showerror("Connection Error", str(e))
            logger.error(f"Connection failed: {e}")

    def _disconnect(self):
        """Disconnect from CAN bus."""
        if self.sending:
            self._stop_sending()

        if self.can_bus:
            try:
                self.can_bus.shutdown()
            except Exception:
                pass
            self.can_bus = None

        self.status_var.set("Disconnected")
        self.status_label.config(foreground="red")
        self.connect_btn.config(state=tk.NORMAL)
        self.disconnect_btn.config(state=tk.DISABLED)
        logger.info("Disconnected from CAN bus")

    def _start_sending(self):
        """Start cyclic message transmission."""
        if not self.can_bus:
            messagebox.showwarning("Warning", "Not connected to CAN bus")
            return

        self.sending = True
        self.start_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)

        self.send_thread = threading.Thread(target=self._send_loop, daemon=True)
        self.send_thread.start()
        logger.info("Started cyclic transmission")

    def _stop_sending(self):
        """Stop cyclic message transmission."""
        self.sending = False
        if self.send_thread:
            self.send_thread.join(timeout=1)
            self.send_thread = None

        self.start_btn.config(state=tk.NORMAL)
        self.stop_btn.config(state=tk.DISABLED)
        logger.info("Stopped cyclic transmission")

    def _send_loop(self):
        """Background thread for cyclic message sending."""
        last_send_times: Dict[int, float] = {msg_id: 0 for msg_id in self.send_sequence}

        while self.sending:
            now = time.time() * 1000  # Current time in ms

            for msg_id in self.send_sequence:
                if not self.sending:
                    break

                state = self.message_states.get(msg_id)
                if not state or not state.enabled:
                    continue

                # Check if it's time to send this message
                if now - last_send_times[msg_id] >= state.interval_ms:
                    self._send_message(state)
                    last_send_times[msg_id] = now

            # Small sleep to prevent CPU spinning
            time.sleep(0.001)

    def _send_message(self, state: MessageState):
        """Send a single CAN message."""
        if not self.can_bus:
            return

        try:
            data = state.message.build_data(state.values)
            msg = can.Message(
                arbitration_id=state.message.id,
                data=data,
                is_extended_id=False
            )
            self.can_bus.send(msg)

            self.messages_sent += 1
            hex_data = ' '.join(f'{b:02X}' for b in data)
            logger.info(f"TX 0x{state.message.id:03X} [{len(data)}] {hex_data}")

            # Update stats in main thread
            self.root.after(0, self._update_stats)

        except Exception as e:
            logger.error(f"Send error: {e}")

    def _send_once(self):
        """Send all enabled messages once."""
        if not self.can_bus:
            messagebox.showwarning("Warning", "Not connected to CAN bus")
            return

        for msg_id in self.send_sequence:
            state = self.message_states.get(msg_id)
            if state and state.enabled:
                self._send_message(state)

    def _update_stats(self):
        """Update statistics display."""
        now = time.time()
        if self.last_send_time > 0:
            elapsed = now - self.last_send_time
            if elapsed > 0:
                rate = 1 / elapsed if elapsed < 1 else self.messages_sent / elapsed
            else:
                rate = 0
        else:
            rate = 0

        self.last_send_time = now
        self.stats_var.set(f"Messages sent: {self.messages_sent}\nRate: {rate:.1f} msg/s")

    def _reset_stats(self):
        """Reset statistics."""
        self.messages_sent = 0
        self.last_send_time = 0
        self.stats_var.set("Messages sent: 0\nRate: 0 msg/s")

    def _clear_log(self):
        """Clear the log text widget."""
        self.log_text.configure(state='normal')
        self.log_text.delete('1.0', tk.END)
        self.log_text.configure(state='disabled')

    def _apply_preset(self, preset_name: str):
        """Apply a preset configuration."""
        presets = {
            "idle": {
                "rpm": 800, "throttle": 0, "tps": 0, "coolant": 85,
                "oil_pressure": 3.5, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 0, "als_active": 0, "speed": 0, "gear": 0
            },
            "cruise": {
                "rpm": 3000, "throttle": 25, "tps": 25, "coolant": 90,
                "oil_pressure": 4.5, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 0, "als_active": 0, "speed": 80, "gear": 4
            },
            "acceleration": {
                "rpm": 5500, "throttle": 100, "tps": 100, "coolant": 95,
                "oil_pressure": 5.0, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 0, "als_active": 0, "speed": 120, "gear": 3
            },
            "rev_limiter": {
                "rpm": 6500, "throttle": 100, "tps": 100, "coolant": 100,
                "oil_pressure": 5.5, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 1, "als_active": 0, "speed": 150, "gear": 4
            },
            "cold_start": {
                "rpm": 1200, "throttle": 0, "tps": 0, "coolant": 20,
                "oil_pressure": 4.0, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 0, "als_active": 0, "speed": 0, "gear": 0
            },
            "oil_warning": {
                "rpm": 4000, "throttle": 50, "tps": 50, "coolant": 95,
                "oil_pressure": 0.5, "brake": 0, "clutch": 0, "ignition": 1,
                "rev_limiter": 0, "als_active": 0, "speed": 100, "gear": 3
            },
        }

        preset = presets.get(preset_name, {})
        if not preset:
            return

        for msg_id, state in self.message_states.items():
            for field in state.message.fields:
                if field.name in preset:
                    value = preset[field.name]
                    state.values[field.name] = value

                    # Update GUI
                    if msg_id in self.message_widgets:
                        field_var = self.message_widgets[msg_id]['fields'].get(field.name)
                        if field_var:
                            if isinstance(field_var, tk.BooleanVar):
                                field_var.set(bool(value))
                            else:
                                field_var.set(value)

            self._update_preview(msg_id)

        logger.info(f"Applied preset: {preset_name}")

    def _save_config(self):
        """Save current configuration to file."""
        filename = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            title="Save Configuration"
        )
        if not filename:
            return

        config = {
            "protocol": self.current_protocol,
            "interface": self.interface_var.get(),
            "channel": self.channel_var.get(),
            "bitrate": self.bitrate_var.get(),
            "sequence": self.send_sequence,
            "messages": {}
        }

        for msg_id, state in self.message_states.items():
            config["messages"][str(msg_id)] = {
                "enabled": state.enabled,
                "interval_ms": state.interval_ms,
                "values": state.values
            }

        try:
            with open(filename, 'w') as f:
                json.dump(config, f, indent=2)
            logger.info(f"Configuration saved to {filename}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save configuration: {e}")

    def _load_config(self):
        """Load configuration from file."""
        filename = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            title="Load Configuration"
        )
        if not filename:
            return

        try:
            with open(filename, 'r') as f:
                config = json.load(f)

            # Load protocol
            if "protocol" in config:
                self.protocol_var.set(config["protocol"])
                self._load_protocol(config["protocol"])

            # Load connection settings
            if "interface" in config:
                self.interface_var.set(config["interface"])
            if "channel" in config:
                self.channel_var.set(config["channel"])
            if "bitrate" in config:
                self.bitrate_var.set(config["bitrate"])

            # Load sequence
            if "sequence" in config:
                self.send_sequence = config["sequence"]
                self._update_sequence_listbox()

            # Load message settings
            if "messages" in config:
                for msg_id_str, msg_config in config["messages"].items():
                    msg_id = int(msg_id_str)
                    if msg_id in self.message_states:
                        state = self.message_states[msg_id]
                        state.enabled = msg_config.get("enabled", True)
                        state.interval_ms = msg_config.get("interval_ms", 100)
                        state.values.update(msg_config.get("values", {}))

                        # Update GUI
                        if msg_id in self.message_widgets:
                            widgets = self.message_widgets[msg_id]
                            widgets['enabled'].set(state.enabled)
                            widgets['interval'].set(str(state.interval_ms))
                            for fname, fval in state.values.items():
                                if fname in widgets['fields']:
                                    var = widgets['fields'][fname]
                                    if isinstance(var, tk.BooleanVar):
                                        var.set(bool(fval))
                                    else:
                                        var.set(fval)

                        self._update_preview(msg_id)

            logger.info(f"Configuration loaded from {filename}")

        except Exception as e:
            messagebox.showerror("Error", f"Failed to load configuration: {e}")

    def _export_log(self):
        """Export log to file."""
        filename = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            initialfile=f"can_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt",
            title="Export Log"
        )
        if not filename:
            return

        try:
            log_content = self.log_text.get('1.0', tk.END)
            with open(filename, 'w') as f:
                f.write(f"CAN Bus Emulator Log - {datetime.now()}\n")
                f.write(f"Protocol: {self.current_protocol}\n")
                f.write("-" * 60 + "\n")
                f.write(log_content)
            logger.info(f"Log exported to {filename}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to export log: {e}")

    def _show_about(self):
        """Show about dialog."""
        messagebox.showinfo(
            "About CAN Bus Emulator",
            "CAN Bus Message Emulator\n"
            "For CANBUS LED Controller\n\n"
            "Supports:\n"
            "- Custom Protocol\n"
            "- Link ECU Generic Dashboard\n"
            "- Link ECU Generic Dashboard 2\n\n"
            "Use USB-CAN adapters like:\n"
            "- SLCAN (CANable, USBtin)\n"
            "- SocketCAN (Linux)\n"
            "- PCAN, Kvaser, Vector"
        )

    def _on_close(self):
        """Handle window close."""
        if self.sending:
            self._stop_sending()
        if self.can_bus:
            self._disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = CANEmulatorApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
