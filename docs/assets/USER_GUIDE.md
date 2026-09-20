Sun Lazer Dual Heater Controller — User Guide

Overview
This guide explains how to safely set up, operate, and maintain the Dual Heater Controller firmware and hardware assembly. It covers power-up checks, the TFT user interface (run screen, PID tuning, timer editor, RTC setting), SD/CSV logging, web endpoints, troubleshooting, and recommended maintenance.

Safety first
- Work with mains-voltage equipment only if you are qualified. Disconnect mains before wiring.
- Use appropriate fuses, earth/grounds, and insulated connectors. SSRs must be heatsinked and rated for your heater current and voltage.
- When testing SSR outputs, use a safe low-voltage dummy load or indicator lamp before switching actual heaters.
- Keep mains wiring separate from low-voltage signal wiring (ESP32, sensors).

Quick start (power-up)
1. Hardware checklist
   - ESP32 dev board mounted and powered (5V input to dev board's regulator or 3.3V where applicable).
   - TFT display connected (SPI) and configured for TFT_eSPI library pins.
   - MAX31865 modules and PT100 sensors connected to the temperature inputs.
   - HX711 connected to torque/load cell and its pins to ESP32.
   - Motor driver or relays wired to MOTOR_UP / MOTOR_DOWN outputs and to proper power source.
   - SSRs mounted on heatsinks and connected to heater mains and ESP32 SSR GPIOs.
   - SD card inserted in SD slot/module.
   - DS3231 RTC module connected to I2C (SDA / SCL) and battery installed if available.
   - Buttons wired: UP, DOWN, LEFT, RIGHT, OK; limit switches wired to HOME_LIMIT / DOWN_LIMIT pins.

2. Power on
   - Apply power. Open serial monitor at 115200 baud to see boot diagnostics. Boot messages include debug test-point pin, SD presence, and RTC presence.
   - Confirm TFT initial screen appears.

TFT UI basics
The TFT UI organizes system functions into menu screens. Use UP/DOWN/LEFT/RIGHT/OK buttons to navigate.

Main / Run Screen
- Shows: active program name, elapsed process time, setpoint and actual temperatures for Heater1 and Heater2, torque/max torque, SD/RTC status, and small status icons.
- OK usually enters main menu or starts/stops the process depending on current state.

Menu navigation
- Use LEFT/RIGHT to move between menu categories, UP/DOWN to change items, OK to select.

Key screens and how to use them

1) Timer Editor (Dedicated HH:MM:SS screen)
- Purpose: set process duration for the selected program.
- Controls: UP/DOWN increments/decrements the currently selected field (hours/minutes/seconds). Default step is 1 second.
- Save: press OK to save and return. Cancel/back via LEFT or another designated button.
- Tips: Use small increments for short processes; long processes by setting hours.

2) PID Tuning (Per-program)
- Purpose: set Kp, Ki, Kd for Heater1 and Heater2 per program.
- Controls: navigate to PID tuning menu. Select parameter (Kp/Ki/Kd for H1/H2). Use UP to increase, DOWN to decrease. Save after editing.
- Values: Kp and Kd default step 0.1, Ki step 0.01 (tuned to sensible defaults). Adjust gradually.
- Tuning advice:
  - Start with Ki = 0, Kd = 0, increase Kp until system reaches setpoint with small overshoot, then add Ki to eliminate steady-state error, finally add small Kd to damp overshoot.
  - Make small changes and observe effect across a whole process run. Log CSV lines to review behavior.

3) RTC Set (Dedicated screen)
- Purpose: set real-time clock when RTC battery/clock is incorrect.
- Controls: Use UP/DOWN to change field values (year/month/day/hour/min/sec). OK to save.
- Validation: firmware validates months/days and leap years. If invalid date/time will be rejected with a confirmation popup.
- Feedback: success/failure indicated by a transient popup and a short beep/LED blink.

4) Service / Tests
- Motor jog: test motor outputs briefly; use only with the mechanism clear.
- Heater SSR test: toggles SSR outputs to confirm wiring (use safe low-voltage dummy load if available).
- Calibration steps: HX711 tare/zero and MAX31865 sensor checks are available from service menu.
- **Limit switch failure monitoring:**
  - Service screen displays failure counts for down and home limit switches
  - Counts highlighted in RED when failures have occurred
  - Use `[DN]+[->]` button combo to reset failure counters
  - Monitor these counts to identify mechanical issues or switch problems

SD card logging and recent logs
- Logs format: CSV file saved to SD: /process_history.csv.
- CSV fields: timestamp, program, h1_set, h1_act, h2_set, h2_act, process_time_sec, max_torque_nm, result, alarm_code
- In-memory recent buffer: the firmware keeps a small ring buffer of recent log records (default 10). This is controlled by LOG_RECENT_COUNT macro in include/config.h.
- To retrieve logs: remove SD card and inspect process_history.csv, or use web endpoint if implemented (ask to enable /recent_logs API).

Web endpoints
- /rtc — returns current RTC time in ISO format (if web server enabled)
- /rtc/set?iso=YYYY-MM-DDTHH:MM:SS — set RTC from web (validated, returns success/fail)
- Additional endpoints depend on compilation options; check the source for enabled routes.

Recipes and NVS (persistent program storage)
- Program recipes are stored in NVS (ESP32 non-volatile storage). Recipes include setpoints, timers, and per-program PID values.
- The firmware will migrate older recipe formats to the new structure on first boot if necessary.
- To save a recipe after editing settings: use the menu command Save / Store recipe. To load: choose Load recipe from the program list.

Running a process
1. **Automatic Pre-Heating on Power-On:** As soon as the system finishes booting and displays the main screen (`STATE_IDLE`), SSRs automatically begin heating to maintain the active program's setpoint. There is no need to press Start to preheat the tools.
2. Select desired program/recipe (P01–P10) and verify setpoints (max 250°C), timer, and PID values.
3. Ensure safety interlocks and pneumatic air pressure are ready.
4. Press Start (OK or on-screen start).
   - The pneumatic cylinder extends down (`PIN_PNEUMATIC`).
   - When the down limit switch is reached: if the heaters are already at setpoint (within tolerance), the process timer starts immediately. If not yet at setpoint, it holds in `STATE_HEAT_TO_SETPOINT` until tolerance is reached (or Force Start is used).
5. During the process: torque motor runs and torque is continuously monitored against the safety limit.
6. On finish: cylinder retracts home, torque motor stops, and results are logged to SD card. **Heaters continue maintaining setpoint** for the next cycle.
7. **Safety Limits & Alarms:** If temperature exceeds 250°C (`MAX_TEMPERATURE_LIMIT_C`), or an Emergency Stop is triggered, or an over-torque occurs, heating and motion are immediately cut off and an alarm is displayed.

Interpreting alarms and logs
- Alarm codes are short numeric values; check README or source code alarm enum to match codes to failures (e.g., OVER_TEMP, TORQUE_LIMIT, RTC_FAIL).
- Use process_history.csv to analyze temperature curves and torque history for tuning and debugging.

Limit switch timeout warnings
- When a limit switch fails to activate within the configured timeout period:
  - A **red-bordered warning popup** appears on the TFT display for 5 seconds
  - The popup shows "WARNING!" header and the failure count
  - Example: "Down limit timeout! Failures: 2"
  - The motor stops automatically to prevent damage
  - The process continues to the next state (non-blocking behavior)
- **Actions to take:**
  - Immediately check the mechanical system and limit switch wiring
  - Inspect the limit switch for physical damage or misalignment
  - Review failure counts on the Service screen
  - Reset failure counters after addressing the issue
- **Configuration:**
  - Timeout values are set in `include/config.h`:
    - `LIMIT_SWITCH_DOWN_TIMEOUT_SEC` (default: 30 seconds)
    - `LIMIT_SWITCH_HOME_TIMEOUT_SEC` (default: 30 seconds)
  - Adjust these values based on your mechanical system's timing requirements

Audio / Visual feedback
- On successful actions (RTC save, recipe save) a small beep or blink occurs using the debug test-point GPIO by default (DEBUG_TP_GPIO, default GPIO24). For production, a dedicated buzzer pin is recommended.

Troubleshooting
- TFT blank or no touch: verify SPI pins and TFT_eSPI config in User_Setup or platform config; check TOUCH_CS definition if using touch.
- SD not detected: check SD CS pin wiring, card seating, and card formatting (FAT32 preferred). Serial boot messages indicate SD status.
- RTC not present or wrong time: check I2C wiring (SDA/SCL), RTC battery, and run RTC Set from TFT or /rtc/set endpoint.
- Temperature readings inconsistent: verify MAX31865 wiring, PT100 wiring (3-wire recommended), and configuration (2/3/4-wire if module supports).
- PID oscillation/overshoot: reduce Kp, increase Kd slightly, or reduce Ki; make small incremental changes and test.
- **Limit switch timeout warnings:**
  - Frequent timeout warnings indicate mechanical issues or switch problems
  - Check limit switch wiring, mechanical alignment, and switch operation
  - Review failure counts on Service screen to identify patterns
  - Adjust timeout values in `include/config.h` if your system requires longer travel times
  - Use `[DN]+[->]` on Service screen to reset counters after fixing the issue

Firmware updates and development
- Firmware built with PlatformIO (Espressif32, Arduino framework). See platformio.ini for dependencies (TFT_eSPI, RTClib, etc.).
- Before updating firmware: back up SD logs if needed and export recipes if you rely on them.
- If you modify pin defines or compile-time options (e.g., DEBUG_TP_GPIO, LOG_RECENT_COUNT), rebuild and flash.

Maintenance and recommended checks
- Weekly: inspect SSR heatsink mounting and ventilation, verify SD card integrity, and check RTC battery.
- After any mains wiring changes: verify fuses and perform a controlled SSR test with safe load.

Appendix
- Default debug and configuration file locations: see include/config.h and include/debug_config.h for pin mappings and macros.
- Recent logs macro: LOG_RECENT_COUNT in include/config.h (change and recompile to adjust retained recent log count).
- PID helper: minimal bundled PID implementation is used; Kp/Ki/Kd units correspond to the temperature control loop used in your recipes.

Support & Next steps
If you want:
- A printable quick-reference sheet for the operator (one-page start/stop/take logs).
- A web API endpoint to download recent logs as JSON (/recent_logs).
- Move beep to dedicated buzzer GPIO and driver circuit.

End of guide

If you want this saved into the repository as USER_GUIDE.md (or a different filename or format) I can create that file now.