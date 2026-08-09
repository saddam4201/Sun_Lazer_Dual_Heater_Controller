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
- SystemStatus_t: runtime state, actual temps, current and max torque, remaining time, limit switch flags, active_program_idx, alarm_msg.

Inter-task communication and synchronization
- xSemaphoreSPI: mutex protecting SPI transactions (MAX31865 reads) and TFT drawing.
- xLogQueue: queue used by Safety task to enqueue CSV lines and consumed by Task_Logger.
  - Item size is 256 bytes; safety task composes CSV string locally and sends it to queue (copy).

Storage & RTC
- NVS (Preferences): recipes stored as bytes. loadRecipesFromNVS() detects an old struct size and migrates values into ProgramRecipe_t (preserving legacy data) and fills default PID values.
- SD card: initStorageModules() ensures SD is mounted on PIN_SD_CS and creates /process_history.csv with header if file empty.
- RTC: DS3231 is handled by src/rtc.cpp (initRTC, getTimestampForLog, setRTCTime, getRTCTimeComponents) and addRTCWebHandlers registers web endpoints.

Detailed sequence: typical run
1. Boot
   - setup(): Serial + DEBUG_TP, pins, SPI, TFT/Web, initialize MAX31865, HX711, initStorageModules(), loadRecipesFromNVS(), initRTC() -> prints boot diagnostics to Serial.
   - Tasks created: SafetyTask, PIDTask, UITask, LogTask.

2. Program selection & start
   - User navigates UI to select a recipe and presses OK -> transitionToState(STATE_SAFETY_CHECK).
   - Safety task verifies sensors; resets max torque; transitions to MOVE_DOWN.

3. Move down and heating
   - Safety task runs motor down until down limit -> transitions to HEAT_TO_SETPOINT.
   - PID Task reads temps, computes PID, applies SSR time-proportioning to heaters.
   - When both temps within tolerance, Safety task transitions to TEMPERATURE_READY -> process timer begins.

4. Process timer
   - PIDTask decrements remaining_time_sec while in PROCESS_TIMER.
   - SafetyTask monitors torque; on over-torque triggers safety shutdown.

5. Timer complete and save
   - On completion, SafetyTask moves motor up and eventually to SAVE_RECORD.
   - SafetyTask composes CSV: timestamp (from getTimestampForLog()), program, set/act temps, process_time_sec, max_torque_nm, result, alarm_code and xQueueSend()s it to xLogQueue.

6. Logging
   - Task_Logger receives CSV and writes to SD, and pushes the line into the in-RAM recent log buffer for quick retrieval.

UI and web interactions
- TFT screens are updated every UI tick (50 ms loop) inside xSemaphoreSPI to avoid SPI collisions.
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
- PID control & SSR time-proportion: src/control_tasks.cpp (PID helper include/pid_helper.h)
- UI, TFT, web: src/display_ui.cpp, include/display_ui.h
- RTC module: src/rtc.cpp, include/rtc.h
- Storage & SD: src/storage.cpp, include/storage.h
- Config & pin mappings: include/config.h
- Debug macros & compile-time guards: include/debug_config.h

Notes for developers
- Keep SPI operations wrapped with xSemaphoreSPI to avoid conflicts between MAX31865 and TFT.
- When changing ProgramRecipe_t layout increment RECIPE_VERSION and implement migration logic in loadRecipesFromNVS().
- Use getTimestampForLog() to get RTC-formatted timestamps; this now lives in src/rtc.cpp.

"Quick trace" example (call graph for SAVE_RECORD)
User presses start -> SafetyTask transitions states -> at STATE_HOME_LIMIT -> SafetyTask: compose CSV -> getTimestampForLog(ts) -> xQueueSend(xLogQueue, csv) -> Task_Logger receives -> logToSD(csv) -> SD.open("/process_history.csv", FILE_APPEND) -> write line -> pushRecentLog(csv).

This document is a living artifact; update it when major codeflow changes are made.