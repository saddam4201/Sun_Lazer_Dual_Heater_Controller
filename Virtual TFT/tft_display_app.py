"""
Virtual 2.8" TFT Display (ILI9341 320x240) Emulator for Windows over UART / Serial
Specifically designed for Sun Lazer Dual Heater Controller Bench Testing
Features:
- Realistic 2.8" ILI9341 PCB Bezel (320x240) with silkscreen, gold pads, and status LEDs
- Interactive Navigation Buttons on PCB & Toolbar:
    [1] UP (▲), [2] DOWN (▼), [3] LEFT (◀), [4] RIGHT (▶), [5] OK (✔)
- Bench Test Controls:
    - Down Limit Switch toggle (:down on/off)
    - Home Limit Switch toggle (:home on/off)
    - Sensor Simulation: H1 Temp (:h1), H2 Temp (:h2), Torque (:torque)
    - Quick actions: Reset Failures (:reset_fail), Show Status (:show)
- Real-time Serial Command Inspector with color-coded RX/TX log and packet stats
- Keyboard hotkeys: Arrow keys, Enter/Space, and 1..5
- 1x, 2x, 3x integer scaling and snapshot capture tool
"""

import sys
import os
import time
import math
import threading
import queue
import re
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False


# ==============================================================================
# COLOR CONVERSION UTILITIES (RGB565 <-> Hex)
# ==============================================================================
def rgb565_to_hex(color_val):
    if color_val is None:
        return None
    if isinstance(color_val, str):
        color_val = color_val.strip()
        if not color_val or color_val.lower() == 'none':
            return None
        if color_val.startswith('#'):
            return color_val
        if color_val.lower().startswith('0x'):
            try:
                color_val = int(color_val, 16)
            except ValueError:
                return None
        else:
            try:
                color_val = int(color_val)
            except ValueError:
                return None

    try:
        val = int(color_val) & 0xFFFF
    except (TypeError, ValueError):
        return None

    r5 = (val >> 11) & 0x1F
    g6 = (val >> 5) & 0x3F
    b5 = val & 0x1F

    # Scale 5/6-bit to 8-bit (0-255)
    r8 = (r5 * 527 + 23) >> 6
    g8 = (g6 * 259 + 33) >> 6
    b8 = (b5 * 527 + 23) >> 6

    return f"#{r8:02X}{g8:02X}{b8:02X}"


# Built-in Standard ILI9341 Color Constants
ILI9341_BLACK       = 0x0000
ILI9341_NAVY        = 0x000F
ILI9341_DARKGREEN   = 0x03E0
ILI9341_DARKCYAN    = 0x03EF
ILI9341_MAROON      = 0x7800
ILI9341_PURPLE      = 0x780F
ILI9341_OLIVE       = 0x7BE0
ILI9341_LIGHTGREY   = 0xC618
ILI9341_DARKGREY    = 0x7BEF
ILI9341_BLUE        = 0x001F
ILI9341_GREEN       = 0x07E0
ILI9341_CYAN        = 0x07FF
ILI9341_RED         = 0xF800
ILI9341_MAGENTA     = 0xF81F
ILI9341_YELLOW      = 0xFFE0
ILI9341_WHITE       = 0xFFFF
ILI9341_ORANGE      = 0xFDA0


# ==============================================================================
# MAIN APPLICATION
# ==============================================================================
FIRMWARE_NAME = "Sun Lazer Dual Heater Controller"
APP_VERSION   = "v3.1"


class VirtualTFTApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"Virtual 2.8\" TFT (ILI9341) - {FIRMWARE_NAME} - Bench Test")
        self.configure(bg="#1E1E24")
        self.geometry("1240x820")
        self.minsize(1020, 720)

        # TFT Native dimensions
        self.native_w = 320
        self.native_h = 240
        self.rotation = 1  # 1: Landscape (320x240)
        self.scale = 2     # 2x gives 640x480

        # Serial Port state
        self.ser = None
        self.serial_thread = None
        self.is_connected = False
        self.cmd_queue = queue.Queue()
        self.rx_counter = 0
        self.fps_counter = 0
        self.last_fps_time = time.time()

        # Bench test simulation state
        self.sim_down_limit = False
        self.sim_home_limit = False
        self.sim_thread = None
        self.sim_running = False
        self.sim_stop_event = threading.Event()
        self._rx_led_active = False

        # Typography
        self.font_family = "Segoe UI"
        self.tft_font_family = "Arial"
        try:
            import tkinter.font as tkfont
            avail = tkfont.families()
            for cand in ("FreeSansBold", "FreeSans", "Arial", "Helvetica", "Segoe UI"):
                if cand in avail:
                    self.tft_font_family = cand
                    break
        except Exception:
            pass

        # Build UI layout
        self.setup_ui()

        # Clean close
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        # Periodic UI update from queue
        self.after(10, self.process_command_queue)
        self.after(1000, self.update_stats)

        # Global hotkeys (Arrow keys, 1..5, Enter, Space)
        self.bind_all("<Key>", self.on_global_key)

        # Auto-connect if COM port detected
        self.after(300, lambda: self.connect_serial(silent=True))

    # --------------------------------------------------------------------------
    # UI SETUP & STYLING
    # --------------------------------------------------------------------------
    def setup_ui(self):
        # 1. Top Primary Toolbar
        toolbar = tk.Frame(self, bg="#2A2A32", height=46, padx=12, pady=6)
        toolbar.pack(side=tk.TOP, fill=tk.X)

        brand_lbl = tk.Label(toolbar, text="SUN LAZER TFT 2.8\"", font=(self.font_family, 11, "bold"), fg="#FF4B4B", bg="#2A2A32")
        brand_lbl.pack(side=tk.LEFT, padx=(0, 14))

        # COM Port Selector
        tk.Label(toolbar, text="Port:", font=(self.font_family, 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.port_combo = ttk.Combobox(toolbar, width=12, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 6))

        refresh_btn = tk.Button(toolbar, text="↻", font=(self.font_family, 10, "bold"), fg="#FFFFFF", bg="#3E3E4A",
                                bd=0, padx=6, pady=1, command=self.refresh_com_ports)
        refresh_btn.pack(side=tk.LEFT, padx=(0, 10))

        # Baud Rate Selector
        tk.Label(toolbar, text="Baud:", font=(self.font_family, 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.baud_combo = ttk.Combobox(toolbar, width=8, state="readonly",
                                       values=["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"])
        self.baud_combo.set("115200")
        self.baud_combo.pack(side=tk.LEFT, padx=(0, 10))

        # Connect Button
        self.connect_btn = tk.Button(toolbar, text="Connect", font=(self.font_family, 9, "bold"), fg="#FFFFFF", bg="#008037",
                                     activebackground="#00A045", bd=0, padx=14, pady=3, command=self.toggle_connection)
        self.connect_btn.pack(side=tk.LEFT, padx=(0, 14))

        # Scale Factor Selector
        tk.Label(toolbar, text="Zoom:", font=(self.font_family, 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.scale_combo = ttk.Combobox(toolbar, width=4, state="readonly", values=["1x", "2x", "3x"])
        self.scale_combo.set("2x")
        self.scale_combo.bind("<<ComboboxSelected>>", self.on_scale_change)
        self.scale_combo.pack(side=tk.LEFT, padx=(0, 10))

        # Demo Mode Button
        self.sim_btn = tk.Button(toolbar, text="▶ Run Demo", font=(self.font_family, 9, "bold"), fg="#FFFFFF", bg="#6930C3",
                                 activebackground="#7400B8", bd=0, padx=10, pady=3, command=self.toggle_simulation)
        self.sim_btn.pack(side=tk.LEFT, padx=(0, 8))

        # Clear Screen button
        clr_btn = tk.Button(toolbar, text="Clear", font=(self.font_family, 9), fg="#DCDCDC", bg="#3E3E4A",
                            bd=0, padx=8, pady=3, command=lambda: self.execute_command("CLS,0x0000"))
        clr_btn.pack(side=tk.LEFT, padx=(0, 8))

        # Snapshot button
        snap_btn = tk.Button(toolbar, text="📸 Snapshot", font=(self.font_family, 9), fg="#DCDCDC", bg="#3E3E4A",
                             bd=0, padx=8, pady=3, command=self.save_snapshot)
        snap_btn.pack(side=tk.LEFT, padx=(0, 8))

        # 2. Secondary Toolbar: Bench-Testing Navigation & Sensor Controls
        bench_bar = tk.Frame(self, bg="#202028", height=42, padx=12, pady=6)
        bench_bar.pack(side=tk.TOP, fill=tk.X)

        tk.Label(bench_bar, text="🎮 Navigation:", font=(self.font_family, 9, "bold"),
                 fg="#38BDF8", bg="#202028").pack(side=tk.LEFT, padx=(0, 6))

        # Direct Navigation Buttons
        self.btn_up_gui = tk.Button(bench_bar, text="▲ UP [1]", font=(self.font_family, 8, "bold"),
                                    fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=8, pady=2,
                                    command=self.send_nav_up)
        self.btn_up_gui.pack(side=tk.LEFT, padx=(0, 4))

        self.btn_dn_gui = tk.Button(bench_bar, text="▼ DN [2]", font=(self.font_family, 8, "bold"),
                                    fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=8, pady=2,
                                    command=self.send_nav_down)
        self.btn_dn_gui.pack(side=tk.LEFT, padx=(0, 4))

        self.btn_lt_gui = tk.Button(bench_bar, text="◀ LT [3]", font=(self.font_family, 8, "bold"),
                                    fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=8, pady=2,
                                    command=self.send_nav_left)
        self.btn_lt_gui.pack(side=tk.LEFT, padx=(0, 4))

        self.btn_rt_gui = tk.Button(bench_bar, text="▶ RT [4]", font=(self.font_family, 8, "bold"),
                                    fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=8, pady=2,
                                    command=self.send_nav_right)
        self.btn_rt_gui.pack(side=tk.LEFT, padx=(0, 4))

        self.btn_ok_gui = tk.Button(bench_bar, text="START/STOP [5]", font=(self.font_family, 8, "bold"),
                                    fg="#FFFFFF", bg="#008037", activebackground="#00A045", bd=0, padx=10, pady=2,
                                    command=self.send_nav_ok)
        self.btn_ok_gui.pack(side=tk.LEFT, padx=(0, 14))

        # Bench Test Simulation Toggles
        tk.Label(bench_bar, text="⚙️ Bench Controls:", font=(self.font_family, 9, "bold"),
                 fg="#F59E0B", bg="#202028").pack(side=tk.LEFT, padx=(0, 6))

        # Down Limit Toggle
        self.btn_dn_sw = tk.Button(bench_bar, text="Down Sw: OPEN", font=(self.font_family, 8, "bold"),
                                   fg="#FFFFFF", bg="#4B5563", activebackground="#6B7280", bd=0, padx=8, pady=2,
                                   command=self.toggle_down_limit)
        self.btn_dn_sw.pack(side=tk.LEFT, padx=(0, 4))

        # Home Limit Toggle
        self.btn_hm_sw = tk.Button(bench_bar, text="Home Sw: OPEN", font=(self.font_family, 8, "bold"),
                                   fg="#FFFFFF", bg="#4B5563", activebackground="#6B7280", bd=0, padx=8, pady=2,
                                   command=self.toggle_home_limit)
        self.btn_hm_sw.pack(side=tk.LEFT, padx=(0, 8))

        # Set Temps / Torque buttons
        btn_set_t = tk.Button(bench_bar, text="Set Temps", font=(self.font_family, 8),
                              fg="#E0E0E0", bg="#374151", bd=0, padx=6, pady=2, command=self.prompt_set_temps)
        btn_set_t.pack(side=tk.LEFT, padx=(0, 4))

        btn_set_tq = tk.Button(bench_bar, text="Set Torque", font=(self.font_family, 8),
                               fg="#E0E0E0", bg="#374151", bd=0, padx=6, pady=2, command=self.prompt_set_torque)
        btn_set_tq.pack(side=tk.LEFT, padx=(0, 8))

        # Reset Failures button
        btn_rst_f = tk.Button(bench_bar, text="Reset Fails", font=(self.font_family, 8),
                              fg="#F87171", bg="#374151", bd=0, padx=6, pady=2,
                              command=lambda: self.send_serial_line(":reset_fail"))
        btn_rst_f.pack(side=tk.LEFT, padx=(0, 8))

        # Temp Sim Toggle (Default OFF)
        self.temp_sim_active = False
        self.btn_temp_sim = tk.Button(bench_bar, text="Temp Sim: OFF", font=(self.font_family, 8, "bold"),
                                      fg="#FFFFFF", bg="#4B5563", activebackground="#6B7280", bd=0, padx=8, pady=2,
                                      command=self.toggle_temp_sim)
        self.btn_temp_sim.pack(side=tk.LEFT, padx=(0, 4))

        # Main Workspace
        main_paned = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#1E1E24", bd=0, sashwidth=4)
        main_paned.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=8, pady=8)

        # Left Container: TFT PCB Bezel
        self.tft_container = tk.Frame(main_paned, bg="#18181D", bd=0)
        main_paned.add(self.tft_container, minsize=540)

        # Right Container: Serial Packet Log & Inspector
        self.inspector_frame = tk.Frame(main_paned, bg="#1E1E24", width=360)
        main_paned.add(self.inspector_frame, minsize=280)

        self.setup_inspector_ui()
        self.draw_tft_hardware()
        self.refresh_com_ports()

    # --------------------------------------------------------------------------
    # REALISTIC ILI9341 HARDWARE BEZEL WITH NAVIGATION BUTTONS
    # --------------------------------------------------------------------------
    def draw_tft_hardware(self):
        for child in self.tft_container.winfo_children():
            child.destroy()

        sw = 320 * self.scale
        sh = 240 * self.scale

        pcb_margin_x = 42
        pcb_margin_y = 48
        pcb_w = sw + (pcb_margin_x * 2)
        pcb_h = sh + (pcb_margin_y * 2) + 64

        scroll_wrapper = tk.Frame(self.tft_container, bg="#18181D")
        scroll_wrapper.pack(expand=True)

        self.pcb_canvas = tk.Canvas(scroll_wrapper, width=pcb_w, height=pcb_h, bg="#18181D",
                                    highlightthickness=0, bd=0)
        self.pcb_canvas.pack(pady=8)

        # 1. Red PCB Board
        self.pcb_canvas.create_rectangle(6, 6, pcb_w - 6, pcb_h - 6, fill="#A31414", outline="#750E0E", width=2)

        # Corner screw pads
        hole_r = 8
        pads = [(20, 20), (pcb_w - 20, 20), (20, pcb_h - 20), (pcb_w - 20, pcb_h - 20)]
        for hx, hy in pads:
            self.pcb_canvas.create_oval(hx - hole_r, hy - hole_r, hx + hole_r, hy + hole_r, fill="#D4AF37", outline="#997C22", width=1.5)
            self.pcb_canvas.create_oval(hx - 4, hy - 4, hx + 4, hy + 4, fill="#18181D", outline="")

        # 2. Silkscreen text on PCB
        self.pcb_canvas.create_text(pcb_w // 2, 18, text="2.8\" TFT ILI9341 320x240 - SUN LAZER DUAL HEATER CONTROLLER",
                                    font=("Consolas", 9, "bold"), fill="#EFEFEF")

        # 3. Metallic screen bezel outer frame
        screen_x1 = pcb_margin_x
        screen_y1 = pcb_margin_y - 10
        screen_x2 = screen_x1 + sw
        screen_y2 = screen_y1 + sh

        self.pcb_canvas.create_rectangle(screen_x1 - 6, screen_y1 - 6, screen_x2 + 6, screen_y2 + 6,
                                         fill="#2C2E33", outline="#50535B", width=2)
        self.pcb_canvas.create_rectangle(screen_x1 - 2, screen_y1 - 2, screen_x2 + 2, screen_y2 + 2,
                                         fill="#111113", outline="#1F2024", width=1)

        # 4. The Active LCD Screen Canvas
        self.tft_canvas = tk.Canvas(self.pcb_canvas, width=sw, height=sh, bg="#000000",
                                    highlightthickness=0, bd=0)
        self.pcb_canvas.create_window(screen_x1, screen_y1, anchor=tk.NW, window=self.tft_canvas)

        # 5. Interactive Navigation Tactile Pushbuttons on PCB
        btn_y = screen_y2 + 22
        btn_spacing = min(76, max(52, (pcb_w - 60) // 5))
        btn_start_x = pcb_w // 2 - int(2.0 * btn_spacing)

        nav_specs = [
            ("pcb_btn1", btn_start_x + 0 * btn_spacing, "1: UP (▲)",    self.send_nav_up,    "#38BDF8"),
            ("pcb_btn2", btn_start_x + 1 * btn_spacing, "2: DOWN (▼)",  self.send_nav_down,  "#38BDF8"),
            ("pcb_btn3", btn_start_x + 2 * btn_spacing, "3: LEFT (◀)",  self.send_nav_left,  "#38BDF8"),
            ("pcb_btn4", btn_start_x + 3 * btn_spacing, "4: RIGHT (▶)", self.send_nav_right, "#38BDF8"),
            ("pcb_btn5", btn_start_x + 4 * btn_spacing, "5: START/STOP", self.send_nav_ok, "#00FF66"),
        ]

        for tag, bx, label, cmd, hl_color in nav_specs:
            # Solder legs
            self.pcb_canvas.create_rectangle(bx - 14, btn_y - 8, bx + 14, btn_y + 8, fill="#D4AF37", outline="", tags=tag)
            # Switch metal chassis
            self.pcb_canvas.create_rectangle(bx - 12, btn_y - 12, bx + 12, btn_y + 12, fill="#9CA3AF", outline="#4B5563", width=1.5, tags=tag)
            # Actuator button
            actuator = self.pcb_canvas.create_oval(bx - 7, btn_y - 7, bx + 7, btn_y + 7, fill="#1F2937", outline="#111827", tags=tag)
            # Highlight ring
            self.pcb_canvas.create_oval(bx - 3, btn_y - 3, bx + 3, btn_y + 3, fill="#374151", outline="", tags=tag)
            # Label
            self.pcb_canvas.create_text(bx, btn_y + 18, text=label, font=("Consolas", 8, "bold"), fill="#F0F0F0", tags=tag)

            def make_handler(c=cmd, act=actuator, hl=hl_color):
                def handler(event):
                    self.pcb_canvas.itemconfig(act, fill=hl)
                    self.after(140, lambda: self.pcb_canvas.itemconfig(act, fill="#1F2937"))
                    c()
                return handler

            self.pcb_canvas.tag_bind(tag, "<Button-1>", make_handler())
            self.pcb_canvas.tag_bind(tag, "<Enter>", lambda e: self.pcb_canvas.config(cursor="hand2"))
            self.pcb_canvas.tag_bind(tag, "<Leave>", lambda e: self.pcb_canvas.config(cursor=""))

        # 6. Status LEDs (PWR and RX)
        led_y = 20
        self.pcb_canvas.create_oval(46, led_y - 5, 56, led_y + 5, fill="#00FF44", outline="#008822")
        self.pcb_canvas.create_text(68, led_y, text="PWR", font=("Segoe UI", 7, "bold"), fill="#C0C0C0", anchor=tk.W)

        self.rx_led_id = self.pcb_canvas.create_oval(pcb_w - 76, led_y - 5, pcb_w - 66, led_y + 5, fill="#1B2838", outline="#0A1828")
        self.pcb_canvas.create_text(pcb_w - 60, led_y, text="RX", font=("Segoe UI", 7, "bold"), fill="#C0C0C0", anchor=tk.W)

        # Draw default boot splash
        self.draw_splash_screen()

    def draw_splash_screen(self):
        sw = self.tft_canvas.winfo_reqwidth()
        sh = self.tft_canvas.winfo_reqheight()

        self.tft_canvas.delete("all")
        self.tft_canvas.create_rectangle(0, 0, sw, sh, fill="#000000", outline="")

        # Navy header
        self.tft_canvas.create_rectangle(0, 0, sw, 24 * self.scale, fill="#000080", outline="")
        self.tft_canvas.create_text(10 * self.scale, 12 * self.scale, text="SUN LAZER INITIALIZING...",
                                    font=(self.font_family, 10 * self.scale, "bold"), fill="#FFFFFF", anchor=tk.W)

        cx = sw // 2
        cy = sh // 2
        self.tft_canvas.create_text(cx, cy - 20, text="VIRTUAL 2.8\" TFT DISPLAY",
                                    font=("Consolas", 12 * self.scale // 2, "bold"), fill="#00FF66")
        self.tft_canvas.create_text(cx, cy + 10, text="Waiting for ESP32 UART stream...",
                                    font=("Consolas", 9 * self.scale // 2, "italic"), fill="#F59E0B")
        self.tft_canvas.create_text(cx, cy + 35, text="Nav Keys: 1=UP, 2=DN, 3=LT, 4=RT, 5=OK",
                                    font=("Consolas", 8 * self.scale // 2), fill="#94A3B8")

    # --------------------------------------------------------------------------
    # SERIAL INSPECTOR UI
    # --------------------------------------------------------------------------
    def setup_inspector_ui(self):
        hdr = tk.Frame(self.inspector_frame, bg="#26262F", padx=8, pady=6)
        hdr.pack(fill=tk.X)

        tk.Label(hdr, text="Serial Command Inspector", font=(self.font_family, 9, "bold"), fg="#FFFFFF", bg="#26262F").pack(side=tk.LEFT)

        clear_log_btn = tk.Button(hdr, text="Clear Log", font=(self.font_family, 8), fg="#B0B0B0", bg="#363642",
                                  bd=0, padx=6, pady=1, command=self.clear_inspector_log)
        clear_log_btn.pack(side=tk.RIGHT)

        self.log_text = tk.Text(self.inspector_frame, bg="#111116", fg="#00FF66", font=("Consolas", 9),
                                bd=0, highlightthickness=0, wrap=tk.NONE)
        scroll_y = ttk.Scrollbar(self.inspector_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll_y.set)

        self.log_text.tag_config("rx", foreground="#64DFDF")
        self.log_text.tag_config("tx", foreground="#FFD166")

        scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.status_bar = tk.Frame(self.inspector_frame, bg="#1B1B22", padx=8, pady=4)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)

        self.rx_label = tk.Label(self.status_bar, text="Packets: 0", font=(self.font_family, 8), fg="#A0A0A0", bg="#1B1B22")
        self.rx_label.pack(side=tk.LEFT, padx=(0, 10))

        self.fps_label = tk.Label(self.status_bar, text="FPS: 0", font=(self.font_family, 8), fg="#00FF66", bg="#1B1B22")
        self.fps_label.pack(side=tk.LEFT)

        self.fw_label = tk.Label(self.status_bar, text=APP_VERSION, font=(self.font_family, 8, "bold"), fg="#00B4D8", bg="#1B1B22")
        self.fw_label.pack(side=tk.RIGHT, padx=(0, 5))

        input_frame = tk.Frame(self.inspector_frame, bg="#26262F", padx=4, pady=4)
        input_frame.pack(fill=tk.X, side=tk.BOTTOM)

        self.manual_cmd = tk.Entry(input_frame, bg="#1A1A22", fg="#FFFFFF", font=("Consolas", 9), insertbackground="#FFFFFF")
        self.manual_cmd.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        self.manual_cmd.bind("<Return>", lambda e: self.send_manual_cmd())

        send_btn = tk.Button(input_frame, text="Send", font=(self.font_family, 8, "bold"), fg="#FFFFFF", bg="#0077B6",
                             bd=0, padx=8, command=self.send_manual_cmd)
        send_btn.pack(side=tk.RIGHT)

    def log_packet(self, msg, direction="RX"):
        ts = time.strftime("%H:%M:%S")
        prefix = "◀ " if direction == "RX" else "▶ "
        color_tag = "rx" if direction == "RX" else "tx"
        self.log_text.insert(tk.END, f"[{ts}] {prefix}{msg}\n", color_tag)

        line_count = int(self.log_text.index('end-1c').split('.')[0])
        if line_count > 300:
            self.log_text.delete("1.0", f"{line_count - 200}.0")
        self.log_text.see(tk.END)

    def clear_inspector_log(self):
        self.log_text.delete("1.0", tk.END)

    def send_manual_cmd(self):
        cmd = self.manual_cmd.get().strip()
        if cmd:
            self.send_serial_line(cmd)
            self.manual_cmd.delete(0, tk.END)

    # --------------------------------------------------------------------------
    # SERIAL PORT MANAGEMENT & THREADING
    # --------------------------------------------------------------------------
    def refresh_com_ports(self):
        if not HAS_SERIAL:
            self.port_combo['values'] = ["No pyserial"]
            self.port_combo.set("No pyserial")
            return

        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            self.port_combo['values'] = ["No Ports"]
            self.port_combo.set("No Ports")
        else:
            self.port_combo['values'] = ports
            if self.port_combo.get() not in ports:
                self.port_combo.set(ports[0])

    def toggle_connection(self):
        if self.is_connected:
            self.disconnect_serial()
        else:
            self.connect_serial()

    def connect_serial(self, silent=False):
        if self.is_connected:
            return

        if not HAS_SERIAL:
            if not silent:
                messagebox.showerror("Error", "pyserial library is not installed!")
            return

        port = self.port_combo.get()
        if not port or "No" in port:
            if not silent:
                messagebox.showwarning("Warning", "Please select a valid COM port!")
            return

        baud = int(self.baud_combo.get())

        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.is_connected = True
            self.connect_btn.configure(text="Disconnect", bg="#D90429")
            self.log_packet(f"Connected to {port} @ {baud} baud", "TX")

            self.serial_thread = threading.Thread(target=self.serial_reader_worker, daemon=True)
            self.serial_thread.start()
        except Exception as e:
            self.is_connected = False
            if not silent:
                messagebox.showerror("Connection Error", f"Failed to open {port}:\n{e}")

    def disconnect_serial(self):
        self.is_connected = False
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.connect_btn.configure(text="Connect", bg="#008037")
        self.log_packet("Serial port disconnected", "TX")

    def serial_reader_worker(self):
        buffer = ""
        while self.is_connected and self.ser and self.ser.is_open:
            try:
                data = self.ser.read(self.ser.in_waiting or 1).decode('utf-8', errors='ignore')
                if data:
                    buffer += data
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self.cmd_queue.put(line)
            except Exception:
                break
            time.sleep(0.001)

    def send_serial_char(self, c):
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(c.encode('utf-8'))
                self.ser.flush()
                self.log_packet(f"Key: '{c}'", "TX")
            except Exception as e:
                self.log_packet(f"TX Error: {e}", "TX")
        else:
            self.log_packet(f"Cannot send '{c}': COM port not connected", "TX")

    def send_serial_line(self, line):
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                payload = (line + "\n").encode('utf-8')
                self.ser.write(payload)
                self.ser.flush()
                self.log_packet(line, "TX")
            except Exception as e:
                self.log_packet(f"TX Error: {e}", "TX")
        else:
            self.log_packet(f"Cannot send '{line}': COM port not connected", "TX")

    # --------------------------------------------------------------------------
    # NAVIGATION BUTTON COMMANDS (1..5)
    # --------------------------------------------------------------------------
    def send_nav_up(self):
        self.send_serial_char('1')

    def send_nav_down(self):
        self.send_serial_char('2')

    def send_nav_left(self):
        self.send_serial_char('3')

    def send_nav_right(self):
        self.send_serial_char('4')

    def send_nav_ok(self):
        self.send_serial_char('5')

    def toggle_down_limit(self):
        self.sim_down_limit = not self.sim_down_limit
        state = "on" if self.sim_down_limit else "off"
        self.btn_dn_sw.configure(
            text=f"Down Sw: {'CLOSED' if self.sim_down_limit else 'OPEN'}",
            bg="#EF4444" if self.sim_down_limit else "#4B5563"
        )
        self.send_serial_line(f":down {state}")

    def toggle_home_limit(self):
        self.sim_home_limit = not self.sim_home_limit
        state = "on" if self.sim_home_limit else "off"
        self.btn_hm_sw.configure(
            text=f"Home Sw: {'CLOSED' if self.sim_home_limit else 'OPEN'}",
            bg="#EF4444" if self.sim_home_limit else "#4B5563"
        )
        self.send_serial_line(f":home {state}")

    def toggle_temp_sim(self):
        self.temp_sim_active = not self.temp_sim_active
        if self.temp_sim_active:
            self.btn_temp_sim.configure(text="Temp Sim: ON", bg="#008037")
            self.send_serial_line(":sim_temp on")
            self.log_packet("[BENCH] Enabled Bench Temperature Simulation")
        else:
            self.btn_temp_sim.configure(text="Temp Sim: OFF", bg="#4B5563")
            self.send_serial_line(":sim_temp off")
            self.log_packet("[BENCH] Disabled Bench Temperature Simulation (reading real sensor)")

    def prompt_set_temps(self):
        val = simpledialog.askstring("Set Temperatures", "Enter target or actual temp in °C:\n(e.g. 'h1 120' or 'h2 120')")
        if val:
            cmd = val.strip()
            if not cmd.startswith(":"):
                cmd = ":" + cmd
            self.send_serial_line(cmd)

    def prompt_set_torque(self):
        val = simpledialog.askstring("Set Torque", "Enter simulated torque in Nm:\n(e.g. '2.5')")
        if val:
            try:
                fval = float(val)
                self.send_serial_line(f":torque {fval:.2f}")
            except ValueError:
                pass

    def on_global_key(self, event):
        # Ignore hotkeys if typing in manual command input box
        if event.widget == self.manual_cmd:
            return

        k = event.keysym
        if k in ("Up", "KP_Up"):
            self.send_nav_up()
        elif k in ("Down", "KP_Down"):
            self.send_nav_down()
        elif k in ("Left", "KP_Left"):
            self.send_nav_left()
        elif k in ("Right", "KP_Right"):
            self.send_nav_right()
        elif k in ("Return", "KP_Enter", "space"):
            self.send_nav_ok()
        elif event.char in ('1', '2', '3', '4', '5'):
            self.send_serial_char(event.char)

    # --------------------------------------------------------------------------
    # COMMAND QUEUE & PARSER DISPATCHER
    # --------------------------------------------------------------------------
    def process_command_queue(self):
        batch_limit = 400
        processed = 0

        while not self.cmd_queue.empty() and processed < batch_limit:
            try:
                raw_cmd = self.cmd_queue.get_nowait()
                self.execute_command(raw_cmd)
                self.log_packet(raw_cmd, "RX")
                self.rx_counter += 1
                self.fps_counter += 1
                processed += 1
            except queue.Empty:
                break

        if processed > 0:
            self.blink_rx_led()

        self.after(5, self.process_command_queue)

    def blink_rx_led(self):
        if not self._rx_led_active:
            self._rx_led_active = True
            try:
                self.pcb_canvas.itemconfig(self.rx_led_id, fill="#00D2FF")
                self.after(40, self._turn_off_rx_led)
            except Exception:
                self._rx_led_active = False

    def _turn_off_rx_led(self):
        try:
            self.pcb_canvas.itemconfig(self.rx_led_id, fill="#1B2838")
        except Exception:
            pass
        finally:
            self._rx_led_active = False

    def update_stats(self):
        now = time.time()
        dt = now - self.last_fps_time
        if dt >= 1.0:
            fps = int(self.fps_counter / dt)
            self.fps_label.configure(text=f"FPS: {fps}")
            self.rx_label.configure(text=f"Packets: {self.rx_counter}")
            self.fps_counter = 0
            self.last_fps_time = now
        self.after(1000, self.update_stats)

    # --------------------------------------------------------------------------
    # GRAPHICS COMMAND PARSER
    # --------------------------------------------------------------------------
    def execute_command(self, cmd_line):
        if "[ESP32 Virtual UART TFT" in cmd_line:
            m = re.search(r'\((v[0-9.]+)\)', cmd_line)
            fw_ver = m.group(1) if m else "v2.0"
            self.fw_label.configure(text=f"FW: {fw_ver}")
            return

        parts = [p.strip() for p in cmd_line.split(',')]
        if not parts:
            return

        cmd = parts[0].upper()

        try:
            if cmd == "CLS":
                # Clear Screen: CLS,color
                color = parts[1] if len(parts) > 1 else "0x0000"
                hex_color = rgb565_to_hex(color)
                sw = self.tft_canvas.winfo_reqwidth()
                sh = self.tft_canvas.winfo_reqheight()
                self.tft_canvas.delete("all")
                self.tft_canvas.create_rectangle(0, 0, sw, sh, fill=hex_color, outline="")

            elif cmd == "PIX":
                # Pixel: PIX,x,y,color
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                color = rgb565_to_hex(parts[3])
                self.tft_canvas.create_rectangle(x, y, x + self.scale, y + self.scale, fill=color, outline=color)

            elif cmd == "LINE":
                # Line: LINE,x0,y0,x1,y1,color
                x0 = int(parts[1]) * self.scale
                y0 = int(parts[2]) * self.scale
                x1 = int(parts[3]) * self.scale
                y1 = int(parts[4]) * self.scale
                color = rgb565_to_hex(parts[5]) or "#FFFFFF"
                if y0 == y1:
                    rx1 = min(x0, x1)
                    rx2 = max(x0, x1) + self.scale
                    self.tft_canvas.create_rectangle(rx1, y0, rx2, y0 + self.scale, fill=color, outline=color)
                elif x0 == x1:
                    ry1 = min(y0, y1)
                    ry2 = max(y0, y1) + self.scale
                    self.tft_canvas.create_rectangle(x0, ry1, x0 + self.scale, ry2, fill=color, outline=color)
                else:
                    self.tft_canvas.create_line(x0, y0, x1, y1, fill=color, width=self.scale)
                self.tft_canvas.tag_raise("text_layer")

            elif cmd == "RECT":
                # Rect: RECT,x,y,w,h,color,fill
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                w = int(parts[3]) * self.scale
                h = int(parts[4]) * self.scale
                color = rgb565_to_hex(parts[5]) or "#000000"
                is_fill = int(parts[6]) if len(parts) > 6 else 0
                rect_tag = f"rect_{x}_{y}_{w}_{h}"
                existing_rect = self.tft_canvas.find_withtag(rect_tag)
                if existing_rect:
                    self.tft_canvas.itemconfigure(existing_rect[0], fill=color if is_fill else "", outline="" if is_fill else color)
                else:
                    if is_fill:
                        for item in self.tft_canvas.find_enclosed(x - 1, y - 1, x + w + 1, y + h + 1):
                            self.tft_canvas.delete(item)
                        self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill=color, outline="", tags=("bg_layer", rect_tag))
                    else:
                        self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill="", outline=color, width=self.scale, tags=("border_layer", rect_tag))
                self.tft_canvas.tag_raise("text_layer")

            elif cmd == "RRECT":
                # Rounded Rect: RRECT,x,y,w,h,r,color,fill
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                w = int(parts[3]) * self.scale
                h = int(parts[4]) * self.scale
                r = int(parts[5]) * self.scale
                color = rgb565_to_hex(parts[6]) or "#000000"
                is_fill = int(parts[7]) if len(parts) > 7 else 0
                rrect_tag = f"rrect_{x}_{y}_{w}_{h}"
                existing_rrect = self.tft_canvas.find_withtag(rrect_tag)
                if existing_rrect:
                    self.tft_canvas.itemconfigure(existing_rrect[0], fill=color if is_fill else "", outline="" if is_fill else color)
                else:
                    if is_fill:
                        for item in self.tft_canvas.find_enclosed(x - 1, y - 1, x + w + 1, y + h + 1):
                            self.tft_canvas.delete(item)
                        self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill=color, outline="", tags=("bg_layer", rrect_tag))
                    else:
                        self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill="", outline=color, width=self.scale, tags=("border_layer", rrect_tag))
                self.tft_canvas.tag_raise("text_layer")

            elif cmd == "CIRC":
                # Circle: CIRC,x,y,r,color,fill
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                r = int(parts[3]) * self.scale
                color = rgb565_to_hex(parts[4]) or "#000000"
                is_fill = int(parts[5]) if len(parts) > 5 else 0
                circ_tag = f"circ_{x}_{y}_{r}"
                existing_circ = self.tft_canvas.find_withtag(circ_tag)
                if existing_circ:
                    self.tft_canvas.itemconfigure(existing_circ[0], fill=color if is_fill else "", outline="" if is_fill else color)
                else:
                    if is_fill:
                        self.tft_canvas.create_oval(x - r, y - r, x + r, y + r, fill=color, outline="", tags=("bg_layer", circ_tag))
                    else:
                        self.tft_canvas.create_oval(x - r, y - r, x + r, y + r, fill="", outline=color, width=self.scale, tags=("border_layer", circ_tag))
                self.tft_canvas.tag_raise("text_layer")

            elif cmd == "TXT":
                # Text: TXT,x,y,size,fgColor,bgColor,text...
                x = int(parts[1])
                y = int(parts[2])
                size = max(1, int(parts[3]))
                fg = rgb565_to_hex(parts[4]) or "#FFFFFF"
                bg = rgb565_to_hex(parts[5]) if len(parts) > 5 else None
                text_str = ",".join(parts[6:]) if len(parts) > 6 else ""

                self.render_text(x, y, size, fg, bg, text_str)

            elif cmd == "ROTA":
                # Rotation: ROTA,0-3
                self.rotation = int(parts[1]) & 3

        except Exception:
            pass

    def render_text(self, x, y, size, fg, bg, text_str):
        cx = x * self.scale
        cy = y * self.scale

        if size == 1:
            f_size = -int(8 * self.scale)
            weight = "normal"
        elif size == 2:
            # FreeSansBold9pt7b: capital/digit ascent 13px, advance 22px
            f_size = -int(14 * self.scale)
            weight = "bold"
        elif size == 3:
            # FreeSansBold12pt7b: capital/digit ascent 18px, advance 29px
            f_size = -int(19 * self.scale)
            weight = "bold"
        else:
            # FreeSansBold18pt7b: capital/digit ascent 25-26px, advance 42px
            f_size = -int(26 * self.scale)
            weight = "bold"

        font_spec = (self.tft_font_family, f_size, weight)
        tag_tx = f"tx_{x}_{y}"
        tag_bg = f"bg_{x}_{y}"

        # Only draw background rectangle for non-black background colors.
        # Drawing solid black rectangles over black canvas clobbers and slices neighboring text items!
        is_colored_bg = bool(bg and bg.lower() not in ('#000000', 'none', '#000', 'black', '0x0000'))

        tx_items = self.tft_canvas.find_withtag(tag_tx)
        bg_items = self.tft_canvas.find_withtag(tag_bg)

        if tx_items:
            # Update existing text item directly in-place (100% flicker-free)
            self.tft_canvas.itemconfigure(tx_items[0], text=text_str, fill=fg, font=font_spec)
            self.tft_canvas.tag_raise(tx_items[0])
            if is_colored_bg:
                bbox = self.tft_canvas.bbox(tx_items[0])
                if bbox:
                    if bg_items:
                        self.tft_canvas.coords(bg_items[0], bbox[0], bbox[1], bbox[2], bbox[3])
                        self.tft_canvas.itemconfigure(bg_items[0], fill=bg)
                    else:
                        self.tft_canvas.create_rectangle(bbox[0], bbox[1], bbox[2], bbox[3], fill=bg, outline="", tags=(tag_bg, "bg_layer"))
                    self.tft_canvas.tag_lower(tag_bg, tx_items[0])
            elif bg_items:
                self.tft_canvas.delete(tag_bg)
        else:
            # First-time creation of text item
            new_tx = self.tft_canvas.create_text(cx, cy, text=text_str, font=font_spec, fill=fg, anchor=tk.NW, tags=(tag_tx, "text_layer"))
            self.tft_canvas.tag_raise(new_tx)
            if is_colored_bg:
                bbox = self.tft_canvas.bbox(new_tx)
                if bbox:
                    self.tft_canvas.create_rectangle(bbox[0], bbox[1], bbox[2], bbox[3], fill=bg, outline="", tags=(tag_bg, "bg_layer"))
                    self.tft_canvas.tag_lower(tag_bg, new_tx)
            elif bg_items:
                self.tft_canvas.delete(tag_bg)

    def on_scale_change(self, event=None):
        sel = self.scale_combo.get()
        if "1x" in sel: self.scale = 1
        elif "2x" in sel: self.scale = 2
        elif "3x" in sel: self.scale = 3
        self.draw_tft_hardware()

    def save_snapshot(self):
        file_path = filedialog.asksaveasfilename(
            defaultextension=".ps",
            filetypes=[("PostScript file", "*.ps"), ("All Files", "*.*")],
            title="Save TFT Display Snapshot"
        )
        if file_path:
            try:
                self.tft_canvas.postscript(file=file_path, colormode='color')
                messagebox.showinfo("Saved", f"Screenshot saved successfully:\n{file_path}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to save snapshot:\n{e}")

    # --------------------------------------------------------------------------
    # STANDALONE DEMO SIMULATION WORKER
    # --------------------------------------------------------------------------
    def toggle_simulation(self):
        if self.sim_running:
            self.sim_running = False
            self.sim_stop_event.set()
            if self.sim_thread and self.sim_thread.is_alive():
                self.sim_thread.join(timeout=0.2)
            self.sim_btn.configure(text="▶ Run Demo", bg="#6930C3")
        else:
            self.sim_running = True
            self.sim_stop_event.clear()
            self.sim_btn.configure(text="⏹ Stop Demo", bg="#D90429")
            self.sim_thread = threading.Thread(target=self.demo_simulation_worker, daemon=True)
            self.sim_thread.start()

    def demo_simulation_worker(self):
        """Simulates the Sun Lazer Dual Heater Controller modern card layout and live readings"""
        try:
            # 5-second Welcome Screen (SUN Smart, FW Version, ESP32 Serial, Build Date)
            self.cmd_queue.put("CLS,0x0000")
            time.sleep(0.05)
            self.cmd_queue.put("RRECT,10,10,300,220,8,0x03EF,0")
            self.cmd_queue.put("RECT,12,12,296,36,0x0841,1")
            self.cmd_queue.put("LINE,10,48,310,48,0x03EF")
            self.cmd_queue.put("TXT,97,18,4,0x07FF,0x0841,SUN SMART")
            self.cmd_queue.put("TXT,72,56,2,0xFFE0,0x0000,DUAL HEATER CONTROLLER")
            self.cmd_queue.put("LINE,24,76,296,76,0x4A69")
            self.cmd_queue.put("TXT,28,88,2,0xC618,0x0000,FW VERSION:")
            self.cmd_queue.put(f"TXT,160,88,2,0x07E0,0x0000,{APP_VERSION}")
            self.cmd_queue.put("TXT,28,114,2,0xC618,0x0000,SERIAL NO:")
            self.cmd_queue.put("TXT,160,114,2,0xFFFF,0x0000,A1B2C3D4E5F6")
            self.cmd_queue.put("TXT,28,140,2,0xC618,0x0000,BUILD DATE:")
            self.cmd_queue.put("TXT,160,140,2,0xFFFF,0x0000,Sep 20 2026")
            self.cmd_queue.put("TXT,28,166,2,0xC618,0x0000,BUILD TIME:")
            self.cmd_queue.put("TXT,160,166,2,0xFFFF,0x0000,05:07:00")
            self.cmd_queue.put("TXT,28,194,2,0x52AA,0x0000,STARTING SYSTEM...")
            self.cmd_queue.put("RRECT,28,212,264,8,3,0x4A69,0")

            for s in range(5, 0, -1):
                self.cmd_queue.put(f"TXT,275,194,2,0x07FF,0x0000,{s}s")
                prog_w = int((5 - s + 1) * 260 / 5)
                self.cmd_queue.put(f"RRECT,30,214,{prog_w},4,2,0x07E0,1")
                if self.sim_stop_event.wait(1.0):
                    return

            self.cmd_queue.put("CLS,0x0000")
            time.sleep(0.05)

            # Mobile Style Top Header (Home Screen: 32px, no Time/WiFi/SD/AUTO)
            self.cmd_queue.put("RECT,0,0,320,32,0x0841,1")
            self.cmd_queue.put("LINE,0,32,320,32,0x03EF")
            self.cmd_queue.put("TXT,10,8,3,0x07FF,0x0841,Sun Smart")
            self.cmd_queue.put("TXT,170,8,3,0xFFE0,0x0841,Standard Seal")

            # Status Banner (y = 36..56, h = 20)
            self.cmd_queue.put("RRECT,6,36,308,20,3,0x03E0,0")
            self.cmd_queue.put("TXT,14,38,2,0x07E0,0x0000,READY (AUTO) - PRESS [START]")

            # 4 Modern Cards (Outlines & Titles)
            # Card 1: Heater 1 (y = 60..128, h = 68)
            self.cmd_queue.put("RRECT,6,60,150,68,4,0x4A69,0")
            self.cmd_queue.put("TXT,14,63,2,0x07FF,0x0000,HEATER 1")
            self.cmd_queue.put("TXT,14,111,2,0xC618,0x0000,SET: 50.0 C")

            # Card 2: Heater 2 (y = 60..128, h = 68)
            self.cmd_queue.put("RRECT,164,60,150,68,4,0x4A69,0")
            self.cmd_queue.put("TXT,172,63,2,0x07FF,0x0000,HEATER 2")
            self.cmd_queue.put("TXT,172,111,2,0xC618,0x0000,SET: 50.0 C")

            # Card 3: Torque (y = 132..201, h = 69)
            self.cmd_queue.put("RRECT,6,132,150,69,4,0x4A69,0")
            self.cmd_queue.put("TXT,14,135,2,0xFFE0,0x0000,TORQUE")

            # Card 4: Timer (y = 132..201, h = 69)
            self.cmd_queue.put("RRECT,164,132,150,69,4,0x4A69,0")
            self.cmd_queue.put("TXT,172,135,2,0x07E0,0x0000,TIMER")

            # Footer (y = 206..236, h = 30) with Font 2 (smaller size)
            self.cmd_queue.put("RECT,0,206,320,30,0x0841,1")
            self.cmd_queue.put("LINE,0,206,320,206,0x03EF")
            self.cmd_queue.put("TXT,10,213,2,0xFFFF,0x0841,[START]: Run")
            self.cmd_queue.put("TXT,235,213,2,0xFFFF,0x0841,[->]: Menu")
            self.cmd_queue.put("RECT,0,236,320,4,0x03E0,1")

            t = 0.0
            timer_sec = 150
            while not self.sim_stop_event.is_set():
                h1_act = 149.2 + 1.2 * math.sin(t * 0.8)
                h2_act = 150.1 + 0.9 * math.cos(t * 0.7)
                torque = max(0.0, 1.45 + 0.35 * math.sin(t * 1.2))

                # Medium font (size 4) updates in-place
                self.cmd_queue.put(f"TXT,14,78,4,0xFFFF,0x0000,{h1_act:.1f}")
                self.cmd_queue.put("TXT,105,79,2,0xFFFF,0x0000,C")

                self.cmd_queue.put(f"TXT,172,78,4,0xFFFF,0x0000,{h2_act:.1f}")
                self.cmd_queue.put("TXT,265,79,2,0xFFFF,0x0000,C")

                self.cmd_queue.put(f"TXT,14,158,4,0xFFFF,0x0000,{torque:.2f}")
                self.cmd_queue.put("TXT,105,159,2,0xFFFF,0x0000,Nm")

                mm = timer_sec // 60
                ss = timer_sec % 60
                self.cmd_queue.put(f"TXT,172,158,4,0x07E0,0x0000,{mm:02d}:{ss:02d}")

                t += 0.2
                if int(t * 5) % 5 == 0 and timer_sec > 0:
                    timer_sec -= 1

                if self.sim_stop_event.wait(0.2):
                    break
        finally:
            self.sim_running = False

    def on_close(self):
        self.sim_running = False
        self.sim_stop_event.set()
        self.disconnect_serial()
        self.destroy()


# ==============================================================================
# ENTRY POINT
# ==============================================================================
if __name__ == "__main__":
    app = VirtualTFTApp()
    app.mainloop()
