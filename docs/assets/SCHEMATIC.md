Sun Lazer Dual Heater Controller — Hardware Schematic & Wiring Guide

Purpose
This file provides a wiring schematic and connection details for the ESP32-based Dual Heater Controller firmware. It lists every device used in the project, required connections, recommended key components, grounding and power notes, and an ASCII wiring diagram focused on connectors and signal pins. Use this to wire up a prototype or create a PCB/handwired harness.

IMPORTANT SAFETY NOTE
- This controller drives heaters via SSRs which switch mains voltage. Mains wiring, SSR selection, and enclosure safety are the user's responsibility. If you are not experienced with mains wiring, consult a qualified electrician or engineer.
- Add fuses, earth grounding, proper creepage/clearance, and an emergency stop as required by your local regulations.

Summary of devices and roles
- ESP32 Dev Module (platform: esp32dev)
- TFT display (ILI9341 via TFT_eSPI)
- Micro SD card (SPI)
- Two MAX31865 RTD-to-digital converters (one per PT100 sensor)
- HX711 load cell amplifier (torque sensor)
- Two SSR outputs (heater control)
- Motor driver inputs (two GPIOs for up/down direction control)
- Mechanical limit switches (DOWN and HOME)
- Push buttons (UP, DOWN, LEFT, RIGHT, OK)
- DS3231 RTC module (I2C)
- Optional buzzer/LED (uses DEBUG_TP_GPIO by default)

Power rails
- 3.3V (ESP32 logic, MAX31865, HX711, TFT logic) — ensure stable 3.3V regulator capable of display current (TFT can draw notable current for backlight)
- 5V or 12V may be required for the TFT backlight depending on module; check your TFT module and power appropriately
- Mains (for heaters) connected to SSR load side — SSR input driven from ESP32 GPIO (3.3V)

Pin mapping (from include/config.h)
- VSPI (shared SPI lines)
  - SCK  : GPIO18 (PIN_VSPI_SCK)
  - MISO : GPIO19 (PIN_VSPI_MISO)
  - MOSI : GPIO23 (PIN_VSPI_MOSI)

- TFT (SPI, using separate CS)
  - TFT_CS  : GPIO5
  - TFT_DC  : GPIO2
  - TFT_RST : GPIO4
  - (TFT MOSI/MISO/SCLK use VSPI pins 23/19/18)
  - TFT backlight: follow your module wiring (may be 3.3V or higher)

- SD Card (SPI)
  - SD_CS : GPIO13 (PIN_SD_CS)
  - SD uses shared VSPI lines (18/19/23) — ensure separate CS and that card detect wiring if used

- MAX31865 (PT100 RTD interface)
  - MAX1 CS : GPIO14 (PIN_MAX31865_CS1)
  - MAX2 CS : GPIO15 (PIN_MAX31865_CS2)
  - MAX31865 SCLK/MOSI/MISO use VSPI lines
  - PT100 Sensor wiring to MAX31865: connect RTD leads as per Adafruit MAX31865 breakout docs (2/3/4-wire; code uses 3-wire)

- HX711 (torque sensor via load cell)
  - DOUT : GPIO36 (PIN_HX711_DOUT)  — note GPIO36 is input-only (ADC1_CH0)
  - SCK  : GPIO12 (PIN_HX711_SCK)
  - Connect HX711 VCC to 3.3V, GND to system GND
  - HX711 RATE and GAIN either use default wiring or set pins accordingly on module

- Limit switches
  - DOWN_LIMIT : GPIO34 (PIN_DOWN_LIMIT) — input-only; external pull-up required
  - HOME_LIMIT : GPIO35 (PIN_HOME_LIMIT) — input-only; external pull-up required
  - Use mechanical switches between GPIO and GND with external 10k pull-ups to 3.3V (or configure as required)

- Push buttons (configured input_pullup in firmware)
  - BTN_UP    : GPIO32
  - BTN_DOWN  : GPIO33
  - BTN_RIGHT : GPIO25
  - BTN_OK    : GPIO26
  - BTN_LEFT  : GPIO27
  - Buttons wired between pin and GND. The firmware uses internal INPUT_PULLUP for these, but PIN_BTN_RIGHT/OK collision with testpoints was guarded earlier — confirm wiring.

- SSR & Motor outputs
  - SSR_1 : GPIO16 (PIN_SSR_1) — heater 1 control (time-proportioning)
  - SSR_2 : GPIO17 (PIN_SSR_2) — heater 2 control
  - MOTOR_DOWN : GPIO21 (PIN_MOTOR_DOWN) — motor control signal (use with driver)
  - MOTOR_UP   : GPIO22 (PIN_MOTOR_UP)
  - NOTE: These are logic-level outputs. For motor control, use an appropriate motor driver (H-bridge, relay driver, or motor controller). The firmware assumes motor direction is controlled by asserting one of the direction pins HIGH (simple driver). Ensure the motor driver has proper enable/disable and current protection.

- RTC (I2C)
  - SDA/SCL: Wire to ESP32 I2C pins (Wire.begin() default pins for ESP32 are SDA=21 / SCL=22 on some variants; in this code we call Wire.begin() without params — use default board pins or set explicit pins if your wiring differs)
  - DS3231 VCC -> 3.3V (or 5V depending on module; check module requirements), GND -> GND

- Debug test-point / buzzer
  - DEBUG_TP_GPIO : GPIO24 (default) — used as logic test point and also to drive an optional LED or piezo buzzer for RTC feedback
  - If using LED: use series resistor (e.g., 330Ω) between GPIO and LED anode; LED cathode to GND.
  - If using piezo buzzer: use transistor driver (NPN or MOSFET) or small MOSFET to avoid overloading GPIO. Alternatively use passive buzzer via PWM but supervise current.

Suggested wiring notes and components
- Level: All logic at 3.3V. If a peripheral requires 5V (some TFT backlights), feed the backlight with 5V but keep logic at 3.3V (or use level shifters as needed).
- SD Card: use a proper microSD breakout with 3.3V signalling or level shifting. Connect CS to GPIO13.
- MAX31865: supply 3.3V and wire RTD accordingly. Place a small 0.1uF decoupling capacitor near module VCC/GND.
- HX711: sensitive analog input — short wires and good grounding. Place decoupling caps and mount load cell properly.
- SSRs: choose SSRs appropriate for mains load and SSR type (zero-cross for resistive heaters is preferred). SSR input typically expects 3.3V logic; check datasheet for input current needs. SSRs can generate heat and require heatsinking.
- Motor driver: the firmware toggles motor direction pins only. Use a dedicated motor driver and ensure enable/disable and current limiting exists. Add flyback diodes or snubbers as required.
- Limit switches: connect to GND via switch; use external 10k pull-ups to 3.3V on GPIO34/35 because these pins are input-only and firmware assumes external pull-ups.

Recommended connectors
- Use labelled screw-terminal blocks for mains, heater outputs, motor power and motor driver signals.
- Use JST or terminal connectors for sensors, buttons and TFT.

Simplified ASCII wiring diagram (logical connections)

  ESP32
  +----------------------------------------------------------------+
  | VSPI SCK/MOSI/MISO: 18 / 23 / 19  ------------------------------+----> TFT MOSI/SCK/MISO
  |                         |                                           TFT_CS -> GPIO5
  |                         +-- CS:GPIO14 -> MAX31865 #1 (PT100 A)
  |                         +-- CS:GPIO15 -> MAX31865 #2 (PT100 B)
  |                         +-- CS:GPIO13 -> SD card
  |                         +-- CS:GPIO5  -> TFT (CS)
  |
  | HX711: DOUT -> GPIO36, SCK -> GPIO12
  |
  | Buttons: GPIO32/33/25/26/27 -> UP/DOWN/RIGHT/OK/LEFT (to GND when pressed)
  |
  | Limit Switches: GPIO34/35 -> (external pull-ups to 3.3V; switch to GND when active)
  |
  | SSRs: GPIO16/GPIO17 -> SSR IN (heater1/heater2)
  | Motor: GPIO21 (DOWN), GPIO22 (UP) -> Motor driver inputs
  |
  | DEBUG_TP: GPIO24 -> optional LED/buzzer (via resistor/driver)
  |
  | I2C (RTC): Wire.begin() -> DS3231 SDA/SCL (check board pin mapping)
  +----------------------------------------------------------------+

PCB / wiring considerations
- Keep analog HX711 wiring short and twisted to reduce noise.
- Keep RTD leads shielded and follow MAX31865 guidelines for 3-wire RTD wiring (and jumper resistor selection on breakout if required).
- Physically separate mains wiring (heaters) from low-voltage logic and sensor wiring.

Example connection snippets
- Button wiring (one pushbutton):
  - One side to the GPIO pin, the other side to GND. Firmware uses internal pull-up, but we recommend external 10k for jitter-free behavior.

- MAX31865 (PT100 3-wire) basic wiring on Adafruit breakout:
  - VIN -> 3.3V, GND -> GND
  - SCK -> 18, SDI -> 23, SDO -> 19, CS -> 14
  - RTD pins: connect PT100 leads per breakout instructions

- SD card wiring (using microSD adapter supporting 3.3V):
  - CS -> 13, MOSI -> 23, MISO -> 19, SCK -> 18, VCC -> 3.3V, GND -> GND

Verification checklist once wired
- Power on only with no mains SSR load connected initially for safety.
- Verify Serial debug prints boot diagnostics and SD and RTC presence.
- Verify MAX31865 temperature readings are plausible (short-circuit or open-circuit will show faults).
- Verify HX711 produces reasonable torque readings (tare first).
- Verify TFT displays home screen and buttons navigate screens.
- Test SSR outputs using a low-voltage test load before connecting mains heater.
- Test motor jog using the motor driver with low-voltage test conditions if possible.

If you want
- I can generate a KiCad netlist text (CSV style) or a simple Fritzing-style parts list and wiring harness list for hand assembly.
- If you provide a preferred schematic format (KiCad, Eagle, or Fritzing), I can create a starter schematic file (.sch or .fz) as text guidance.

