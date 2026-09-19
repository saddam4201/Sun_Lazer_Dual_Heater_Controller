"""
Standalone 2.8" TFT Display (ILI9341) Emulator for Windows over UART / Serial
Author: Antigravity AI
Resolution: 320x240 (Landscape) / 240x320 (Portrait)
Features:
- Realistic ILI9341 2.8" PCB Breakout Board bezel styling (pins, mounting holes, silkscreen)
- Real-time COM port detection & multithreaded serial reader
- RGB565 16-bit color decoding (standard ILI9341 color format) & standard hex colors
- Adafruit_GFX compatible command parser (CLS, TXT, LINE, RECT, CIRC, PIX, ROTA)
- Built-in Virtual MCU Simulator (live gauges, moving graphs, telemetry)
- 1x, 2x, 3x crisp integer display scaling
- Live serial inspector & packet monitor
"""

import sys
import os
import time
import math
import threading
import queue
import re
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

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
    """
    Converts 16-bit RGB565 integer or hex string (e.g. 0xF800, '0xFFFF', '63488')
    to standard #RRGGBB hex string.
    Returns None if color_val is None, empty, 'none', or invalid (for transparent rendering).
    RGB565 layout: RRRRR GGGGGG BBBBB
    """
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
ILI9341_ORANGE      = 0xFD20
ILI9341_GREENYELLOW = 0xAFE5
ILI9341_PINK        = 0xFC18


# ==============================================================================
# CLASSIC 5x7 ADAFRUIT_GFX FONT BITMAP (ASCII 32 - 126)
# ==============================================================================
# 5 columns per character, MSB down
FONT_5X7 = {
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00],
    '!': [0x00, 0x00, 0x5F, 0x00, 0x00],
    '"': [0x00, 0x07, 0x00, 0x07, 0x00],
    '#': [0x14, 0x7F, 0x14, 0x7F, 0x14],
    '$': [0x24, 0x2A, 0x7F, 0x2A, 0x12],
    '%': [0x23, 0x13, 0x08, 0x64, 0x62],
    '&': [0x36, 0x49, 0x55, 0x22, 0x50],
    "'": [0x00, 0x05, 0x03, 0x00, 0x00],
    '(': [0x00, 0x1C, 0x22, 0x41, 0x00],
    ')': [0x00, 0x41, 0x22, 0x1C, 0x00],
    '*': [0x14, 0x08, 0x3E, 0x08, 0x14],
    '+': [0x08, 0x08, 0x3E, 0x08, 0x08],
    ',': [0x00, 0x50, 0x30, 0x00, 0x00],
    '-': [0x08, 0x08, 0x08, 0x08, 0x08],
    '.': [0x00, 0x60, 0x60, 0x00, 0x00],
    '/': [0x20, 0x10, 0x08, 0x04, 0x02],
    '0': [0x3E, 0x51, 0x49, 0x45, 0x3E],
    '1': [0x00, 0x42, 0x7F, 0x40, 0x00],
    '2': [0x42, 0x61, 0x51, 0x49, 0x46],
    '3': [0x21, 0x41, 0x45, 0x4B, 0x31],
    '4': [0x18, 0x14, 0x12, 0x7F, 0x10],
    '5': [0x27, 0x45, 0x45, 0x45, 0x39],
    '6': [0x3C, 0x4A, 0x49, 0x49, 0x30],
    '7': [0x01, 0x71, 0x09, 0x05, 0x03],
    '8': [0x36, 0x49, 0x49, 0x49, 0x36],
    '9': [0x06, 0x49, 0x49, 0x29, 0x1E],
    ':': [0x00, 0x36, 0x36, 0x00, 0x00],
    ';': [0x00, 0x56, 0x36, 0x00, 0x00],
    '<': [0x08, 0x14, 0x22, 0x41, 0x00],
    '=': [0x14, 0x14, 0x14, 0x14, 0x14],
    '>': [0x00, 0x41, 0x22, 0x14, 0x08],
    '?': [0x02, 0x01, 0x51, 0x09, 0x06],
    '@': [0x32, 0x49, 0x79, 0x41, 0x3E],
    'A': [0x7E, 0x11, 0x11, 0x11, 0x7E],
    'B': [0x7F, 0x49, 0x49, 0x49, 0x36],
    'C': [0x3E, 0x41, 0x41, 0x41, 0x22],
    'D': [0x7F, 0x41, 0x41, 0x22, 0x1C],
    'E': [0x7F, 0x49, 0x49, 0x49, 0x41],
    'F': [0x7F, 0x09, 0x09, 0x09, 0x01],
    'G': [0x3E, 0x41, 0x49, 0x49, 0x7A],
    'H': [0x7F, 0x08, 0x08, 0x08, 0x7F],
    'I': [0x00, 0x41, 0x7F, 0x41, 0x00],
    'J': [0x20, 0x40, 0x41, 0x3F, 0x01],
    'K': [0x7F, 0x08, 0x14, 0x22, 0x41],
    'L': [0x7F, 0x40, 0x40, 0x40, 0x40],
    'M': [0x7F, 0x02, 0x0C, 0x02, 0x7F],
    'N': [0x7F, 0x04, 0x08, 0x10, 0x7F],
    'O': [0x3E, 0x41, 0x41, 0x41, 0x3E],
    'P': [0x7F, 0x09, 0x09, 0x09, 0x06],
    'Q': [0x3E, 0x41, 0x51, 0x21, 0x5E],
    'R': [0x7F, 0x09, 0x19, 0x29, 0x46],
    'S': [0x46, 0x49, 0x49, 0x49, 0x31],
    'T': [0x01, 0x01, 0x7F, 0x01, 0x01],
    'U': [0x3F, 0x40, 0x40, 0x40, 0x3F],
    'V': [0x1F, 0x20, 0x40, 0x20, 0x1F],
    'W': [0x7F, 0x20, 0x18, 0x20, 0x7F],
    'X': [0x63, 0x14, 0x08, 0x14, 0x63],
    'Y': [0x07, 0x08, 0x70, 0x08, 0x07],
    'Z': [0x61, 0x51, 0x49, 0x45, 0x43],
    '[': [0x00, 0x7F, 0x41, 0x41, 0x00],
    '\\': [0x02, 0x04, 0x08, 0x10, 0x20],
    ']': [0x00, 0x41, 0x41, 0x7F, 0x00],
    '^': [0x04, 0x02, 0x01, 0x02, 0x04],
    '_': [0x40, 0x40, 0x40, 0x40, 0x40],
    '`': [0x00, 0x01, 0x02, 0x04, 0x00],
    'a': [0x20, 0x54, 0x54, 0x54, 0x78],
    'b': [0x7F, 0x48, 0x44, 0x44, 0x38],
    'c': [0x38, 0x44, 0x44, 0x44, 0x20],
    'd': [0x38, 0x44, 0x44, 0x48, 0x7F],
    'e': [0x38, 0x54, 0x54, 0x54, 0x18],
    'f': [0x08, 0x7E, 0x09, 0x01, 0x02],
    'g': [0x0C, 0x52, 0x52, 0x52, 0x3E],
    'h': [0x7F, 0x08, 0x04, 0x04, 0x78],
    'i': [0x00, 0x44, 0x7D, 0x40, 0x00],
    'j': [0x20, 0x40, 0x44, 0x3D, 0x00],
    'k': [0x7F, 0x10, 0x28, 0x44, 0x00],
    'l': [0x00, 0x41, 0x7F, 0x40, 0x00],
    'm': [0x7C, 0x04, 0x18, 0x04, 0x78],
    'n': [0x7C, 0x08, 0x04, 0x04, 0x78],
    'o': [0x38, 0x44, 0x44, 0x44, 0x38],
    'p': [0x7C, 0x14, 0x14, 0x14, 0x08],
    'q': [0x08, 0x14, 0x14, 0x18, 0x7C],
    'r': [0x7C, 0x08, 0x04, 0x04, 0x08],
    's': [0x48, 0x54, 0x54, 0x54, 0x20],
    't': [0x04, 0x3F, 0x44, 0x40, 0x20],
    'u': [0x3C, 0x40, 0x40, 0x20, 0x7C],
    'v': [0x1C, 0x20, 0x40, 0x20, 0x1C],
    'w': [0x3C, 0x40, 0x30, 0x40, 0x3C],
    'x': [0x44, 0x28, 0x10, 0x28, 0x44],
    'y': [0x0C, 0x50, 0x50, 0x50, 0x3C],
    'z': [0x44, 0x64, 0x54, 0x4C, 0x44],
    '{': [0x00, 0x08, 0x36, 0x41, 0x00],
    '|': [0x00, 0x00, 0x7F, 0x00, 0x00],
    '}': [0x00, 0x41, 0x36, 0x08, 0x00],
    '~': [0x08, 0x08, 0x2A, 0x1C, 0x08],
    '°': [0x0C, 0x12, 0x12, 0x0C, 0x00], # Custom degree symbol
}


# ==============================================================================
# MAIN TFT EMULATOR WINDOW APPLICATION
# ==============================================================================
def get_firmware_version():
    try:
        cfg_path = os.path.join(os.path.dirname(__file__), "Display_Sniffer", "SPI_Sniffer_128x64_Display", "include", "config.h")
        if os.path.exists(cfg_path):
            with open(cfg_path, "r", encoding="utf-8") as f:
                for line in f:
                    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', line)
                    if m:
                        return m.group(1)
    except Exception:
        pass
    return "v3.1.0"

FIRMWARE_VERSION = get_firmware_version()


class VirtualTFTApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"Virtual 2.8\" TFT Display (ILI9341) - FW: {FIRMWARE_VERSION} - UART Serial Monitor")
        self.configure(bg="#1E1E24")
        self.geometry("1180x780")
        self.minsize(980, 680)

        # TFT Native dimensions
        self.native_w = 320
        self.native_h = 240
        self.rotation = 1  # 0: Portrait (240x320), 1: Landscape (320x240), 2: Inv-Port, 3: Inv-Land
        self.scale = 2     # Display scaling (2x gives 640x480 screen)

        # Serial Port state
        self.ser = None
        self.serial_thread = None
        self.is_connected = False
        self.cmd_queue = queue.Queue()
        self.rx_counter = 0
        self.fps_counter = 0
        self.last_fps_time = time.time()

        # Built-in simulator state
        self.sim_thread = None
        self.sim_running = False
        self.sim_stop_event = threading.Event()
        self._rx_led_active = False

        # Modern Typography Setup
        self.font_mode = "modern"  # "modern" for anti-aliased TrueType fonts; "retro" for 5x7 dot-matrix
        self.modern_font_family = "Segoe UI"
        try:
            import tkinter.font as tkfont
            avail = tkfont.families()
            for cand in ("Segoe UI", "Inter", "Roboto", "Helvetica", "Arial"):
                if cand in avail:
                    self.modern_font_family = cand
                    break
        except Exception:
            pass

        # Build UI layout
        self.setup_ui()

        # Handle clean window close
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        # Periodic UI update from queue
        self.after(10, self.process_command_queue)
        self.after(1000, self.update_stats)

        # Global hotkeys (v = switch mode, m = switch theme, c = clear screen)
        self.bind_all("<Key>", self.on_global_key)

        # Auto-connect if COM port is detected on startup
        self.after(300, lambda: self.connect_serial(silent=True))

    # --------------------------------------------------------------------------
    # UI SETUP & STYLING
    # --------------------------------------------------------------------------
    def setup_ui(self):
        # Top toolbar
        toolbar = tk.Frame(self, bg="#2A2A32", height=48, padx=12, pady=8)
        toolbar.pack(side=tk.TOP, fill=tk.X)

        # Title / Brand
        brand_lbl = tk.Label(toolbar, text=f"ILI9341 2.8\" TFT ({FIRMWARE_VERSION})", font=("Segoe UI", 12, "bold"), fg="#FF4B4B", bg="#2A2A32")
        brand_lbl.pack(side=tk.LEFT, padx=(0, 15))

        # COM Port Selector
        tk.Label(toolbar, text="Port:", font=("Segoe UI", 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.port_combo = ttk.Combobox(toolbar, width=12, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 6))

        refresh_btn = tk.Button(toolbar, text="↻", font=("Segoe UI", 10, "bold"), fg="#FFFFFF", bg="#3E3E4A",
                                activebackground="#505060", activeforeground="#FFFFFF", bd=0, padx=6, pady=1,
                                command=self.refresh_com_ports)
        refresh_btn.pack(side=tk.LEFT, padx=(0, 12))

        # Baud Rate Selector
        tk.Label(toolbar, text="Baud:", font=("Segoe UI", 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.baud_combo = ttk.Combobox(toolbar, width=9, state="readonly",
                                       values=["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "2000000"])
        self.baud_combo.set("115200")
        self.baud_combo.pack(side=tk.LEFT, padx=(0, 12))

        # Connect Button
        self.connect_btn = tk.Button(toolbar, text="Connect", font=("Segoe UI", 9, "bold"), fg="#FFFFFF", bg="#008037",
                                     activebackground="#00A045", bd=0, padx=14, pady=3, command=self.toggle_connection)
        self.connect_btn.pack(side=tk.LEFT, padx=(0, 15))

        # Scale Factor Selector
        tk.Label(toolbar, text="Zoom:", font=("Segoe UI", 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.scale_combo = ttk.Combobox(toolbar, width=4, state="readonly", values=["1x", "2x", "3x"])
        self.scale_combo.set("2x")
        self.scale_combo.bind("<<ComboboxSelected>>", self.on_scale_change)
        self.scale_combo.pack(side=tk.LEFT, padx=(0, 12))

        # Orientation Selector
        tk.Label(toolbar, text="Rotate:", font=("Segoe UI", 9), fg="#DCDCDC", bg="#2A2A32").pack(side=tk.LEFT, padx=(0, 4))
        self.rot_combo = ttk.Combobox(toolbar, width=12, state="readonly",
                                      values=["0 (Portrait)", "1 (Landscape)", "2 (Inv-Port)", "3 (Inv-Land)"])
        self.rot_combo.set("1 (Landscape)")
        self.rot_combo.bind("<<ComboboxSelected>>", self.on_rotation_change)
        self.rot_combo.pack(side=tk.LEFT, padx=(0, 15))

        # Hardware Simulator / Demo Mode Button
        self.sim_btn = tk.Button(toolbar, text="▶ Run Demo", font=("Segoe UI", 9, "bold"), fg="#FFFFFF", bg="#6930C3",
                                 activebackground="#7400B8", bd=0, padx=12, pady=3, command=self.toggle_simulation)
        self.sim_btn.pack(side=tk.LEFT, padx=(0, 8))

        # Clear Screen button
        clr_btn = tk.Button(toolbar, text="Clear", font=("Segoe UI", 9), fg="#DCDCDC", bg="#3E3E4A",
                            bd=0, padx=8, pady=3, command=lambda: self.execute_command("CLS,0x0000"))
        clr_btn.pack(side=tk.LEFT, padx=(0, 6))

        # Display Mode Toggle (Original Display vs Modern Dashboard)
        mode_btn = tk.Button(toolbar, text="🖥️ Mode ('v')", font=("Segoe UI", 9, "bold"), fg="#FFFFFF", bg="#0077B6",
                             activebackground="#0096C7", bd=0, padx=10, pady=3, command=self.send_switch_mode)
        mode_btn.pack(side=tk.LEFT, padx=(0, 6))

        # Color Theme Toggle
        theme_btn = tk.Button(toolbar, text="🎨 Theme ('m')", font=("Segoe UI", 9), fg="#FFFFFF", bg="#495057",
                              activebackground="#6C757D", bd=0, padx=8, pady=3, command=self.send_switch_theme)
        theme_btn.pack(side=tk.LEFT, padx=(0, 6))

        # Font Style Toggle (Modern Vector Typography vs Retro 5x7 Dot-Matrix)
        self.font_btn = tk.Button(toolbar, text="🔤 Font: Modern", font=("Segoe UI", 9, "bold"), fg="#FFFFFF", bg="#0081A7",
                                  activebackground="#00AFB9", bd=0, padx=8, pady=3, command=self.toggle_font_mode)
        self.font_btn.pack(side=tk.LEFT, padx=(0, 8))

        # Screenshot button
        snap_btn = tk.Button(toolbar, text="📸 Snapshot", font=("Segoe UI", 9), fg="#DCDCDC", bg="#3E3E4A",
                             bd=0, padx=8, pady=3, command=self.save_snapshot)
        snap_btn.pack(side=tk.LEFT, padx=(0, 8))

        # Secondary Toolbar: Ultrasonic Degasser Remote Controls
        degas_bar = tk.Frame(self, bg="#202028", height=42, padx=12, pady=6)
        degas_bar.pack(side=tk.TOP, fill=tk.X)

        # Brand / Section Icon
        tk.Label(degas_bar, text="🥛 Degasser Controls:", font=("Segoe UI", 9, "bold"),
                 fg="#FFD166", bg="#202028").pack(side=tk.LEFT, padx=(0, 10))

        # BTN 1: Degas / Abort (Click)
        self.btn1_gui = tk.Button(degas_bar, text="🥛 Degas / Abort [1]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#008037", activebackground="#00A045", bd=0, padx=12, pady=3,
                                  command=self.send_btn1_trigger)
        self.btn1_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 1 HOLD: Setup Mode Toggle
        self.btn_setup_gui = tk.Button(degas_bar, text="⚙️ Setup Mode [s]", font=("Segoe UI", 9, "bold"),
                                       fg="#FFFFFF", bg="#4F46E5", activebackground="#6366F1", bd=0, padx=10, pady=3,
                                       command=self.send_btn1_setup)
        self.btn_setup_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 2: Frequency Down
        self.btn2_gui = tk.Button(degas_bar, text="➖ Freq - [2]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=10, pady=3,
                                  command=self.send_btn2_freq_down)
        self.btn2_gui.pack(side=tk.LEFT, padx=(0, 6))

        # BTN 3: Frequency Up
        self.btn3_gui = tk.Button(degas_bar, text="➕ Freq + [3]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#0284C7", activebackground="#0EA5E9", bd=0, padx=10, pady=3,
                                  command=self.send_btn3_freq_up)
        self.btn3_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 4: Nuvoton Mode Toggle (Auto / Manual)
        self.btn4_gui = tk.Button(degas_bar, text="🚀 Nuvoton [4]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#D97706", activebackground="#F59E0B", bd=0, padx=10, pady=3,
                                  command=self.send_btn4_nuvoton_mode)
        self.btn4_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 5: Thermal Printer Test
        self.btn5_gui = tk.Button(degas_bar, text="🖨️ Print [5]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#7C3AED", activebackground="#8B5CF6", bd=0, padx=10, pady=3,
                                  command=self.send_btn5_print_test)
        self.btn5_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 6: Simulator Demo Cycle
        self.btn6_gui = tk.Button(degas_bar, text="🧪 Sim Cycle [6]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#DB2777", activebackground="#F472B6", bd=0, padx=10, pady=3,
                                  command=self.send_btn6_sim_cycle)
        self.btn6_gui.pack(side=tk.LEFT, padx=(0, 8))

        # BTN 7: Simulate Bluetooth App Return Payload
        self.btn7_gui = tk.Button(degas_bar, text="📲 BT Return [7]", font=("Segoe UI", 9, "bold"),
                                  fg="#FFFFFF", bg="#4338CA", activebackground="#6366F1", bd=0, padx=10, pady=3,
                                  command=self.send_btn7_bt_return)
        self.btn7_gui.pack(side=tk.LEFT, padx=(0, 12))

        # Degasser Status Label
        self.degas_status_lbl = tk.Label(degas_bar, text="Standby: [1] Start, Hold [s] Setup, [2]/[3] Freq, [4] Nuvoton, [5] Print, [6] Sim, [7] BT Return",
                                         font=("Segoe UI", 8, "italic"), fg="#94A3B8", bg="#202028")
        self.degas_status_lbl.pack(side=tk.LEFT, padx=(6, 0))

        # Main Workspace (Split into TFT Bezel view and Collapsible Console)
        main_paned = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#1E1E24", bd=0, sashwidth=4)
        main_paned.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=8, pady=8)

        # Left Container: Realistic TFT PCB Bezel
        self.tft_container = tk.Frame(main_paned, bg="#18181D", bd=0)
        main_paned.add(self.tft_container, minsize=520)

        # Right Container: Serial Packet Log & Inspector
        self.inspector_frame = tk.Frame(main_paned, bg="#1E1E24", width=340)
        main_paned.add(self.inspector_frame, minsize=260)

        self.setup_inspector_ui()
        self.draw_tft_hardware()
        self.refresh_com_ports()

    # --------------------------------------------------------------------------
    # REALISTIC ILI9341 HARDWARE BEZEL DRAWING
    # --------------------------------------------------------------------------
    def draw_tft_hardware(self):
        """Draws the realistic Red PCB breakout board with pinout silkscreen, screws, and screen"""
        for child in self.tft_container.winfo_children():
            child.destroy()

        # Determine effective screen size based on rotation & scale
        if self.rotation in (0, 2):
            # Portrait: 240 x 320
            sw = 240 * self.scale
            sh = 320 * self.scale
        else:
            # Landscape: 320 x 240
            sw = 320 * self.scale
            sh = 240 * self.scale

        # PCB margin around the screen
        pcb_margin_x = 40
        pcb_margin_y = 48
        pcb_w = sw + (pcb_margin_x * 2)
        pcb_h = sh + (pcb_margin_y * 2) + 60 # extra room for tactile buttons and pinout header

        # Center canvas container
        scroll_wrapper = tk.Frame(self.tft_container, bg="#18181D")
        scroll_wrapper.pack(expand=True)

        self.pcb_canvas = tk.Canvas(scroll_wrapper, width=pcb_w, height=pcb_h, bg="#18181D",
                                    highlightthickness=0, bd=0)
        self.pcb_canvas.pack(pady=10)

        # 1. Red PCB Board (Classic ILI9341 Red PCB)
        self.pcb_canvas.create_rectangle(6, 6, pcb_w - 6, pcb_h - 6, fill="#A31414", outline="#750E0E", width=2)
        # PCB Corner Screw Holes (Golden pads)
        hole_r = 8
        pads = [
            (20, 20), (pcb_w - 20, 20),
            (20, pcb_h - 20), (pcb_w - 20, pcb_h - 20)
        ]
        for hx, hy in pads:
            self.pcb_canvas.create_oval(hx - hole_r, hy - hole_r, hx + hole_r, hy + hole_r, fill="#D4AF37", outline="#997C22", width=1.5)
            self.pcb_canvas.create_oval(hx - 4, hy - 4, hx + 4, hy + 4, fill="#18181D", outline="")

        # 2. Silkscreen text on PCB
        self.pcb_canvas.create_text(pcb_w // 2, 18, text="2.8\" TFT SPI / UART 240x320 ILI9341",
                                    font=("Consolas", 10, "bold"), fill="#EFEFEF")

        # 3. Metallic screen bezel outer frame
        screen_x1 = pcb_margin_x
        screen_y1 = pcb_margin_y - 12
        screen_x2 = screen_x1 + sw
        screen_y2 = screen_y1 + sh

        # Bezel silver/grey border
        self.pcb_canvas.create_rectangle(screen_x1 - 6, screen_y1 - 6, screen_x2 + 6, screen_y2 + 6,
                                         fill="#2C2E33", outline="#50535B", width=2)
        # Inner glass shadow
        self.pcb_canvas.create_rectangle(screen_x1 - 2, screen_y1 - 2, screen_x2 + 2, screen_y2 + 2,
                                         fill="#111113", outline="#1F2024", width=1)

        # 4. The actual Active LCD Screen Canvas
        self.tft_canvas = tk.Canvas(self.pcb_canvas, width=sw, height=sh, bg="#050507",
                                    highlightthickness=0, bd=0)
        self.pcb_canvas.create_window(screen_x1, screen_y1, anchor=tk.NW, window=self.tft_canvas)

        # 5. Interactive Hardware Tactile Pushbuttons (Degasser & System Remote Controls)
        btn_y = screen_y2 + 20
        btn_spacing = min(68, max(46, (pcb_w - 60) // 6))
        btn_start_x = pcb_w // 2 - int(2.5 * btn_spacing)
        tactile_specs = [
            ("pcb_btn1", btn_start_x + 0 * btn_spacing, "BTN 1 (SET)",   self.send_btn1_trigger, "#00FF66"),
            ("pcb_btn2", btn_start_x + 1 * btn_spacing, "BTN 2 (F-)",    self.send_btn2_freq_down, "#38BDF8"),
            ("pcb_btn3", btn_start_x + 2 * btn_spacing, "BTN 3 (F+)",    self.send_btn3_freq_up,   "#38BDF8"),
            ("pcb_btn4", btn_start_x + 3 * btn_spacing, "BTN 4 (NUV)",   self.send_btn4_nuvoton_mode, "#F59E0B"),
            ("pcb_btn5", btn_start_x + 4 * btn_spacing, "BTN 5 (PRINT)", self.send_btn5_print_test, "#A855F7"),
            ("pcb_btn6", btn_start_x + 5 * btn_spacing, "BTN 6 (SIM)",   self.send_btn6_sim_cycle, "#EC4899"),
        ]

        for tag, bx, label, cmd, hl_color in tactile_specs:
            # Solder legs (golden)
            self.pcb_canvas.create_rectangle(bx - 14, btn_y - 8, bx + 14, btn_y + 8, fill="#D4AF37", outline="", tags=tag)
            # Switch metal chassis
            self.pcb_canvas.create_rectangle(bx - 11, btn_y - 11, bx + 11, btn_y + 11, fill="#9CA3AF", outline="#4B5563", width=1.5, tags=tag)
            # Actuator button
            actuator = self.pcb_canvas.create_oval(bx - 6, btn_y - 6, bx + 6, btn_y + 6, fill="#1F2937", outline="#111827", tags=tag)
            # Button highlight ring
            self.pcb_canvas.create_oval(bx - 3, btn_y - 3, bx + 3, btn_y + 3, fill="#374151", outline="", tags=tag)
            # Silkscreen label
            self.pcb_canvas.create_text(bx, btn_y + 17, text=label, font=("Consolas", 7, "bold"), fill="#E0E0E0", tags=tag)

            if tag == "pcb_btn1":
                # Press-and-hold detection for virtual tactile BTN 1
                self._btn1_hold_timer = None
                self._btn1_hold_triggered = False

                def on_btn1_press(event, act=actuator):
                    self._btn1_hold_triggered = False
                    self.pcb_canvas.itemconfig(act, fill="#00FF66")
                    def on_hold():
                        self._btn1_hold_triggered = True
                        self.pcb_canvas.itemconfig(act, fill="#8B5CF6")  # Vivid Purple for Setup Mode!
                        self.send_btn1_setup()
                    self._btn1_hold_timer = self.after(500, on_hold)  # 500ms hold triggers Setup Mode

                def on_btn1_release(event, act=actuator):
                    if self._btn1_hold_timer is not None:
                        self.after_cancel(self._btn1_hold_timer)
                        self._btn1_hold_timer = None
                    self.after(150, lambda: self.pcb_canvas.itemconfig(act, fill="#1F2937"))
                    if not self._btn1_hold_triggered:
                        self.send_btn1_trigger()

                def on_btn1_right_click(event, act=actuator):
                    # Direct instant setup toggle on right-click
                    self.pcb_canvas.itemconfig(act, fill="#8B5CF6")
                    self.after(200, lambda: self.pcb_canvas.itemconfig(act, fill="#1F2937"))
                    self.send_btn1_setup()

                self.pcb_canvas.tag_bind(tag, "<ButtonPress-1>", on_btn1_press)
                self.pcb_canvas.tag_bind(tag, "<ButtonRelease-1>", on_btn1_release)
                self.pcb_canvas.tag_bind(tag, "<Button-3>", on_btn1_right_click)
            else:
                def make_handler(c=cmd, act=actuator, hl=hl_color):
                    def handler(event):
                        self.pcb_canvas.itemconfig(act, fill=hl)
                        self.after(150, lambda: self.pcb_canvas.itemconfig(act, fill="#1F2937"))
                        c()
                    return handler

                self.pcb_canvas.tag_bind(tag, "<Button-1>", make_handler())

            self.pcb_canvas.tag_bind(tag, "<Enter>", lambda e: self.pcb_canvas.config(cursor="hand2"))
            self.pcb_canvas.tag_bind(tag, "<Leave>", lambda e: self.pcb_canvas.config(cursor=""))

        # 6. Header Pinout Strip at bottom
        header_y = pcb_h - 22
        pins = ["VCC", "GND", "CS", "RESET", "DC", "MOSI/TX", "SCK/RX", "LED", "MISO"]
        pin_spacing = (pcb_w - 60) / (len(pins) - 1)

        for idx, pin_name in enumerate(pins):
            px = 30 + (idx * pin_spacing)
            # Golden square/round solder pin
            self.pcb_canvas.create_rectangle(px - 4, header_y - 4, px + 4, header_y + 4, fill="#D4AF37", outline="#8E721B")
            self.pcb_canvas.create_oval(px - 2, header_y - 2, px + 2, header_y + 2, fill="#18181D", outline="")
            # Silkscreen text
            self.pcb_canvas.create_text(px, header_y - 12, text=pin_name, font=("Segoe UI", 7, "bold"), fill="#E0E0E0")

        # 6. Status LEDs (PWR and RX/TX)
        led_y = 20
        # Power LED (Green)
        self.pcb_canvas.create_oval(46, led_y - 5, 56, led_y + 5, fill="#00FF44", outline="#008822")
        self.pcb_canvas.create_text(68, led_y, text="PWR", font=("Segoe UI", 7, "bold"), fill="#C0C0C0", anchor=tk.W)

        # RX LED (Blue - blinks on UART traffic)
        self.rx_led_id = self.pcb_canvas.create_oval(pcb_w - 76, led_y - 5, pcb_w - 66, led_y + 5, fill="#1B2838", outline="#0A1828")
        self.pcb_canvas.create_text(pcb_w - 60, led_y, text="RX", font=("Segoe UI", 7, "bold"), fill="#C0C0C0", anchor=tk.W)

        # Draw splash screen
        self.draw_splash_screen()

    def draw_splash_screen(self):
        """Displays boot splash on the TFT display"""
        sw = self.tft_canvas.winfo_reqwidth()
        sh = self.tft_canvas.winfo_reqheight()

        self.tft_canvas.delete("all")
        self.tft_canvas.create_rectangle(0, 0, sw, sh, fill="#000814", outline="")

        # Grid lines (CRT/LCD subtle grid)
        for y in range(0, sh, 20 * self.scale):
            self.tft_canvas.create_line(0, y, sw, y, fill="#001428", width=1)
        for x in range(0, sw, 20 * self.scale):
            self.tft_canvas.create_line(x, 0, x, sh, fill="#001428", width=1)

        # Frame border
        self.tft_canvas.create_rectangle(4, 4, sw - 4, sh - 4, outline="#0077B6", width=2)

        # Center Text
        cx = sw // 2
        cy = sh // 2
        self.tft_canvas.create_text(cx, cy - 36, text="ILI9341 TFT DISPLAY",
                                    font=("Consolas", 14 * self.scale // 2, "bold"), fill="#00B4D8")
        self.tft_canvas.create_text(cx, cy - 10, text="UART / SERIAL EMULATOR",
                                    font=("Consolas", 11 * self.scale // 2, "bold"), fill="#90E0EF")
        self.tft_canvas.create_text(cx, cy + 18, text=f"320 x 240 RGB565 | FW: {FIRMWARE_VERSION}",
                                    font=("Consolas", 9 * self.scale // 2), fill="#48CAE4")
        self.tft_canvas.create_text(cx, cy + 42, text="Waiting for COM input...",
                                    font=("Consolas", 9 * self.scale // 2, "italic"), fill="#F77F00")

    # --------------------------------------------------------------------------
    # SERIAL LOG / INSPECTOR UI
    # --------------------------------------------------------------------------
    def setup_inspector_ui(self):
        # Header
        hdr = tk.Frame(self.inspector_frame, bg="#26262F", padx=8, pady=6)
        hdr.pack(fill=tk.X)

        tk.Label(hdr, text="Serial Command Inspector", font=("Segoe UI", 9, "bold"), fg="#FFFFFF", bg="#26262F").pack(side=tk.LEFT)

        clear_log_btn = tk.Button(hdr, text="Clear Log", font=("Segoe UI", 8), fg="#B0B0B0", bg="#363642",
                                  bd=0, padx=6, pady=1, command=self.clear_inspector_log)
        clear_log_btn.pack(side=tk.RIGHT)

        # Log Text Box
        self.log_text = tk.Text(self.inspector_frame, bg="#111116", fg="#00FF66", font=("Consolas", 9),
                                bd=0, highlightthickness=0, wrap=tk.NONE)
        scroll_y = ttk.Scrollbar(self.inspector_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll_y.set)

        # Preconfigure text tags once
        self.log_text.tag_config("rx", foreground="#64DFDF")
        self.log_text.tag_config("tx", foreground="#FFD166")
        self.log_text.tag_config("ts", foreground="#6C757D")

        scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Status Bar
        self.status_bar = tk.Frame(self.inspector_frame, bg="#1B1B22", padx=8, pady=4)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)

        self.rx_label = tk.Label(self.status_bar, text="Packets: 0", font=("Segoe UI", 8), fg="#A0A0A0", bg="#1B1B22")
        self.rx_label.pack(side=tk.LEFT, padx=(0, 10))

        self.fps_label = tk.Label(self.status_bar, text="FPS: 0", font=("Segoe UI", 8), fg="#00FF66", bg="#1B1B22")
        self.fps_label.pack(side=tk.LEFT)

        self.fw_label = tk.Label(self.status_bar, text=f"Firmware: {FIRMWARE_VERSION}", font=("Segoe UI", 8, "bold"), fg="#00B4D8", bg="#1B1B22")
        self.fw_label.pack(side=tk.RIGHT, padx=(0, 5))

        # Command input for manual testing
        input_frame = tk.Frame(self.inspector_frame, bg="#26262F", padx=4, pady=4)
        input_frame.pack(fill=tk.X, side=tk.BOTTOM)

        self.manual_cmd = tk.Entry(input_frame, bg="#1A1A22", fg="#FFFFFF", font=("Consolas", 9), insertbackground="#FFFFFF")
        self.manual_cmd.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        self.manual_cmd.bind("<Return>", lambda e: self.send_manual_cmd())

        send_btn = tk.Button(input_frame, text="Send", font=("Segoe UI", 8, "bold"), fg="#FFFFFF", bg="#0077B6",
                             bd=0, padx=8, command=self.send_manual_cmd)
        send_btn.pack(side=tk.RIGHT)

    def log_packet(self, msg, direction="RX"):
        self.log_packets_batch([msg], direction=direction)

    def log_packets_batch(self, msgs, direction="RX"):
        if not msgs:
            return
        ts = time.strftime("%H:%M:%S")
        prefix = "◀ " if direction == "RX" else "▶ "
        color_tag = "rx" if direction == "RX" else "tx"

        text_chunk = "".join(f"[{ts}] {prefix}{m}\n" for m in msgs)
        self.log_text.insert(tk.END, text_chunk, color_tag)

        # Limit lines to prevent memory bloat
        line_count = int(self.log_text.index('end-1c').split('.')[0])
        if line_count > 350:
            self.log_text.delete("1.0", f"{line_count - 250}.0")

        self.log_text.see(tk.END)

    def clear_inspector_log(self):
        self.log_text.delete("1.0", tk.END)

    def send_manual_cmd(self):
        cmd = self.manual_cmd.get().strip()
        if cmd:
            self.log_packet(cmd, direction="TX")
            self.execute_command(cmd)
            self.manual_cmd.delete(0, tk.END)

    # --------------------------------------------------------------------------
    # SERIAL PORT MANAGEMENT
    # --------------------------------------------------------------------------
    def refresh_com_ports(self):
        if not HAS_SERIAL:
            self.port_combo['values'] = ["No pyserial"]
            self.port_combo.set("No pyserial")
            return

        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            self.port_combo['values'] = ["No COM Ports"]
            self.port_combo.set("No COM Ports")
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
                messagebox.showerror("Error", "pyserial is not installed!")
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

            # Start background listener thread
            self.serial_thread = threading.Thread(target=self.serial_reader_worker, daemon=True)
            self.serial_thread.start()
        except Exception as e:
            self.is_connected = False
            if not silent:
                messagebox.showerror("Connection Error", f"Failed to open {port}:\n{str(e)}")
            else:
                self.log_packet(f"Auto-connect failed on {port}: {e}", "TX")

    def disconnect_serial(self):
        self.is_connected = False
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass
        self.connect_btn.configure(text="Connect", bg="#008037")
        self.log_packet("Serial port disconnected", "TX")

    def serial_reader_worker(self):
        """Dedicated background thread reading UART data line by line"""
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
            except Exception as e:
                break
            time.sleep(0.001)

    # --------------------------------------------------------------------------
    # COMMAND QUEUE & PARSER DISPATCHER
    # --------------------------------------------------------------------------
    def process_command_queue(self):
        """Processes up to 500 commands per GUI tick with batched logging for maximum responsiveness"""
        batch_limit = 500
        processed = 0
        rx_logs = []

        while not self.cmd_queue.empty() and processed < batch_limit:
            try:
                raw_cmd = self.cmd_queue.get_nowait()
                self.execute_command(raw_cmd)
                rx_logs.append(raw_cmd)
                self.rx_counter += 1
                self.fps_counter += 1
                processed += 1
            except queue.Empty:
                break

        if rx_logs:
            self.blink_rx_led()
            display_logs = [m for m in rx_logs if not m.startswith("STROW,")]
            if display_logs:
                self.log_packets_batch(display_logs, "RX")

        # Schedule next tick
        self.after(5, self.process_command_queue)

    def blink_rx_led(self):
        if not self._rx_led_active:
            self._rx_led_active = True
            try:
                self.pcb_canvas.itemconfig(self.rx_led_id, fill="#00D2FF")
                self.after(50, self._turn_off_rx_led)
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
    # GRAPHICS ENGINE & COMMAND EXECUTION
    # --------------------------------------------------------------------------
    def execute_command(self, cmd_line):
        """
        Interprets ILI9341 commands:
        CLS,color
        PIX,x,y,color
        LINE,x0,y0,x1,y1,color
        RECT,x,y,w,h,color,fill
        CIRC,x,y,r,color,fill
        TXT,x,y,size,fgColor,bgColor,text...
        ROTA,0-3
        """
        # Check for ESP32 boot banner / firmware broadcast
        if "[ESP32 ST7920 SPI Sniffer Ready" in cmd_line:
            m = re.search(r'Ready\s*\(([^)]+)\)', cmd_line)
            if m:
                live_ver = m.group(1)
                self.fw_label.configure(text=f"Firmware: {live_ver}")
                self.title(f"Virtual 2.8\" TFT Display (ILI9341) - FW: {live_ver} - UART Serial Monitor")

        parts = [p.strip() for p in cmd_line.split(',')]
        if not parts:
            return

        cmd = parts[0].upper()

        try:
            if cmd == "CLS":
                # Clear screen: CLS,color
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
                    # Solid pixel-aligned horizontal span (essential for writePixels scanlines)
                    rx1 = min(x0, x1)
                    rx2 = max(x0, x1) + self.scale
                    self.tft_canvas.create_rectangle(rx1, y0, rx2, y0 + self.scale, fill=color, outline=color)
                elif x0 == x1:
                    # Solid pixel-aligned vertical span
                    ry1 = min(y0, y1)
                    ry2 = max(y0, y1) + self.scale
                    self.tft_canvas.create_rectangle(x0, ry1, x0 + self.scale, ry2, fill=color, outline=color)
                else:
                    self.tft_canvas.create_line(x0, y0, x1, y1, fill=color, width=self.scale)

            elif cmd == "RECT":
                # Rect: RECT,x,y,w,h,color,[fill:0|1]
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                w = int(parts[3]) * self.scale
                h = int(parts[4]) * self.scale
                color = rgb565_to_hex(parts[5]) or "#000000"
                is_fill = int(parts[6]) if len(parts) > 6 else 0
                if is_fill:
                    # Clean up covered canvas items within this rectangle to prevent item accumulation lag
                    for item in self.tft_canvas.find_enclosed(x - 1, y - 1, x + w + 1, y + h + 1):
                        self.tft_canvas.delete(item)
                    self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill=color, outline=color)
                else:
                    self.tft_canvas.create_rectangle(x, y, x + w, y + h, fill="", outline=color, width=self.scale)

            elif cmd == "RRECT":
                # Rounded Rect: RRECT,x,y,w,h,r,color,[fill:0|1]
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                w = int(parts[3]) * self.scale
                h = int(parts[4]) * self.scale
                r = int(parts[5]) * self.scale
                color = rgb565_to_hex(parts[6]) or "#000000"
                is_fill = int(parts[7]) if len(parts) > 7 else 0

                x1, y1, x2, y2 = x, y, x + w, y + h
                if r <= 0:
                    r = 1
                if r * 2 > w:
                    r = w // 2
                if r * 2 > h:
                    r = h // 2

                points = [
                    x1 + r, y1,
                    x1 + r, y1,
                    x2 - r, y1,
                    x2 - r, y1,
                    x2, y1,
                    x2, y1 + r,
                    x2, y1 + r,
                    x2, y2 - r,
                    x2, y2 - r,
                    x2, y2,
                    x2 - r, y2,
                    x2 - r, y2,
                    x1 + r, y2,
                    x1 + r, y2,
                    x1, y2,
                    x1, y2 - r,
                    x1, y2 - r,
                    x1, y1 + r,
                    x1, y1 + r,
                    x1, y1,
                ]
                if is_fill:
                    for item in self.tft_canvas.find_enclosed(x - 1, y - 1, x + w + 1, y + h + 1):
                        self.tft_canvas.delete(item)
                    for item in self.tft_canvas.find_overlapping(x + 4, y + 4, x + w - 4, y + h - 4):
                        tags = self.tft_canvas.gettags(item)
                        if "txt_item" in tags or "txt_bg" in tags:
                            self.tft_canvas.delete(item)
                    self.tft_canvas.create_polygon(points, fill=color, outline=color, smooth=True)
                else:
                    self.tft_canvas.create_polygon(points, fill="", outline=color, width=self.scale, smooth=True)

            elif cmd == "CIRC":
                # Circle: CIRC,x,y,r,color,[fill:0|1]
                x = int(parts[1]) * self.scale
                y = int(parts[2]) * self.scale
                r = int(parts[3]) * self.scale
                color = rgb565_to_hex(parts[4]) or "#000000"
                is_fill = int(parts[5]) if len(parts) > 5 else 0
                if is_fill:
                    self.tft_canvas.create_oval(x - r, y - r, x + r, y + r, fill=color, outline=color)
                else:
                    self.tft_canvas.create_oval(x - r, y - r, x + r, y + r, fill="", outline=color, width=self.scale)

            elif cmd == "TXT":
                # Text: TXT,x,y,size,fgColor,bgColor,text...
                # Note: text can contain commas, so recombine parts[6:]
                x = int(parts[1])
                y = int(parts[2])
                size = max(1, int(parts[3]))
                fg = rgb565_to_hex(parts[4]) or "#FFFFFF"
                bg = rgb565_to_hex(parts[5]) if len(parts) > 5 else None
                text_str = ",".join(parts[6:]) if len(parts) > 6 else ""

                self.render_gfx_text(x, y, size, fg, bg, text_str)

            elif cmd == "ROTA":
                # Rotation: ROTA,0-3
                rot = int(parts[1]) & 3
                if rot != self.rotation:
                    self.rotation = rot
                    rot_names = ["0 (Portrait)", "1 (Landscape)", "2 (Inv-Port)", "3 (Inv-Land)"]
                    self.rot_combo.set(rot_names[rot])
                    self.draw_tft_hardware()

            elif cmd == "STROW":
                # High-speed compressed ST7920 row: STROW,row,fgColor,bgColor,hex32
                if len(parts) > 4:
                    row = int(parts[1])
                    fg = rgb565_to_hex(parts[2]) or "#FFFFFF"
                    bg = rgb565_to_hex(parts[3]) or "#000013"
                    hex_str = parts[4]
                    self.render_st7920_row(row, fg, bg, hex_str)

        except Exception as e:
            # Silently ignore or log malformed packet
            pass

    def render_st7920_row(self, row, fg, bg, hex_str):
        """
        Renders a 128-pixel ST7920 scanline at native 2x scale (256x128 centered at 32, 76).
        Deletes previous row items via tag to guarantee ZERO canvas item accumulation!
        """
        if len(hex_str) < 32:
            return

        offset_x = 32
        offset_y = 76

        tag = f"st_{row}"
        self.tft_canvas.delete(tag)

        try:
            row_bytes = bytes.fromhex(hex_str[:32])
        except ValueError:
            return

        sy1 = (offset_y + row * 2) * self.scale
        sy2 = (offset_y + (row + 1) * 2) * self.scale

        col_idx = 0
        in_run = False
        run_start = 0

        for b in row_bytes:
            for bit_pos in (7, 6, 5, 4, 3, 2, 1, 0):
                is_on = bool((b >> bit_pos) & 1)
                if is_on:
                    if not in_run:
                        in_run = True
                        run_start = col_idx
                else:
                    if in_run:
                        in_run = False
                        sx1 = (offset_x + run_start * 2) * self.scale
                        sx2 = (offset_x + col_idx * 2) * self.scale
                        self.tft_canvas.create_rectangle(sx1, sy1, sx2, sy2, fill=fg, outline=fg, tags=tag)
                col_idx += 1

        if in_run:
            sx1 = (offset_x + run_start * 2) * self.scale
            sx2 = (offset_x + col_idx * 2) * self.scale
            self.tft_canvas.create_rectangle(sx1, sy1, sx2, sy2, fill=fg, outline=fg, tags=tag)

    def render_gfx_text(self, x, y, size, fg, bg, text_str):
        """Dispatches text rendering to either Modern TrueType or Retro 5x7 font engine"""
        if self.font_mode == "modern":
            self.render_modern_text(x, y, size, fg, bg, text_str)
        else:
            self.render_retro_5x7_text(x, y, size, fg, bg, text_str)

    def render_modern_text(self, x, y, size, fg, bg, text_str):
        """
        Renders crisp, anti-aliased TrueType vector typography (Segoe UI / Roboto).
        Uses location-based tagging to cleanly replace previous text without accumulation.
        """
        # Single-character text (e.g. cursor overlay) uses distinct tag so it doesn't delete full strings at (x, y)
        tag = f"cur_{x}_{y}" if len(text_str) <= 1 else f"txt_{x}_{y}"
        self.tft_canvas.delete(tag)

        cx = x * self.scale
        cy = y * self.scale

        # Typographic scale mapped to UI hierarchy:
        if size == 1:
            f_size = max(8, int(7.5 * self.scale))
            weight = "bold"
        elif size == 2:
            f_size = max(10, int(10.5 * self.scale))
            weight = "bold"
        elif size == 3:
            f_size = max(15, int(15 * self.scale))
            weight = "bold"
        else: # size >= 4
            f_size = max(20, int(22 * self.scale))
            weight = "bold"

        font_spec = (self.modern_font_family, f_size, weight)

        if bg is not None and bg.lower() not in ("none", ""):
            t_id = self.tft_canvas.create_text(cx, cy, text=text_str, font=font_spec, fill=fg, anchor="nw", tags=(tag, "txt_item"))
            bbox = self.tft_canvas.bbox(t_id)
            if bbox:
                # Ensure width covers at least expected character slot width so narrower/wider digit changes never leave fringes
                est_w = int(len(text_str) * (f_size * 0.72))
                rx2 = max(bbox[2] + 1, bbox[0] + est_w)
                bg_id = self.tft_canvas.create_rectangle(bbox[0]-1, bbox[1]-1, rx2, bbox[3]+1, fill=bg, outline=bg, tags=(tag, "txt_bg"))
                try:
                    self.tft_canvas.tag_lower(bg_id, t_id)
                except Exception:
                    pass
        else:
            self.tft_canvas.create_text(cx, cy, text=text_str, font=font_spec, fill=fg, anchor="nw", tags=(tag, "txt_item"))

    def render_retro_5x7_text(self, x, y, size, fg, bg, text_str):
        """
        Renders authentic Adafruit_GFX 5x7 font characters onto canvas.
        Each character is 5x7 with 1 pixel spacing between chars.
        """
        cur_x = x
        char_w = 6 * size

        for char in text_str:
            glyph = FONT_5X7.get(char, FONT_5X7.get('?'))

            if bg is not None and bg.lower() != "none" and bg.lower() != "":
                bx1 = cur_x * self.scale
                by1 = y * self.scale
                bx2 = (cur_x + 6 * size) * self.scale
                by2 = (y + 8 * size) * self.scale
                for item in self.tft_canvas.find_enclosed(bx1 - 1, by1 - 1, bx2 + 1, by2 + 1):
                    self.tft_canvas.delete(item)
                self.tft_canvas.create_rectangle(bx1, by1, bx2, by2, fill=bg, outline="")

            for col_idx, col_bits in enumerate(glyph):
                row_idx = 0
                while row_idx < 8:
                    if (col_bits >> row_idx) & 1:
                        start_row = row_idx
                        while row_idx < 8 and ((col_bits >> row_idx) & 1):
                            row_idx += 1
                        height_rows = row_idx - start_row
                        px = (cur_x + (col_idx * size)) * self.scale
                        py = (y + (start_row * size)) * self.scale
                        pw = size * self.scale
                        ph = height_rows * size * self.scale
                        self.tft_canvas.create_rectangle(px, py, px + pw, py + ph, fill=fg, outline=fg)
                    else:
                        row_idx += 1

            cur_x += char_w

    # --------------------------------------------------------------------------
    # CONTROLS & EVENT HANDLERS
    # --------------------------------------------------------------------------
    def on_scale_change(self, event=None):
        val = self.scale_combo.get()
        self.scale = int(val.replace("x", ""))
        self.draw_tft_hardware()

    def on_rotation_change(self, event=None):
        val = self.rot_combo.get()
        self.rotation = int(val.split()[0])
        self.draw_tft_hardware()

    def on_global_key(self, event):
        """Global keyboard shortcuts: 'v' for mode, 'm' for theme, 'c' for clear, '1'/'s'/'2'/'3' for Degasser"""
        # Don't intercept if user is typing in manual command entry box
        if self.focus_get() == getattr(self, 'manual_cmd', None):
            return
        ch = (event.char or "").lower()
        sym = (event.keysym or "").lower()
        if ch == 'v' or sym == 'v':
            self.send_switch_mode()
        elif ch == 'm' or sym == 'm':
            self.send_switch_theme()
        elif ch == 'f' or sym == 'f':
            self.toggle_font_mode()
        elif ch == 'c' or sym == 'c':
            self.execute_command("CLS,0x0000")
        elif ch == '1' or sym == '1' or ch == 'd':
            self.send_btn1_trigger()
        elif ch == 's' or sym == 's':
            self.send_btn1_setup()
        elif ch == '2' or sym == '2' or ch == '-' or sym == 'minus':
            self.send_btn2_freq_down()
        elif ch == '3' or sym == '3' or ch == '+' or sym == 'plus' or sym == 'equal':
            self.send_btn3_freq_up()
        elif ch == '4' or sym == '4' or ch == 'n' or sym == 'n' or ch == 'N':
            self.send_btn4_nuvoton_mode()
        elif ch == '5' or sym == '5':
            self.send_btn5_print_test()
        elif ch == '6' or sym == '6':
            self.send_btn6_sim_cycle()
        elif ch == '7' or sym == '7':
            self.send_btn7_bt_return()

    def toggle_font_mode(self):
        """Toggles between Modern anti-aliased TrueType font and Retro 5x7 bitmap font"""
        if self.font_mode == "modern":
            self.font_mode = "retro"
            self.font_btn.configure(text="🔤 Font: Retro", bg="#5C677D")
            self.fps_label.configure(text="FONT: Retro 5x7")
        else:
            self.font_mode = "modern"
            self.font_btn.configure(text="🔤 Font: Modern", bg="#0081A7")
            self.fps_label.configure(text=f"FONT: Modern ({self.modern_font_family})")

    def send_btn1_trigger(self):
        """Sends '1' to ESP32: Degas Start Cycle / Abort Active Cycle / Increment Duration in Setup"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'1')
                self.ser.flush()
                self.log_packet("BTN 1 Trigger / Abort / Duration Inc ('1')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 1 (Degas Trigger/Abort) ['1']", fg="#00FF66")
            except Exception as e:
                self.log_packet(f"Error sending '1': {e}", "TX")
        else:
            self.log_packet("Cannot send BTN 1: COM port not connected", "TX")

    def send_btn1_setup(self):
        """Sends 's' to ESP32: Setup Mode Toggle (Simulating BTN 1 Long-Press)"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b's')
                self.ser.flush()
                self.log_packet("BTN 1 HOLD: Setup Mode Toggle ('s')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 1 HOLD (Setup Mode) ['s']", fg="#A78BFA")
            except Exception as e:
                self.log_packet(f"Error sending 's': {e}", "TX")
        else:
            self.log_packet("Cannot send Setup Mode: COM port not connected", "TX")

    def send_btn2_freq_down(self):
        """Sends '2' to ESP32: Frequency Decrement (-50 Hz)"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'2')
                self.ser.flush()
                self.log_packet("BTN 2: Freq Tune Down ('2')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 2 (Freq Down) ['2']", fg="#38BDF8")
            except Exception as e:
                self.log_packet(f"Error sending '2': {e}", "TX")
        else:
            self.log_packet("Cannot send BTN 2: COM port not connected", "TX")

    def send_btn3_freq_up(self):
        """Sends '3' to ESP32: Frequency Increment"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'3')
                self.ser.flush()
                self.log_packet("BTN 3: Freq Tune Up ('3')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 3 (Freq Up) ['3']", fg="#38BDF8")
            except Exception as e:
                self.log_packet(f"Error sending '3': {e}", "TX")
        else:
            self.log_packet("Cannot send BTN 3: COM port not connected", "TX")

    def send_btn4_nuvoton_mode(self):
        """Sends '4' to ESP32: Toggle Nuvoton Auto / Manual Start Mode in Setup"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'4')
                self.ser.flush()
                self.log_packet("BTN 4: Toggle Nuvoton Mode ('4')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 4 (Nuvoton Mode) ['4']", fg="#F59E0B")
            except Exception as e:
                self.log_packet(f"Error sending '4': {e}", "TX")
        else:
            self.log_packet("Cannot send BTN 4: COM port not connected", "TX")

    def send_btn5_print_test(self):
        """Sends '5' to ESP32: Manual Thermal Printer Test Receipt"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'5')
                self.ser.flush()
                self.log_packet("BTN 5: Manual Thermal Printer Test ('5')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BTN 5 (Print Test) ['5']", fg="#A855F7")
            except Exception as e:
                self.log_packet(f"Error sending '5': {e}", "TX")
        else:
            self.log_packet("Cannot send BTN 5: COM port not connected", "TX")

    def send_btn6_sim_cycle(self):
        """Sends '6' to ESP32: Nuvoton Simulation Demo Test Cycle (Cow / Buffalo / Mix)"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'6')
                self.ser.flush()
                self.log_packet("SIM 6: Nuvoton Test Cycle Injection ('6')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: SIM 6 (Demo Cycle Inject) ['6']", fg="#EC4899")
            except Exception as e:
                self.log_packet(f"Error sending '6': {e}", "TX")
        else:
            self.log_packet("Cannot send SIM 6: COM port not connected", "TX")

    def send_btn7_bt_return(self):
        """Sends '7' to ESP32: Inject Simulated Bluetooth App Return Response with Updated Dairy Data"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'7')
                self.ser.flush()
                self.log_packet("BT 7: Inject Sample Bluetooth Return Data ('7')", "TX")
                if hasattr(self, 'degas_status_lbl'):
                    self.degas_status_lbl.configure(text="TX: BT 7 (Inject Bluetooth Return) ['7']", fg="#818CF8")
            except Exception as e:
                self.log_packet(f"Error sending '7': {e}", "TX")
        else:
            self.log_packet("Cannot send BT 7: COM port not connected", "TX")

    def send_switch_mode(self):
        """Sends 'v' to ESP32 to toggle between Original ST7920 Screen and Modern Dashboard"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'v')
                self.ser.flush()
                self.log_packet("Switch Display Mode ('v')", "TX")
                self.fps_label.configure(text="MODE SWITCHED ('v')")
            except Exception as e:
                self.log_packet(f"Error sending 'v': {e}", "TX")
        else:
            self.log_packet("Cannot switch mode: COM port not connected", "TX")

    def send_switch_theme(self):
        """Sends 'm' to ESP32 to cycle color themes"""
        if not self.is_connected or not self.ser or not self.ser.is_open:
            self.connect_serial(silent=True)

        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b'm')
                self.ser.flush()
                self.log_packet("Switch Color Theme ('m')", "TX")
                self.fps_label.configure(text="THEME SWITCHED ('m')")
            except Exception as e:
                self.log_packet(f"Error sending 'm': {e}", "TX")
        else:
            self.log_packet("Cannot switch theme: COM port not connected", "TX")

    def save_snapshot(self):
        """Saves current TFT screen content to PostScript / image file"""
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
                messagebox.showerror("Error", f"Failed to save snapshot:\n{str(e)}")

    # --------------------------------------------------------------------------
    # BUILT-IN HARDWARE SIMULATOR (DEMO MODE)
    # --------------------------------------------------------------------------
    def toggle_simulation(self):
        if self.sim_running:
            self.sim_running = False
            self.sim_stop_event.set()
            if self.sim_thread and self.sim_thread.is_alive():
                self.sim_thread.join(timeout=0.2)

            # Instantly flush all remaining queued commands so display stops immediately
            drained_count = 0
            while not self.cmd_queue.empty():
                try:
                    self.cmd_queue.get_nowait()
                    drained_count += 1
                except queue.Empty:
                    break

            self.sim_btn.configure(text="▶ Run Demo", bg="#6930C3")
            self.log_packet(f"Demo stopped ({drained_count} pending commands flushed)", "TX")
        else:
            self.sim_running = True
            self.sim_stop_event.clear()
            self.sim_btn.configure(text="⏹ Stop Demo", bg="#D90429")
            self.sim_thread = threading.Thread(target=self.demo_simulation_worker, daemon=True)
            self.sim_thread.start()

    def demo_simulation_worker(self):
        """Simulates an Arduino running an instrument cluster with animated gauges and sine waves"""
        try:
            # Initial Clear & Header
            if self.sim_stop_event.is_set():
                return
            self.cmd_queue.put("CLS,0x0000") # Black
            if self.sim_stop_event.wait(0.04):
                return

            self.cmd_queue.put("RECT,0,0,320,24,0x001F,1") # Header blue bar
            self.cmd_queue.put("TXT,8,4,2,0xFFFF,0x001F,VEHICLE TELEMETRY")
            self.cmd_queue.put("RECT,0,222,320,18,0x2104,1") # Footer bar
            self.cmd_queue.put("TXT,10,226,1,0x07E0,0x2104,SYSTEM OK | UART: 115200 BAUD")

            # Static card borders
            self.cmd_queue.put("RECT,8,32,148,88,0x7BEF,0") # Speed card
            self.cmd_queue.put("TXT,16,40,1,0xC618,0x0000,SPEED (KM/H)")

            self.cmd_queue.put("RECT,164,32,148,88,0x7BEF,0") # RPM card
            self.cmd_queue.put("TXT,172,40,1,0xC618,0x0000,ENGINE RPM")

            self.cmd_queue.put("RECT,8,128,304,86,0x7BEF,0") # Waveform graph
            self.cmd_queue.put("TXT,16,134,1,0xC618,0x0000,REAL-TIME SENSOR OSCILLOSCOPE")

            t = 0.0
            while not self.sim_stop_event.is_set():
                # Flow control: don't flood queue if GUI hasn't caught up
                if self.cmd_queue.qsize() > 25:
                    if self.sim_stop_event.wait(0.02):
                        break
                    continue

                # 1. Animate Speedometer
                speed = int(60 + 35 * math.sin(t * 1.5))
                speed_str = f"{speed:3d}"
                self.cmd_queue.put(f"TXT,32,62,4,0xFFE0,0x0000,{speed_str}")

                # 2. Animate RPM Gauge
                rpm = int(2400 + 1200 * math.cos(t * 1.2))
                rpm_str = f"{rpm:4d}"
                self.cmd_queue.put(f"TXT,186,62,4,0x07FF,0x0000,{rpm_str}")

                # 3. Dynamic Progress Bar for Throttle
                bar_w = int((speed / 100.0) * 120)
                self.cmd_queue.put("RECT,20,102,124,10,0x0000,1") # clear bar
                bar_color = "0x07E0" if speed < 75 else ("0xFFE0" if speed < 88 else "0xF800")
                self.cmd_queue.put(f"RECT,20,102,{bar_w},10,{bar_color},1")

                # 4. Animated Sine Wave Oscilloscope
                self.cmd_queue.put("RECT,16,148,288,58,0x0000,1")
                self.cmd_queue.put("LINE,16,177,304,177,0x2104")

                # Compute and draw wave segments (step 8 for crisp 35-point waveform)
                prev_x, prev_y = None, None
                for idx, x_step in enumerate(range(20, 300, 8)):
                    if self.sim_stop_event.is_set():
                        return
                    y_val = int(177 + 24 * math.sin((t * 4) + (idx * 0.25)))
                    if prev_x is not None:
                        self.cmd_queue.put(f"LINE,{prev_x},{prev_y},{x_step},{y_val},0x07E0")
                    prev_x, prev_y = x_step, y_val

                t += 0.08
                if self.sim_stop_event.wait(0.06):
                    break
        finally:
            self.sim_running = False

    def on_close(self):
        """Cleanly terminates background threads and exits"""
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

