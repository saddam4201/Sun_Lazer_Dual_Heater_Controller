Sun Lazer Dual Heater Controller — Code Flow

Purpose
This document describes the runtime code flow for the firmware: the main tasks, their responsibilities, interactions, and key data flows (queues, semaphores, NVS/SD). Use this to understand execution order and to guide debugging or feature changes.

High-level runtime components
- setup() (src/main.cpp)
  - Initializes Serial, debug TP, pins, peripherals (SPI, MAX31865, HX711), TFT web server, NVS/SD/RTC, and creates FreeRTOS tasks.
  - Creates synchronization primitives: xSemaphoreSPI and xLogQueue.

- FreeRTOS tasks
  - Task_SafetyAndControl (real-time, core 1)
    - Main state machine (ProcessState_t) which drives movement, safety checks, timers, and saves CSV records at run end.
    - Reads torque sensor and enforces torque limits -> calls triggerSafetyShutdown on trip.
    - **Limit switch timeout monitoring:**
      - Separate timeout tracking for down limit (`LIMIT_SWITCH_DOWN_TIMEOUT_SEC`) and home limit (`LIMIT_SWITCH_HOME_TIMEOUT_SEC`)
      - Failure counter increment and timestamp tracking when timeout occurs
      - TFT warning popup trigger via `showLimitSwitchWarning()` function
      - Motor stops on timeout, process continues to next state (non-blocking)
    - Produces CSV record and queues it to xLogQueue when saving records.
  - Task_TemperaturePID (real-time, core 1)
    - Periodically reads MAX31865 thermocouples under xSemaphoreSPI.
    - Runs PID for each heater (header include/pid_helper.h) and maps output to 0..100%.
    - Implements time-proportioned SSR control with a cycle window (default 2000 ms).
    - Decrements remaining process timer when in PROCESS_TIMER.
  - Task_UIAndWeb (async, core 0)
    - Reads button inputs (debounced), updates currentScreen, and handles TFT drawing under xSemaphoreSPI.
    - Hosts WebServer and registers web endpoints (/, /rtc, /rtc/set, …).
    - Manages service tests, PID tuning screen, timer editor, and new RTC editor (SCREEN_RTC_SET).
    - Handles non-blocking blink/beeper patterns for RTC set confirmation using DEBUG_TP pin.
  - Task_Logger (async, core 0)
    - Receives CSV lines from xLogQueue and writes to SD (/process_history.csv).
    - Pushes entries into in-RAM ring buffer (LOG_RECENT_COUNT).

Key data structures (include/config.h)
- ProgramRecipe_t (10 entries): per-program fields include setpoints, process_time_sec, torque_limit_nm, temp_tolerance_c, and per-program PID tunings (h1_Kp, h1_Ki, h1_Kd, h2_*) plus magic/version for NVS migration.
- SystemStatus_t: runtime state, actual temps, current and max torque, remaining time, limit switch flags, active_program_idx, alarm_msg, **limit switch failure tracking** (down_limit_fail_count, home_limit_fail_count, last_down_limit_fail_ms, last_home_limit_fail_ms).

Inter-task communication and synchronization
- xSemaphoreSPI: mutex protecting SPI transactions (MAX31865 reads) and TFT drawing.
- xLogQueue: queue used by Safety task to enqueue CSV lines and consumed by Task_Logger.
  - Item size is 256 bytes; safety task composes CSV string locally and sends it to queue (copy).

Storage & RTC
- NVS (Preferences): recipes stored as bytes. loadRecipesFromNVS() detects an old struct size and migrates values into ProgramRecipe_t (preserving legacy data) and fills default PID values.
- SD card: initStorageModules() ensures SD is mounted on PIN_SD_CS and creates /process_history.csv with header if file empty.
- RTC: DS3231 is handled by src/rtc.cpp (initRTC, getTimestampForLog, setRTCTime, getRTCTimeComponents) and addRTCWebHandlers registers web endpoints.

Detailed sequence: typical run
1. Boot & Pre-Heating
   - setup(): Serial + DEBUG_TP, pins, SPI, TFT/Web, initialize MAX31865, HX711, initStorageModules(), loadRecipesFromNVS(), initRTC() -> prints boot diagnostics to Serial.
   - Initialize limit switch failure counters to zero.
   - Tasks created: SafetyTask, PIDTask, UITask, LogTask.
   - **Continuous Heater Operation:** As soon as boot completes and enters the main screen (`STATE_IDLE` / `STATE_READY`), `Task_TemperaturePID` immediately starts computing PID and driving SSRs to maintain the active recipe's setpoint temperature.
   - **Safety Limits:** Both heaters are continuously monitored against `MAX_TEMPERATURE_LIMIT_C` (250°C). Any reading > 250°C immediately trips safety shutdown across all states.

2. Program selection & start
   - User selects program or changes setpoints from UI.
   - Pressing Start transitions system to `STATE_SAFETY_CHECK`.
   - Safety task verifies sensors (must be between -45°C and 250°C); resets max torque; transitions to `STATE_MOVE_DOWN`.

3. Pneumatic Down Stroke & Temperature Verification
   - Safety task activates pneumatic cylinder (`PIN_PNEUMATIC = HIGH`) to move downward until `PIN_DOWN_LIMIT` activates -> transitions to `STATE_DOWN_LIMIT`.
   - **If down limit timeout occurs (`LIMIT_SWITCH_DOWN_TIMEOUT_SEC`):**
     - Cylinder stays down or stops, failure counter increments, warning popup appears, and process continues.
   - In `STATE_DOWN_LIMIT`:
     - Checks if both temperatures are within tolerance (`inTol`).
     - If already within tolerance (due to boot pre-heating) or Force Start active -> transitions directly to `STATE_TEMPERATURE_READY` -> `STATE_PROCESS_TIMER`.
     - If not yet in tolerance -> transitions to `STATE_HEAT_TO_SETPOINT` until reached.

4. Process timer
   - `PIN_TORQUE_MOTOR` is activated (runs torque motor).
   - `Task_SafetyAndControl` decrements process timer and monitors torque via HX711 (trips if torque exceeds limit).
   - Heaters continue closed-loop PID regulation to setpoint.

5. Timer complete and retraction
   - On timer expiry -> `STATE_TIMER_COMPLETE`:
     - `PIN_TORQUE_MOTOR` is turned OFF.
     - `PIN_PNEUMATIC` is turned OFF (cylinder retracts upward to home).
     - **SSRs remain active** to maintain setpoint temperature for subsequent cycles.
     - Transitions to `STATE_MOVE_UP` -> `STATE_SAVE_RECORD`.
   - SafetyTask composes CSV: timestamp (from getTimestampForLog()), program, set/act temps, process_time_sec, max_torque_nm, result, alarm_code and xQueueSend()s it to xLogQueue.

6. Logging
   - Task_Logger receives CSV and writes to SD, and pushes the line into the in-RAM recent log buffer for quick retrieval.

UI and web interactions
- TFT screens are updated every UI tick (50 ms loop) inside xSemaphoreSPI to avoid SPI collisions.
- **Limit switch warning popups:**
  - Red-bordered popup appears on TFT for 5 seconds when limit switch timeout occurs
  - Displays "WARNING!" header and failure count
  - Managed via `showLimitSwitchWarning()` function and transient popup system
- Service screen displays limit switch failure counts (highlighted in red when > 0)
- Web endpoints:
  - / -> basic status page (H1/H2 temps, torque, state)
  - /rtc -> current RTC time
  - /rtc/set?iso=YYYY-MM-DDTHH:MM:SS -> set RTC
  - Additional quick endpoints can be added (recent logs, program list).

Debug/test-point usage
- Controlled via include/debug_config.h macros (DEBUG_TP_INIT, DEBUG_TP_HIGH, DEBUG_TP_LOW). The pin defaults to GPIO24, and compile-time guards ensure no collision with critical pins.
- Used as a logic probe marker in the safety loop and as the LED/beeper output for RTC set feedback.

Where to look in code
- Boot & task creation: src/main.cpp
- State machine & logging enqueue: src/control_tasks.cpp
- **Limit switch timeout monitoring:** src/control_tasks.cpp (STATE_MOVE_DOWN, STATE_MOVE_UP)
- PID control & SSR time-proportion: src/control_tasks.cpp (PID helper include/pid_helper.h)
- UI, TFT, web: src/display_ui.cpp, include/display_ui.h
- **Limit switch warning popups:** src/display_ui.cpp (showLimitSwitchWarning, transient popup system)
- RTC module: src/rtc.cpp, include/rtc.h
- Storage & SD: src/storage.cpp, include/storage.h
- Config & pin mappings: include/config.h (LIMIT_SWITCH_DOWN_TIMEOUT_SEC, LIMIT_SWITCH_HOME_TIMEOUT_SEC)
- Debug macros & compile-time guards: include/debug_config.h

Notes for developers
- Keep SPI operations wrapped with xSemaphoreSPI to avoid conflicts between MAX31865 and TFT.
- When changing ProgramRecipe_t layout increment RECIPE_VERSION and implement migration logic in loadRecipesFromNVS().
- Use getTimestampForLog() to get RTC-formatted timestamps; this now lives in src/rtc.cpp.
- **Limit switch timeout configuration:**
  - Adjust `LIMIT_SWITCH_DOWN_TIMEOUT_SEC` and `LIMIT_SWITCH_HOME_TIMEOUT_SEC` in include/config.h for different mechanical systems
  - Failure tracking data is stored in SystemStatus_t and initialized in main.cpp setup()
  - Warning popups use the transient popup system in display_ui.cpp with 5-second duration

"Quick trace" example (call graph for SAVE_RECORD)
User presses start -> SafetyTask transitions states -> at STATE_HOME_LIMIT -> SafetyTask: compose CSV -> getTimestampForLog(ts) -> xQueueSend(xLogQueue, csv) -> Task_Logger receives -> logToSD(csv) -> SD.open("/process_history.csv", FILE_APPEND) -> write line -> pushRecentLog(csv).

"Quick trace" example (limit switch timeout)
User presses start -> SafetyTask transitions to STATE_MOVE_DOWN -> down limit not detected within LIMIT_SWITCH_DOWN_TIMEOUT_SEC -> motor stops -> failure counter increments -> showLimitSwitchWarning() called -> TFT popup appears -> process continues to STATE_DOWN_LIMIT.

This document is a living artifact; update it when major codeflow changes are made.