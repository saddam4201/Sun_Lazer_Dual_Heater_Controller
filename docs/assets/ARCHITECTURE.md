Sun Lazer Dual Heater Controller — Architecture Diagram & Notes

Purpose
This document provides an architectural overview of the firmware: logical modules, hardware interfaces, data stores, concurrency model, and a visual ASCII component diagram. Use this when onboarding developers or making design choices.

Overview (logical modules)
- Core firmware (src/main.cpp)
  - Initialization, debug prints, peripheral bring-up, task creation.

- Control & Safety (src/control_tasks.cpp)
  - Process state machine
  - Motor control (move down/up), limit-switch handling
  - **Separate limit switch timeout system:**
    - Individual timeouts for down limit (`LIMIT_SWITCH_DOWN_TIMEOUT_SEC`) and home limit (`LIMIT_SWITCH_HOME_TIMEOUT_SEC`)
    - Failure tracking with counts and timestamps for each limit switch
    - TFT warning popups on timeout with red border and failure count display
  - Torque monitoring via HX711 and safety trip
  - CSV record creation and queueing

- Temperature loop (src/control_tasks.cpp)
  - PID controllers per heater (include/pid_helper.h)
  - SSR outputs with time-proportioned control

- UI & Web (src/display_ui.cpp)
  - TFT screens (home, program select/edit, PID tuning, timer editor, RTC set, service)
  - Button handling and navigation
  - WebServer endpoints (/, /rtc, /rtc/set)
  - Visual confirmation popups and debug feedback

- Storage & Logging (src/storage.cpp)
  - SD card (process_history.csv) and CSV header management
  - NVS (Preferences) for recipes; migration from older format supported
  - In-RAM ring buffer for recent logs

- RTC module (src/rtc.cpp)
  - DS3231 integration, validation, read/write APIs, web handlers

- Debug/Tools (include/debug_config.h)
  - Debug test-point macros, compile-time pin collision guards

Hardware interfaces and pins (summary)
- VSPI: SCK=18, MISO=19, MOSI=23
- TFT: CS=5, DC=2, RST=4 (TFT_eSPI)
- SD: CS=13 (PIN_SD_CS)
- MAX31865: CS1=14, CS2=15
- HX711 (torque): DOUT=36, SCK=12
- SSRs: PIN_SSR_1=16, PIN_SSR_2=17
- Motor: PIN_MOTOR_DOWN=21, PIN_MOTOR_UP=22
- Limit switches: PIN_DOWN_LIMIT=34, PIN_HOME_LIMIT=35 (active LOW)
- Buttons: PIN_BTN_UP=32, BTN_DOWN=33, BTN_RIGHT=25, BTN_OK=26, BTN_LEFT=27
- Debug TP: DEBUG_TP_GPIO=24 (default; guarded)

Concurrency model
- Tasks pinned across cores: Safety and PID tasks on Core 1 (real-time); UI and Logger on Core 0 (async).
- SPI access and TFT drawing protected by a single mutex: xSemaphoreSPI.
- Log messages passed via queue xLogQueue (copy of 256-byte buffer).

Storage layout
- NVS: recipes stored with keys rec_0 .. rec_9. ProgramRecipe_t includes magic & version; loadRecipesFromNVS() migrates older-format blobs.
- SD: /process_history.csv stores a header and appended CSV records.
- In-RAM ring buffer recentLogs keeps recent N CSV lines for quick reference (LOG_RECENT_COUNT in include/config.h).

ASCII Architecture Diagram

+-------------------------------------------+
|                ESP32 MCU                  |
|  +-------------------------------------+  |
|  | Tasks / Modules                     |  |
|  |                                     |  |
|  |  +----------------------------+     |  |
|  |  | Task_SafetyAndControl      |     |  |
|  |  | - State machine            |     |  |
|  |  | - Motor control            |     |  |
|  |  | - Torque checks            |     |  |
|  |  +----------------------------+     |  |
|  |                                     |  |
|  |  +----------------------------+     |  |
|  |  | Task_TemperaturePID        |     |  |
|  |  | - MAX31865 reads (SPI)     |     |  |
|  |  | - PID compute & SSR        |     |  |
|  |  +----------------------------+     |  |
|  |                                     |  |
|  |  +----------------------------+     |  |
|  |  | Task_UIAndWeb              |     |  |
|  |  | - TFT screens              |     |  |
|  |  | - WebServer                |     |  |
|  |  +----------------------------+     |  |
|  |                                     |  |
|  |  +----------------------------+     |  |
|  |  | Task_Logger                |     |  |
|  |  | - SD write                 |     |  |
|  |  +----------------------------+     |  |
|  +-------------------------------------+  |
|                                           |
|  +-------------------------------------+  |
|  | Storage & RTC Module                |  |
|  | - Preferences (NVS)                 |  |
|  | - SD (process_history.csv)          |  |
|  | - RTC (DS3231 via RTClib)           |  |
|  +-------------------------------------+  |
|                                           |
|  +-------------------------------------+  |
|  | HW Peripherals                      |  |
|  | - MAX31865 x2 (SPI)                 |  |
|  | - HX711 (torque)                    |  |
|  | - SSRs & motor driver pins          |  |
|  | - TFT & SD (SPI)                    |  |
|  | - Buttons and limit switches        |  |
|  +-------------------------------------+  |
+-------------------------------------------+

Data flows (short)
- Temperature readings -> PID -> SSR -> heaters
- Torque readings -> Safety checks -> possible safety trip -> system alarm
- **Limit switch monitoring:**
  - Limit switch state -> timeout check -> failure counter increment -> TFT warning popup
  - Failure counts -> Service screen display (highlighted when > 0)
  - Reset command -> failure counters cleared
- End-of-run -> SafetyTask enqueues CSV -> Logger writes SD and updates recent-in-RAM
- UI edits (recipe or PID) -> saveRecipeToNVS() -> NVS updated
- RTC set via TFT or Web -> rtc.setRTCTime() -> RTC updated -> timestamp used for logs

Design rationale & tradeoffs
- Time-proportioned SSR control (2000 ms): minimizes SSR switching while providing fine-grain average power control.
- PID bundled locally to avoid external dependency instability and to keep the repo self-contained.
- Per-program PID tunings allow optimized control per recipe without global overrides.
- xSemaphoreSPI centralizes SPI protection at the cost of serializing TFT and sensor SPI access — acceptable given low-frequency sensor readings.

Future architectural recommendations
- Add a small dedicated buzzer GPIO rather than multiplexing DEBUG_TP for audible feedback.
- Add unit-tests or a simulation harness for PID tuning outside hardware.
- Consider moving web handlers to a lightweight REST module for clearer separation.
- Add a simple JSON API for recent logs and program states for remote monitoring and integration.

Files of interest (quick map)
- src/main.cpp
- src/control_tasks.cpp
- src/display_ui.cpp
- src/storage.cpp
- src/rtc.cpp
- include/config.h
- include/debug_config.h
- include/display_ui.h
- include/rtc.h

This diagram and description should be added to project documentation and updated if modules or task partitioning change.