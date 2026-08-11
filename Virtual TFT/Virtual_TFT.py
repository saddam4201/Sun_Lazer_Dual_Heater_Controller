import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports

class TFTTerminalApp:
    def __init__(self, root):
        self.root = root
        self.root.title("2.4\" TFT Terminal")
        
        # Standard 2.4" TFT resolution in landscape (320x240)
        self.root.geometry("320x240")
        self.root.resizable(False, False)
        self.root.configure(bg="#000000")

        self.serial_port = None
        self.is_reading = False

        self.create_widgets()
        self.refresh_ports()

    def create_widgets(self):
        # --- Top Control Bar ---
        control_frame = tk.Frame(self.root, bg="#111111", height=30)
        control_frame.pack(fill=tk.X, side=tk.TOP)

        # Port Dropdown
        self.port_var = tk.StringVar()
        self.port_dropdown = ttk.Combobox(control_frame, textvariable=self.port_var, width=8, state="readonly")
        self.port_dropdown.pack(side=tk.LEFT, padx=2, pady=2)

        # Baudrate Dropdown
        self.baud_var = tk.StringVar(value="9600")
        baud_rates = ["4800", "9600", "19200", "38400", "57600", "115200"]
        self.baud_dropdown = ttk.Combobox(control_frame, textvariable=self.baud_var, values=baud_rates, width=6, state="readonly")
        self.baud_dropdown.pack(side=tk.LEFT, padx=2, pady=2)

        # Connect / Disconnect Button
        self.btn_connect = tk.Button(control_frame, text="Connect", command=self.toggle_connection, bg="#333333", fg="#FFFFFF", font=("Helvetica", 8))
        self.btn_connect.pack(side=tk.LEFT, padx=2, pady=2)

        # Refresh Ports Button
        self.btn_refresh = tk.Button(control_frame, text="↻", command=self.refresh_ports, bg="#333333", fg="#FFFFFF", font=("Helvetica", 8, "bold"))
        self.btn_refresh.pack(side=tk.LEFT, padx=2, pady=2)

        # --- TFT Display Area ---
        # Stylized terminal text area mimicking a small TFT matrix screen
        self.terminal = scrolledtext.ScrolledText(
            self.root, 
            bg="#000000", 
            fg="#00FF00", 
            insertbackground="#00FF00", 
            font=("Consolas", 8),
            wrap=tk.WORD,
            bd=0,
            highlightthickness=0
        )
        self.terminal.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)

    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_dropdown['values'] = ports
        if ports:
            self.port_dropdown.current(0)

    def toggle_connection(self):
        if self.is_reading:
            self.disconnect_serial()
        else:
            self.connect_serial()

    def connect_serial(self):
        port = self.port_var.get()
        baud = self.baud_var.get()

        if not port:
            self.append_terminal("[Error] No COM port selected.\n")
            return

        try:
            self.serial_port = serial.Serial(port, int(baud), timeout=1)
            self.is_reading = True
            self.btn_connect.config(text="Disconnect", bg="#880000")
            self.append_terminal(f"[Connected to {port} @ {baud}]\n")

            # Start reading thread
            self.read_thread = threading.Thread(target=self.read_from_serial, daemon=True)
            self.read_thread.start()
        except Exception as e:
            self.append_terminal(f"[Connection Error] {e}\n")

    def disconnect_serial(self):
        self.is_reading = False
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.btn_connect.config(text="Connect", bg="#333333")
        self.append_terminal("[Disconnected]\n")

    def read_from_serial(self):
        while self.is_reading and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting > 0:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore')
                    self.append_terminal(line)
            except Exception as e:
                self.append_terminal(f"\n[Read Error] {e}\n")
                break
            time.sleep(0.05)

    def append_terminal(self, text):
        # Update text area in thread-safe manner
        self.root.after(0, self._update_text_widget, text)

    def _update_text_widget(self, text):
        self.terminal.insert(tk.END, text)
        self.terminal.see(tk.END)

if __name__ == "__main__":
    root = tk.Tk()
    app = TFTTerminalApp(root)
    root.mainloop()
