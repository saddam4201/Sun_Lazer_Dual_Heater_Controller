Sun Lazer Dual Heater Controller — Release Notes

This README summarizes the recent updates, implemented features, usage notes, and next steps. Treat this as a release-note for the current firmware state.

Version: v3.5
Date: 2026-09-21

Implemented features (summary)
- PID temperature control
  - Minimal header-only PID implementation bundled (include/pid_helper.h) so builds don't depend on external PID packages.
  - Two independent PID controllers (one per heater). PID outputs mapped to 0..100% and applied as time-proportioned SSR drive.
  - Per-program PID tunings stored in the recipe (ProgramRecipe_t) and editable from the TFT UI.
  - **Continuous Heater Regulation on Boot:** SSRs begin regulating and maintaining setpoint temperature immediately upon system boot and entry into the main screen (`STATE_IDLE`, `STATE_READY`). The heater controller operates independently of the cycle start button.
  - **250°C Maximum Temperature Safety Limit:** The absolute safety trip limit is set to 250.0°C (`MAX_TEMPERATURE_LIMIT_C`). Continuous over-temperature monitoring protects against overheating across all operating states, immediately disabling SSRs if exceeded. Setpoints are capped at 250°C.

- TFT UI enhancements
  - Program selection and edit for 10 programs (P01..P10).
  - Dedicated HH:MM:SS process timer editor (SCREEN_TIMER_EDIT) with 1 second increment/decrement steps.
  - PID tuning screen (SCREEN_PID_TUNING) that edits per-program Kp/Ki/Kd.
  - Service screen (SCREEN_SERVICE) with heater test, motor jog, and a dedicated RTC set entry.
  - New RTC Set screen (SCREEN_RTC_SET): set year/month/day/hour/min/sec via TFT.
    - Entry: From SERVICE screen press [DN] + [OK] to open RTC editor.
    - Validates day/month correctness including leap years; invalid dates are rejected.
  - Transient confirmation popup shown after RTC set; also non-blocking LED/beeper feedback (uses DEBUG_TP pin by default).

- RTC and timekeeping
  - Hardware RTC integration via DS3231 (RTClib) moved into a dedicated module:
    - include/rtc.h and src/rtc.cpp (init, read components, set time, validation)
    - initRTC() now called from setup(), and the RTC is responsible for timestamping logs.
  - Web endpoints added:
    - GET /rtc -> returns current RTC time (YYYY-MM-DD HH:MM:SS)
    - GET/POST /rtc/set?iso=YYYY-MM-DDTHH:MM:SS -> sets RTC (returns 200 on success)

- Logging and storage
  - CSV logging to SD: /process_history.csv (header auto-created on first open).
  - CSV format: timestamp,program,h1_set,h1_act,h2_set,h2_act,process_time_sec,max_torque_nm,result,alarm_code
  - In-RAM recent logs ring buffer: LOG_RECENT_COUNT (default 10) entries retained for quick access.
  - NVS recipe migration: old recipe format is detected and migrated to the new ProgramRecipe_t with default PID values assigned.

- Safety, service and hardware
  - Safety trips: torque limit detection, travel timeouts, sensor disconnect checks.
  - **NEW: Separate limit switch timeout handling:**
    - Individual timeout values for down limit (`LIMIT_SWITCH_DOWN_TIMEOUT_SEC`) and home limit (`LIMIT_SWITCH_HOME_TIMEOUT_SEC`) switches.
    - Failure tracking: counts and timestamps for each limit switch failure.
    - **TFT warning popups** with red border and yellow "WARNING!" header when limit switch timeout occurs.
    - Failure counts displayed on Service screen (highlighted in red when > 0).
    - Manual reset capability via `[DN]+[->]` button combo on Service screen or `:reset_fail` serial command.
  - Service tests: heater SSR toggle and motor jog (2 s), displayed on SERVICE screen.
  - Debug test-point:
    - Enabled by default and assigned to GPIO24.
    - include/debug_config.h contains compile-time guards that error if DEBUG_TP_GPIO collides with critical pins (SD CS, buttons, MAX31865 CS, SSRs, motor pins).
    - Used as a pin for the transient RTC success/failure blink/beep feedback (non-blocking patterns).

Build & development notes
- PlatformIO project (platformio.ini) — run locally:
  - platformio run  (build)
  - platformio run --target upload  (flash)
  - platformio device monitor -b 115200  (serial monitor)
- Required libraries (PlatformIO will fetch them via lib_deps):
  - TFT_eSPI, Adafruit MAX31865, HX711, RTClib
  - A local minimal PID helper is included; no PID lib required.

Boot diagnostics
- On startup the firmware prints via Serial (DEBUG):
  - [BOOT] Debug TP GPIO: <n>
  - [BOOT] SD present: YES/NO
  - [BOOT] RTC present: YES/NO
- If DEBUG is disabled (ENABLE_DEBUG_TEST_POINTS = 0), the macros become no-ops and no serial output is made.

How to set RTC (two ways)
- Via TFT:
  - From SERVICE screen press Down + OK to open the RTC editor.
  - Use UP/DOWN to adjust selected field, RIGHT to move to next, OK to save and LEFT to cancel.
  - On save, the UI validates the date/time (month lengths and leap years). A confirmation popup appears and the debug-test pin blinks/beeps on success or failure.
- Via web:
  - GET /rtc  -> returns current time
  - GET /rtc/set?iso=YYYY-MM-DDTHH:MM:SS  -> sets RTC (same validation applies)

How to set RTC (two ways)
- Via TFT:
  - From SERVICE screen press Down + OK to open the RTC editor.
  - Use UP/DOWN to adjust selected field, RIGHT to move to next, OK to save and LEFT to cancel.
  - On save, the UI validates the date/time (month lengths and leap years). A confirmation popup appears and the debug-test pin blinks/beeps on success or failure.
- Via web:
  - GET /rtc  -> returns current time
  - GET /rtc/set?iso=YYYY-MM-DDTHH:MM:SS  -> sets RTC (same validation applies)

Limit switch timeout monitoring
- The system now tracks failures for both down and home limit switches separately.
- When a limit switch fails to activate within its timeout period:
  - Motor stops automatically
  - **TFT warning popup** appears for 5 seconds with failure count
  - Failure counter increments
  - Serial warning message is logged
- View failure counts on Service screen (highlighted in red when > 0)
- Reset failure counters:
  - Via TFT: Press `[DN]+[->]` on Service screen
  - Via serial: Send command `:reset_fail` in simulator mode
- Configure timeouts in `include/config.h`:
  - `LIMIT_SWITCH_DOWN_TIMEOUT_SEC` (default: 30s)
  - `LIMIT_SWITCH_HOME_TIMEOUT_SEC` (default: 30s)

Known issues and constraints
- PlatformIO/Build: this repository assumes PlatformIO for build/flash. Ensure PlatformIO is installed locally — earlier environment lacked it.
- Touch support: TFT_eSPI warns if TOUCH_CS is not defined; touch is optional and not currently used.
- RTC behaviour: on RTC power loss the DS3231 is set to compile-time; set correct time via TFT or web after flashing if necessary.
- Day field editing in the TFT clamps only on save; the UI allows changing day freely but the save will fail for invalid pairs. (Can be tightened to clamp during editing on request.)
- **Limit switch timeout behavior:** When a limit switch fails to activate within the configured timeout, the system stops the motor and continues to the next state rather than triggering a hard safety shutdown. This is intentional to avoid process blocking, but operators should monitor failure counts on the Service screen.

Next steps / Recommendations
- Web endpoints for recent logs (JSON) and program list (optional) — quick to add for remote verification.
- Add runtime assertion to ensure DEBUG_TP_GPIO not accidentally configured as an input at runtime.
- Add a dedicated buzzer pin (if hardware available) for audible feedback rather than reusing DEBUG_TP_GPIO.
- Run hardware tests: verify MAX31865 readings, PID tuning behavior, SD card writes, RTC persistence, and safety trips.

Files of interest
- **NEW: Android Companion Application & Parallel TFT Display Mirroring:**
  - Dedicated Android application created in `android/` with Material Design 3 and Jetpack Compose.
  - Compile-time macro `ENABLE_APP_REMOTE` in `include/config.h` (toggles all REST/JSON endpoints & remote buffers).
  - Parallel display mirroring across all 7 TFT screens (Home, Program Select, Edit, Timer Edit, PID Tuning, Service, RTC Set).
  - Tactile Virtual D-Pad (UP, DOWN, LEFT, RIGHT, OK) with haptic feedback operating in parallel with physical & serial inputs.
  - REST endpoints: `GET /api/status`, `POST /api/button`, `GET/POST /api/recipes`, `POST /api/control`, `GET /api/logs`.

Files of interest
- src/main.cpp — boot sequence, task creation, boot diagnostics
- src/control_tasks.cpp — main state machine, PID compute, SSR time-proportioning, logging enqueue
- src/display_ui.cpp / include/display_ui.h — TFT UI screens, button handling, and App REST API endpoints
- src/rtc.cpp / include/rtc.h — RTC module (init, set, get, web handlers)
- src/storage.cpp / include/storage.h — SD init, CSV logging, NVS recipe migration, recent-in-RAM log buffer
- include/config.h — pin definitions, ProgramRecipe_t struct, ENABLE_APP_REMOTE macro, AppButtonMask_t
- android/ — Standalone Native Android application project

Changelog (high level)
- Added: PID control, per-program PID storage, PID tuning UI
- Added: Dedicated HH:MM:SS timer editor (1s steps)
- Added: DS3231 RTC module with web and TFT set interfaces
- Added: CSV logging to SD and recent-in-RAM log buffer
- Added: NVS recipe migration to preserve older data
- Added: Service tests and max-torque display
- Added: Debug test-point compile-time guards and runtime boot prints
- Added: Separate limit switch timeout system with failure tracking and TFT warning popups
- **Added: Android Companion App & Remote Control API with Parallel TFT Screen Mirroring (`ENABLE_APP_REMOTE`)**
- **Added: Continuous Heater PID Regulation on Boot (`STATE_IDLE` / `STATE_READY`)**
- **Updated: Maximum Temperature Safety Limit lowered to 250°C (`MAX_TEMPERATURE_LIMIT_C`) and setpoint capped at 250°C**
- **Added: Emergency Stop Bench Testing toggle (`ENABLE_ESTOP_BENCH_TESTING`)**
- **Updated: Bumped firmware version to v3.5 and configured upstream GitHub repository**
