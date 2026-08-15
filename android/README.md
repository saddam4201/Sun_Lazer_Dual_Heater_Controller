# Sun Lazer Dual Heater Controller — Android Application

Native Android Companion Application for the **Sun Lazer Dual Heater Controller** (ESP32).

---

## 📱 Features

1. **Parallel TFT Display Mirror (Digital Twin):**
   - Renders a live, real-time replica of the physical 2.4" TFT display.
   - Synchronizes across all 7 screens:
     - `SCREEN_HOME`: Status banner, dual temperatures, torque, countdown timer, SSR & limit switch status LEDs.
     - `SCREEN_PROGRAM_SELECT`: Program P01–P10 browser.
     - `SCREEN_PROGRAM_EDIT`: Parameter editor with active field highlight box.
     - `SCREEN_TIMER_EDIT`: HH:MM:SS process timer adjustment.
     - `SCREEN_PID_TUNING`: Kp, Ki, Kd parameters for H1 & H2.
     - `SCREEN_SERVICE`: Actuator test states and limit switch failure tracking.
     - `SCREEN_RTC_SET`: Clock calendar adjustment.

2. **Tactile Virtual D-Pad Controller:**
   - 5 Navigation Buttons: `UP`, `DOWN`, `LEFT`, `RIGHT`, `OK` with haptic feedback.
   - Injects virtual key presses into the ESP32 state machine in real time alongside physical buttons.

3. **Live Process Dashboard:**
   - **Dual Temperature Gauges**: Live H1 & H2 RTD PT100 readings with setpoint markers, tolerance indicator, and heating animation.
   - **Torque Gauge**: Dynamic load dial with peak torque marker and overload trip warning.
   - **13-State Process Stepper**: Real-time progress flowchart through all sequence states (`IDLE` $\rightarrow$ `SAFETY_CHECK` $\rightarrow$ `MOVE_DOWN` $\rightarrow$ `HEAT_TO_SETPOINT` $\rightarrow$ `PROCESS_TIMER` $\rightarrow$ `MOVE_UP` $\rightarrow$ `SAVE_RECORD` $\rightarrow$ `READY`).
   - **Actuator & Safety Hub**: Direct buttons for SSR toggle, Motor Jog Up/Down, and limit switch failure reset.

4. **Program Recipe Manager:**
   - Visual editor for recipes P01 through P10 with 1-tap save to ESP32 NVS flash storage.

5. **Service & Diagnostics:**
   - Start Mode toggle (`AUTO` vs `MANUAL`).
   - 1-tap phone clock sync to DS3231 hardware RTC.
   - Event log history viewer for recent runs.

---

## 🚀 How to Connect

1. **Power on the ESP32 Dual Heater Controller.**
2. Connect your Android phone to the ESP32 Wi-Fi Access Point:
   - **SSID:** `SunLazer_Config`
   - **Password:** `sunlazer123`
3. Open the **Sun Lazer Controller** App.
4. The default IP is set to `192.168.4.1`. Tap **CONNECT**.
5. Once connected, telemetry will stream at 100ms update rates, and all buttons will operate in parallel with the physical TFT display!

---

## 🛠️ Building the Project

- **Prerequisites:** Android Studio Iguana / Jellyfish (or Gradle 8.4+ and JDK 17).
- **Import:** Open the `android/` directory in Android Studio.
- **Build APK:** Run `./gradlew assembleDebug` or build directly from Android Studio.
