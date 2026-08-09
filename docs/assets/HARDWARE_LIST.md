Sun Lazer Dual Heater Controller — Hardware Bill of Materials (BOM)

Purpose
This hardware list (BOM) contains recommended parts, quantities, and notes for assembling the Dual Heater Controller described by this firmware. Use it as a starting point for ordering components or building a prototype. Where multiple options exist, alternatives are listed.

Notes
- Quantities: BOM assumes building one controller board/assembly.
- Ratings: For mains/heater items (SSRs, fuses, wiring), choose ratings appropriate for your mains voltage and heater current with margin (at least 25–50%). Consult an electrical engineer when in doubt.
- Safety: Add ground/earth, enclosures, fuses, and E-STOP hardware as required by local regulations.

Core electronics
- ESP32 Dev Module (esp32dev)
  - Qty: 1
  - Notes: ESP32-WROOM 32 (DevKitC or similar). Provides 3.3V logic and sufficient GPIOs and dual-core CPU.

- 3.2" ILI9341 TFT Display (TFT_eSPI supported)
  - Qty: 1
  - Notes: SPI display module with 320x240 resolution (or compatible). Check supply/backlight voltage; many modules require 3.3V logic and 5V backlight.

Sensors & converters
- Adafruit MAX31865 RTD Breakout (or equivalent)
  - Qty: 2 (one per PT100)
  - Notes: Use for PT100 RTD (3-wire configuration used in firmware).

- PT100 RTD sensors
  - Qty: 2
  - Notes: Choose appropriate temperature range for heaters (e.g., 0–300°C). Use 3-wire PT100 and connect per MAX31865 instructions.

- HX711 Load Cell Amplifier (module)
  - Qty: 1
  - Notes: 24-bit ADC for load cells. Use a torque sensor or load cell rated for expected torque, mounted with mechanical adapters. Connect DOUT and SCK to designated pins.

Actuators & power switching
- Solid State Relays (SSRs) for heaters
  - Qty: 2
  - Suggested spec: Zero-cross SSR, input control compatible with 3.3V logic, output rating >= heater voltage/current (e.g., 25A @ 240VAC if heaters draw <25A). Use appropriate heatsinking.
  - Notes: Use SSRs rated for the type of load. For resistive heaters, zero-cross SSRs are common. For more precise control or phase-angle control, select appropriate SSRs (but phase-angle SSRs require different handling).

- Motor driver or relay(s)
  - Qty: 1 motor driver module (depends on motor)
  - Notes: The firmware asserts two GPIOs for motor direction (MOTOR_DOWN, MOTOR_UP). Use a driver that accepts direction inputs, or implement via relays/H-bridge that prevents both outputs from being active simultaneously. Ensure current control and braking if needed.

Switches & buttons
- Mechanical pushbuttons (momentary, tactile or panel mount)
  - Qty: 5 (UP, DOWN, LEFT, RIGHT, OK)
  - Notes: Wired to pins configured as INPUT_PULLUP (buttons to GND).

- Limit switches (mechanical)
  - Qty: 2 (DOWN_LIMIT, HOME_LIMIT)
  - Notes: Active LOW; use external pull-ups for GPIO34/35 or ensure correct wiring.

Real Time Clock
- DS3231 RTC module (I2C)
  - Qty: 1
  - Notes: 3.3V or 5V depending on module. Battery holder for CR2032 recommended for time persistence.

Storage
- MicroSD card module or breakout (3.3V tolerant)
  - Qty: 1
  - Notes: SD card for CSV logging. Ensure module supports 3.3V signalling or use level shifting.
  - MicroSD card (class 10 recommended) — qty: 1

Miscellaneous
- Debug LED / Buzzer
  - Qty: 1
  - Notes: Optional piezo buzzer or LED connected to DEBUG_TP_GPIO (default GPIO24). For buzzers, use a transistor driver if required.

- Wiring & connectors
  - Screw terminal blocks for mains/heater/motor power
  - JST or header connectors for sensors, buttons, and display
  - JST 2/3/4-pin connectors as required by modules
  - Wire: mains-rated wire for heater/mains lines (size per current; e.g., 16–18 AWG or as required). Use 22–26 AWG for signal wiring.

- Passive components & hardware
  - 330Ω resistor for LED series (if using LED)
  - Decoupling capacitors for sensor power rails (0.1uF recommended)
  - Fuses: appropriate rating for heater and motor mains circuits
  - Heatsinks for SSRs
  - Mounting hardware and enclosure

Tools & safety equipment (recommended)
- Multimeter, oscilloscope, logic probe
- Crimping tools and wire strippers
- Soldering station
- Personal protective equipment when working with mains (insulated gloves, goggles)

Optional / recommended alternatives & upgrades
- Dedicated buzzer GPIO and driver circuit — recommended over reusing DEBUG_TP pin for audible feedback in production.
- Use an isolated DC-DC converter for logic and sensor power where isolation is required.
- Add an emergency-stop (E-STOP) switch wired to a high-priority interrupt input and capable of cutting mains or disabling SSRs via external relay.

Mechanical & assembly notes
- Keep mains wiring physically separated from low-voltage logic and sensor wiring to reduce noise and improve safety.
- Securely mount SSRs with appropriate heat sinking; place them on the outside or ventilated area of enclosure.
- Shorten analog HX711 wires and run them away from motor and mains wiring. Ground shielding is recommended.

Ordering / part selection hints
- When ordering MAX31865, HX711, DS3231 modules, prefer reputable vendors or Adafruit/ Sparkfun modules which include documentation.
- For SSRs, use recognized brands and ensure surge and transient specs meet your region's mains conditions.

If you want I can:
- Produce a line-item Excel/CSV with columns: Part, Manufacturer, Supplier SKU, Qty, Unit Price, Notes.
- Produce a KiCad BOM CSV that you can import into PCB/assembly tools.

Which of the above would you like next (CSV BOM, KiCad netlist, or supplier SKUs added)?