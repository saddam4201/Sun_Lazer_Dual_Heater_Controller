# Virtual 2.8" TFT Display (ILI9341 320x240) - Bench Test Terminal

Standalone desktop display emulator and test console for the **Sun Lazer Dual Heater Controller**, enabling full hardware-in-the-loop bench testing with only an ESP32 board connected via USB (no physical TFT display or buttons required).

---

## Features

- **Realistic 2.8" ILI9341 LCD (320x240)**:
  - Red PCB breakout board bezel with gold corner pads and silkscreen.
  - Active screen with 1x, 2x, and 3x crisp integer scaling.
  - Blinking **RX LED** for live UART packet activity and **PWR LED**.
- **Interactive Navigation Buttons**:
  - `▲ UP [1]`: Navigate up in menus / increment values / motor jog up.
  - `▼ DOWN [2]`: Navigate down in menus / decrement values / motor jog down.
  - `◀ LEFT [3]`: Cancel / back / cancel force-start.
  - `▶ RIGHT [4]`: Next field / enter submenus.
  - `✔ OK [5]`: Confirm / select / start heating process.
- **Bench-Testing Controls**:
  - **Down Limit Switch**: Toggle between `OPEN` and `CLOSED` (`:down on` / `:down off`).
  - **Home Limit Switch**: Toggle between `OPEN` and `CLOSED` (`:home on` / `:home off`).
  - **Simulated Sensors**: Quick dialogs to inject temperatures (`:h1`, `:h2`) and torque (`:torque`).
  - **Reset Failures**: Reset limit switch failure counters (`:reset_fail`).
- **Keyboard Hotkeys**:
  - `Up Arrow` or `1`: UP
  - `Down Arrow` or `2`: DOWN
  - `Left Arrow` or `3`: LEFT
  - `Right Arrow` or `4`: RIGHT
  - `Enter` / `Space` or `5`: OK
- **Serial Command Inspector**:
  - Real-time color-coded RX / TX packet monitor.
  - Live FPS and packet counter.
  - Manual command sender for raw testing.
- **Snapshot Tool**:
  - One-click screenshot capture of the active TFT canvas.

---

## Quick Start

### 1. Launching the Application
Double-click `run_virtual_tft.bat` in this folder:
```cmd
run_virtual_tft.bat
```
*(The script automatically verifies Python and installs `pyserial` if needed.)*

### 2. Connecting to ESP32
1. Select your ESP32's **COM Port** from the dropdown.
2. Ensure the baud rate is set to `115200`.
3. Click **Connect**. The Virtual TFT screen will initialize and display the live Sun Lazer UI.

### 3. Standalone Demo Mode
If no ESP32 is connected, click **▶ Run Demo** in the top toolbar to test the canvas rendering, live gauges, animated temperatures, and timer.

---

## Firmware Configuration (`config.h`)

To toggle between bench-testing mode and physical hardware display:

```c
// Enable Virtual TFT display over UART Serial (for bench testing)
#ifndef ENABLE_UART_VIRTUAL_TFT
#define ENABLE_UART_VIRTUAL_TFT 1
#endif

// Enable Physical SPI ILI9341 TFT display (set to 0 for bench testing with only ESP32)
#ifndef ENABLE_PHYSICAL_TFT
#define ENABLE_PHYSICAL_TFT 0
#endif
```

- When `ENABLE_PHYSICAL_TFT` is `0`, all SPI transactions to the unattached screen are bypassed, preventing bus contention and timeouts.
- When `ENABLE_PHYSICAL_TFT` is `1` and `ENABLE_UART_VIRTUAL_TFT` is `1`, both displays operate simultaneously (dual-output mirror).
