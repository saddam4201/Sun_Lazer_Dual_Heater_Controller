#include "control_tasks.h"
#include "storage.h"
#include "display_ui.h"
#include "debug_config.h"
#include "rtc.h"
#include "pid_helper.h"
#include <esp_task_wdt.h>

// PID variables for two heaters
static double h1_input, h1_setpoint, h1_output;
static double h2_input, h2_setpoint, h2_output;
static PID h1_pid(&h1_input, &h1_output, &h1_setpoint, 1.0, 0.0, 0.0, DIRECT);
static PID h2_pid(&h2_input, &h2_output, &h2_setpoint, 1.0, 0.0, 0.0, DIRECT);

void transitionToState(ProcessState_t newState) {
    DEBUG_LOG_STATE_CHANGE(stateNames[sysStatus.currentState], stateNames[newState]);
    sysStatus.currentState = newState;
}

void triggerSafetyShutdown(const char* reason) {
    sysStatus.forceStartActive = false;
    snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "%s", reason);
    DEBUG_LOG_SAFETY_TRIP(reason, 0);
    transitionToState(STATE_ALARM_FAULT);
}

/**
 * @brief Task_SafetyAndControl: FreeRTOS task managing safety and control flow
 * @details This task is responsible for managing the safety and control flow of the
 *          system. It monitors torque sensors, checks for safety trips, and moves the
 *          motor up and down. It also controls the SSR output for the heaters based on
 *          the current state of the system.
 * @param pvParameters not used
 */
void Task_SafetyAndControl(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t movement_timer_ms = 0;
    static bool s_processAbortedByLimitSwitch = false;
    static uint32_t s_process_timer_start_ms = 0;

    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();
        DEBUG_TP_HIGH();

        // Continuous Over-Temperature Safety Trip
        if ((sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT) &&
            (sysStatus.h1_actual_c > 350.0f || sysStatus.h2_actual_c > 350.0f)) {
            char tripMsg[32];
            if (sysStatus.h1_actual_c > 350.0f && sysStatus.h2_actual_c > 350.0f) {
                snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H1:%.0f H2:%.0fC", sysStatus.h1_actual_c, sysStatus.h2_actual_c);
            } else if (sysStatus.h1_actual_c > 350.0f) {
                snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H1: %.1f C", sysStatus.h1_actual_c);
            } else {
                snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H2: %.1f C", sysStatus.h2_actual_c);
            }
            triggerSafetyShutdown(tripMsg);
        }

        // Read physical limit switches with 100ms debounce (Active LOW) + Virtual Simulator override
        static uint8_t down_deb_count = 0;
        static uint8_t home_deb_count = 0;
        const uint8_t deb_thresh = (LIMIT_SWITCH_DEBOUNCE_MS / 10); // 10 samples @ 10ms = 100ms

        int raw_down_pin = safeDigitalRead(PIN_DOWN_LIMIT);
        int raw_home_pin = safeDigitalRead(PIN_HOME_LIMIT);

        bool raw_down_active = (raw_down_pin == (LIMIT_SWITCH_ACTIVE_LOW ? LOW : HIGH)) || g_simDownLimit;
        bool raw_home_active = (raw_home_pin == (LIMIT_SWITCH_ACTIVE_LOW ? LOW : HIGH)) || g_simHomeLimit;

        if (raw_down_active) {
            if (down_deb_count < deb_thresh) down_deb_count++;
        } else {
            if (down_deb_count > 0) down_deb_count--;
        }
        if (down_deb_count >= deb_thresh) {
            sysStatus.down_limit_active = true;
        } else if (down_deb_count == 0) {
            sysStatus.down_limit_active = false;
        }

        if (raw_home_active) {
            if (home_deb_count < deb_thresh) home_deb_count++;
        } else {
            if (home_deb_count > 0) home_deb_count--;
        }
        if (home_deb_count >= deb_thresh) {
            sysStatus.home_limit_active = true;
        } else if (home_deb_count == 0) {
            sysStatus.home_limit_active = false;
        }

        // Dedicated hardware Emergency Stop check (GPIO 35, active LOW)
        int raw_estop_pin = digitalRead(PIN_EMERGENCY_STOP);
        static bool s_last_estop_pin_active = false;
        bool raw_estop_active = false;

#if ENABLE_ESTOP_BENCH_TESTING
        if (!g_simEstopBypass) {
            raw_estop_active = (raw_estop_pin == LOW) || g_simEmergencyStop;
        } else {
            raw_estop_active = g_simEmergencyStop;
            if (!g_simEmergencyStop && sysStatus.emergency_stop_active) {
                // When E-Stop is bypassed, clear the emergency alert immediately
                sysStatus.emergency_stop_active = false;
            }
        }
#else
        // Physical Hardware Operation: strictly read hardware pin (GPIO 35, active LOW)
        raw_estop_active = (raw_estop_pin == LOW);
        if (!raw_estop_active && sysStatus.emergency_stop_active) {
            sysStatus.emergency_stop_active = false;
        }
#endif

        // Edge-triggered hardware press or explicit software activation
        if (raw_estop_active && !s_last_estop_pin_active) {
            sysStatus.emergency_stop_active = true;
            safeDigitalWrite(PIN_SSR_1, LOW);
            safeDigitalWrite(PIN_SSR_2, LOW);
            safeDigitalWrite(PIN_PNEUMATIC, LOW);
            safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
            sysStatus.torque_motor_running = false;
            sysStatus.motor_up_running = false;
            sysStatus.motor_down_running = false;
            sysStatus.remaining_time_sec = 0;
            sysStatus.forceStartActive = false;
            if (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT) {
                snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "EMERGENCY STOP PRESSED");
                transitionToState(STATE_IDLE);
            }
        }
        s_last_estop_pin_active = raw_estop_active;

        // Defensive array bounds clamping
        if (sysStatus.active_program_idx >= 10) sysStatus.active_program_idx = 0;

        // Note: Upper limit switch is replaced by Emergency Stop; pneumatic returns home automatically.

#if ENABLE_HX711
  #if INPUT_SERIAL_SIMULATOR
        // Use simulated torque value set by terminal commands (sysStatus.current_torque_nm)
        float raw_torque = fabsf(sysStatus.current_torque_nm);
        sysStatus.current_torque_nm = raw_torque;
        if (raw_torque > sysStatus.max_torque_nm) sysStatus.max_torque_nm = raw_torque;
        if ((isnan(raw_torque) || raw_torque > MAX_TORQUE_OVERLOAD_NM) &&
            (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT)) {
            triggerSafetyShutdown(isnan(raw_torque) ? "TORQUE SENSOR FAULT (NAN)" : "TORQUE OVERLOAD TRIP");
        }
  #else
        if (torqueScale.is_ready()) {
            float raw_torque = fabsf(torqueScale.get_units(1));
            sysStatus.current_torque_nm = raw_torque;
            // track max torque during a run
            if (raw_torque > sysStatus.max_torque_nm) sysStatus.max_torque_nm = raw_torque;

            if ((isnan(raw_torque) || raw_torque > MAX_TORQUE_OVERLOAD_NM) &&
               (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT)) {
                triggerSafetyShutdown(isnan(raw_torque) ? "TORQUE SENSOR FAULT (NAN)" : "TORQUE OVERLOAD TRIP");
            }
        }
  #endif
#else
        // HX711 disabled: simulate torque sensor with zeros so process can proceed
        sysStatus.current_torque_nm = 0.0f;
#endif

        switch (sysStatus.currentState) {
            case STATE_IDLE:
            case STATE_READY:
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                sysStatus.torque_motor_running = false;
                sysStatus.motor_up_running = false;
                sysStatus.motor_down_running = false;
                break;

            case STATE_SAFETY_CHECK:
                s_processAbortedByLimitSwitch = false;
                if (isnan(sysStatus.h1_actual_c) || sysStatus.h1_actual_c < -45.0f || sysStatus.h1_actual_c > 350.0f ||
                    isnan(sysStatus.h2_actual_c) || sysStatus.h2_actual_c < -45.0f || sysStatus.h2_actual_c > 350.0f) {
                    char tripMsg[32];
                    if (sysStatus.h1_actual_c > 350.0f && sysStatus.h2_actual_c > 350.0f) {
                        snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H1:%.0f H2:%.0fC", sysStatus.h1_actual_c, sysStatus.h2_actual_c);
                    } else if (sysStatus.h1_actual_c > 350.0f) {
                        snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H1: %.1f C", sysStatus.h1_actual_c);
                    } else if (sysStatus.h2_actual_c > 350.0f) {
                        snprintf(tripMsg, sizeof(tripMsg), "OVER-TEMP H2: %.1f C", sysStatus.h2_actual_c);
                    } else {
                        snprintf(tripMsg, sizeof(tripMsg), "SENSOR DISCONNECT FAULT");
                    }
                    triggerSafetyShutdown(tripMsg);
                } else {
                    // reset max torque for new run
                    sysStatus.max_torque_nm = 0.0f;
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_DOWN);
                }
                break;

            case STATE_MOVE_DOWN:
                if (sysStatus.down_limit_active) {
                    // Down limit hit: Keep pneumatic ON continuously
                    safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                    transitionToState(STATE_DOWN_LIMIT);
                } else {
                    safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                    if (millis() - movement_timer_ms > (PNEUMATIC_DOWN_TIMEOUT_SEC * 1000UL)) {
                        // Timeout reached without triggering down limit switch:
                        // Turn off pneumatic so cylinder automatically returns to home.
                        // Stop and move directly to READY state (not locked in fault).
                        safeDigitalWrite(PIN_PNEUMATIC, LOW);
                        sysStatus.down_limit_fail_count++;
                        sysStatus.last_down_limit_fail_ms = millis();
                        
                        showLimitSwitchWarning("Down limit timeout! Retracted.");
                        Serial.printf("[WARNING] Down limit switch not detected within %u s. Pneumatic retracted to home.\n", 
                                      (unsigned)PNEUMATIC_DOWN_TIMEOUT_SEC);
                        transitionToState(STATE_READY);
                    }
                }
                break;

            case STATE_DOWN_LIMIT:
                safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                sysStatus.motor_down_running = true;
                sysStatus.torque_motor_running = false;
                {
                    float tol = fabsf(recipes[sysStatus.active_program_idx].temp_tolerance_c);
                    bool inTol = (fabsf(sysStatus.h1_actual_c - recipes[sysStatus.active_program_idx].h1_setpoint_c) <= tol &&
                                  fabsf(sysStatus.h2_actual_c - recipes[sysStatus.active_program_idx].h2_setpoint_c) <= tol);
                    if (sysStatus.forceStartActive || inTol || !sysStatus.start_mode_auto) {
                        // Force-start, already in tolerance, or Manual mode: proceed directly to timer
                        transitionToState(STATE_TEMPERATURE_READY);
                    } else {
                        transitionToState(STATE_HEAT_TO_SETPOINT);
                    }
                }
                break;

            case STATE_HEAT_TO_SETPOINT: {
                safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                if (!sysStatus.down_limit_active) {
                    safeDigitalWrite(PIN_PNEUMATIC, LOW);
                    safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                    sysStatus.remaining_time_sec = 0;
                    showLimitSwitchWarning("Down limit opened during heating!");
                    snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "DOWN LIMIT SWITCH OPEN");
                    s_processAbortedByLimitSwitch = true;
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_UP);
                    break;
                }
                float tol = fabsf(recipes[sysStatus.active_program_idx].temp_tolerance_c);
                if (fabsf(sysStatus.h1_actual_c - recipes[sysStatus.active_program_idx].h1_setpoint_c) <= tol &&
                    fabsf(sysStatus.h2_actual_c - recipes[sysStatus.active_program_idx].h2_setpoint_c) <= tol) {
                    transitionToState(STATE_TEMPERATURE_READY);
                }
                break;
            }

            case STATE_TEMPERATURE_READY:
                safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                if (!sysStatus.down_limit_active) {
                    safeDigitalWrite(PIN_PNEUMATIC, LOW);
                    safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                    sysStatus.remaining_time_sec = 0;
                    showLimitSwitchWarning("Down limit opened before timer!");
                    snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "DOWN LIMIT SWITCH OPEN");
                    s_processAbortedByLimitSwitch = true;
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_UP);
                    break;
                }
                // Initialize process timer in 100-base units (100 units = 60s, 1 unit = 600ms)
                sysStatus.remaining_time_sec = recipes[sysStatus.active_program_idx].process_time_sec;
                s_process_timer_start_ms = millis();
                transitionToState(STATE_PROCESS_TIMER);
                break;

            case STATE_PROCESS_TIMER:
                // Keep pneumatic ON AND run torque motor for timer duration!
                safeDigitalWrite(PIN_PNEUMATIC, HIGH);
                safeDigitalWrite(PIN_TORQUE_MOTOR, HIGH);
                sysStatus.torque_motor_running = true;
                sysStatus.motor_up_running = true;
                sysStatus.motor_down_running = true;
                if (!sysStatus.down_limit_active) {
                    safeDigitalWrite(PIN_PNEUMATIC, LOW);
                    safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                    sysStatus.torque_motor_running = false;
                    sysStatus.motor_up_running = false;
                    sysStatus.remaining_time_sec = 0;
                    showLimitSwitchWarning("Down limit switch opened!");
                    snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "DOWN LIMIT SWITCH OPEN");
                    s_processAbortedByLimitSwitch = true;
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_UP);
                    break;
                }
                {
                    uint32_t totalUnits = recipes[sysStatus.active_program_idx].process_time_sec;
                    uint32_t totalDurationMs = totalUnits * 600UL; // 1 unit = 600ms fast countdown (100 units = 60.0s)
                    uint32_t elapsedMs = millis() - s_process_timer_start_ms;
                    if (elapsedMs >= totalDurationMs) {
                        sysStatus.remaining_time_sec = 0;
                        transitionToState(STATE_TIMER_COMPLETE);
                    } else {
                        sysStatus.remaining_time_sec = totalUnits - (elapsedMs / 600UL);
                    }
                }
                break;

            case STATE_TIMER_COMPLETE:
                // Timer expired: turn OFF SSRs, turn OFF torque motor, and turn OFF pneumatic (cylinder returns home)
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                sysStatus.torque_motor_running = false;
                sysStatus.motor_up_running = false;
                sysStatus.motor_down_running = false;
                movement_timer_ms = millis();
                transitionToState(STATE_MOVE_UP);
                break;

            case STATE_MOVE_UP:
                // Pneumatic is OFF (LOW); cylinder automatically retracts to home via spring/exhaust.
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                sysStatus.torque_motor_running = false;
                sysStatus.motor_up_running = false;
                sysStatus.motor_down_running = false;
                if (millis() - movement_timer_ms >= PNEUMATIC_RETRACT_DELAY_MS) {
                    transitionToState(STATE_SAVE_RECORD);
                }
                break;

            case STATE_HOME_LIMIT:
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                transitionToState(STATE_SAVE_RECORD);
                break;

            case STATE_SAVE_RECORD: {
                // Compose CSV record with timestamp, program, set/actual temps, process time, max torque, result and alarm code
                char csvBuf[256];
                char ts[32];
                getTimestampForLog(ts, sizeof(ts));
                const char *result = (s_processAbortedByLimitSwitch || sysStatus.currentState == STATE_ALARM_FAULT) ? "ALARM" : "OK";
                const char *alarmcode = (s_processAbortedByLimitSwitch || sysStatus.currentState == STATE_ALARM_FAULT) ? sysStatus.alarm_msg : "";
                snprintf(csvBuf, sizeof(csvBuf), "%s,P%02d,%.1f,%.1f,%.1f,%.1f,%u,%.2f,%s,%s",
                         ts,
                         sysStatus.active_program_idx + 1,
                         recipes[sysStatus.active_program_idx].h1_setpoint_c,
                         sysStatus.h1_actual_c,
                         recipes[sysStatus.active_program_idx].h2_setpoint_c,
                         sysStatus.h2_actual_c,
                         recipes[sysStatus.active_program_idx].process_time_sec,
                         sysStatus.max_torque_nm,
                         result,
                         alarmcode);
                xQueueSend(xLogQueue, csvBuf, 0);
                if (s_processAbortedByLimitSwitch) {
                    s_processAbortedByLimitSwitch = false;
                    sysStatus.forceStartActive = false;
                    transitionToState(STATE_ALARM_FAULT);
                } else {
                    transitionToState(STATE_PROCESS_COMPLETE);
                }
                break;
            }

            case STATE_PROCESS_COMPLETE:
                sysStatus.forceStartActive = false;
                transitionToState(STATE_READY);
                break;

            case STATE_ALARM_FAULT:
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                sysStatus.torque_motor_running = false;
                sysStatus.motor_up_running = false;
                sysStatus.motor_down_running = false;
                break;
        }

        DEBUG_TP_LOW();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

float readPT100Temperature(Adafruit_MAX31865 &maxSensor, uint8_t &faultCode) {
    faultCode = maxSensor.readFault(MAX31865_FAULT_NONE);
    if (faultCode != 0) {
        maxSensor.clearFault();
        return NAN;
    }

    uint16_t rtd = maxSensor.readRTD();

    // Check fault after conversion
    faultCode = maxSensor.readFault(MAX31865_FAULT_NONE);
    if (faultCode != 0) {
        maxSensor.clearFault();
        return NAN;
    }

    // Sanity check raw code (0x0000 = short, 0x7FFF = open circuit / disconnected)
    if (rtd == 0 || rtd >= 0x7FFF) {
        faultCode = 0xFF;
        return NAN;
    }

    float temp = maxSensor.calculateTemperature(rtd, RNOMINAL, RREF);

    // Sanity check temperature range (-50°C to +550°C for industrial PT100)
    if (isnan(temp) || temp < -50.0f || temp > 550.0f) {
        faultCode = 0xFE;
        return NAN;
    }

    return temp;
}

void Task_TemperaturePID(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    esp_task_wdt_add(NULL);

    h1_pid.SetMode(AUTOMATIC);
    h1_pid.SetOutputLimits(0, 100);
    h2_pid.SetMode(AUTOMATIC);
    h2_pid.SetOutputLimits(0, 100);

    for (;;) {
        esp_task_wdt_reset();
        // Dynamic time window: 1s for SSR (fast PWM/switching), 10s for Normal Mechanical Relay
        const uint32_t cycleWindowMs = (g_relayType == RELAY_TYPE_SSR) ? 1000 : 10000;

#if ENABLE_MAX31865
        if (!g_benchTempSimEnabled) {
            // Real Hardware Mode: actively read MAX31865 sensors (CS1 & CS2)
            if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(250)) == pdTRUE) {
                uint8_t f1 = 0, f2 = 0;
                float raw1 = readPT100Temperature(max1, f1);
                float raw2 = readPT100Temperature(max2, f2);
                xSemaphoreGive(xSemaphoreSPI);

                bool isRunning = (sysStatus.currentState == STATE_HEAT_TO_SETPOINT || sysStatus.currentState == STATE_PROCESS_TIMER);

                // Sensor 1 (Heater 1)
                if (!isnan(raw1)) {
                    static float raw1_smoothed = 25.0f;
                    if (raw1_smoothed < -40.0f || raw1_smoothed == 25.0f) {
                        raw1_smoothed = raw1;
                    } else {
                        raw1_smoothed = (raw1_smoothed * 0.7f) + (raw1 * 0.3f);
                    }
#if ENABLE_TEMP_MANIPULATION
                    float h1_set = recipes[sysStatus.active_program_idx].h1_setpoint_c;
                    float h1_pct = recipes[sysStatus.active_program_idx].h1_temp_offset_pct;
                    sysStatus.h1_actual_c = raw1_smoothed + (h1_pct / 100.0f) * h1_set;
#else
                    sysStatus.h1_actual_c = raw1_smoothed;
#endif
                } else if (isRunning) {
                    triggerSafetyShutdown("H1 PT100 SENSOR FAULT");
                }

                // Sensor 2 (Heater 2): read CS2, or fallback to CS1 if unpopulated
                if (!isnan(raw2)) {
                    static float raw2_smoothed = 25.0f;
                    if (raw2_smoothed < -40.0f || raw2_smoothed == 25.0f) {
                        raw2_smoothed = raw2;
                    } else {
                        raw2_smoothed = (raw2_smoothed * 0.7f) + (raw2 * 0.3f);
                    }
#if ENABLE_TEMP_MANIPULATION
                    float h2_set = recipes[sysStatus.active_program_idx].h2_setpoint_c;
                    float h2_pct = recipes[sysStatus.active_program_idx].h2_temp_offset_pct;
                    sysStatus.h2_actual_c = raw2_smoothed + (h2_pct / 100.0f) * h2_set;
#else
                    sysStatus.h2_actual_c = raw2_smoothed;
#endif
                } else {
                    // Fallback to Sensor 1 if Sensor 2 is not wired / detected
                    if (!isnan(raw1)) {
#if ENABLE_TEMP_MANIPULATION
                        float h2_set = recipes[sysStatus.active_program_idx].h2_setpoint_c;
                        float h2_pct = recipes[sysStatus.active_program_idx].h2_temp_offset_pct;
                        sysStatus.h2_actual_c = sysStatus.h1_actual_c + (h2_pct / 100.0f) * h2_set;
#else
                        sysStatus.h2_actual_c = sysStatus.h1_actual_c;
#endif
                    } else if (isRunning) {
                        triggerSafetyShutdown("H2 PT100 SENSOR FAULT");
                    }
                }
            }
        } else {
            // Bench Simulator Mode (enabled from Virtual TFT):
            // Simulate thermal rise when SSRs are active, and cooling when inactive
            float h1_tgt = recipes[sysStatus.active_program_idx].h1_setpoint_c;
            float h2_tgt = recipes[sysStatus.active_program_idx].h2_setpoint_c;

            if (digitalRead(PIN_SSR_1) == HIGH || sysStatus.currentState == STATE_HEAT_TO_SETPOINT || sysStatus.currentState == STATE_PROCESS_TIMER) {
                if (sysStatus.h1_actual_c < h1_tgt) {
                    sysStatus.h1_actual_c += (h1_tgt - sysStatus.h1_actual_c) * 0.05f + 0.3f;
                    if (sysStatus.h1_actual_c > h1_tgt) sysStatus.h1_actual_c = h1_tgt;
                }
            } else {
                if (sysStatus.h1_actual_c > 25.0f) {
                    sysStatus.h1_actual_c -= (sysStatus.h1_actual_c - 25.0f) * 0.02f + 0.05f;
                    if (sysStatus.h1_actual_c < 25.0f) sysStatus.h1_actual_c = 25.0f;
                }
            }

            if (digitalRead(PIN_SSR_2) == HIGH || sysStatus.currentState == STATE_HEAT_TO_SETPOINT || sysStatus.currentState == STATE_PROCESS_TIMER) {
                if (sysStatus.h2_actual_c < h2_tgt) {
                    sysStatus.h2_actual_c += (h2_tgt - sysStatus.h2_actual_c) * 0.05f + 0.3f;
                    if (sysStatus.h2_actual_c > h2_tgt) sysStatus.h2_actual_c = h2_tgt;
                }
            } else {
                if (sysStatus.h2_actual_c > 25.0f) {
                    sysStatus.h2_actual_c -= (sysStatus.h2_actual_c - 25.0f) * 0.02f + 0.05f;
                    if (sysStatus.h2_actual_c < 25.0f) sysStatus.h2_actual_c = 25.0f;
                }
            }
        }
#else
        // MAX31865 disabled at compile-time: match setpoints
        sysStatus.h1_actual_c = recipes[sysStatus.active_program_idx].h1_setpoint_c;
        sysStatus.h2_actual_c = recipes[sysStatus.active_program_idx].h2_setpoint_c;
#endif

        // Update PID inputs and setpoints
        h1_input = sysStatus.h1_actual_c;
        h2_input = sysStatus.h2_actual_c;
        h1_setpoint = recipes[sysStatus.active_program_idx].h1_setpoint_c;
        h2_setpoint = recipes[sysStatus.active_program_idx].h2_setpoint_c;

        // Update PID tunings from active program's settings
        h1_pid.SetTunings(recipes[sysStatus.active_program_idx].h1_Kp,
                          recipes[sysStatus.active_program_idx].h1_Ki,
                          recipes[sysStatus.active_program_idx].h1_Kd);
        h2_pid.SetTunings(recipes[sysStatus.active_program_idx].h2_Kp,
                          recipes[sysStatus.active_program_idx].h2_Ki,
                          recipes[sysStatus.active_program_idx].h2_Kd);

        if (sysStatus.currentState == STATE_HEAT_TO_SETPOINT || sysStatus.currentState == STATE_PROCESS_TIMER) {
            // Compute PID conditionally per-heater
#if ENABLE_H1
            h1_pid.Compute();
#else
            h1_output = 0.0;
#endif
#if ENABLE_H2
            h2_pid.Compute();
#else
            h2_output = 0.0;
#endif

            // Time-proportioning control for SSRs/relays (only if heater enabled)
            uint32_t now = millis();
            uint32_t pos = now % cycleWindowMs;
#if ENABLE_H1
            uint32_t onTime1 = (uint32_t)((h1_output / 100.0) * cycleWindowMs);
            // In Normal Relay mode, prevent rapid contact chatter (min 10% on, max 90% off)
            if (g_relayType == RELAY_TYPE_NORMAL) {
                if (onTime1 < 1000) onTime1 = 0;
                else if (onTime1 > 9000) onTime1 = 10000;
            }
#else
            uint32_t onTime1 = 0;
#endif
#if ENABLE_H2
            uint32_t onTime2 = (uint32_t)((h2_output / 100.0) * cycleWindowMs);
            if (g_relayType == RELAY_TYPE_NORMAL) {
                if (onTime2 < 1000) onTime2 = 0;
                else if (onTime2 > 9000) onTime2 = 10000;
            }
#else
            uint32_t onTime2 = 0;
#endif
            safeDigitalWrite(PIN_SSR_1, (pos < onTime1) ? HIGH : LOW);
            safeDigitalWrite(PIN_SSR_2, (pos < onTime2) ? HIGH : LOW);
        } else {
            safeDigitalWrite(PIN_SSR_1, LOW);
            safeDigitalWrite(PIN_SSR_2, LOW);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

void Task_UIAndWeb(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t lastDisplayUpdate = 0;

    for (;;) {
        handleButtonInputs();

#if ENABLE_WIFI_WEBSERVER
        webServer.handleClient();
#endif

        if (millis() - lastDisplayUpdate >= 150) {
            lastDisplayUpdate = millis();
            if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(200)) == pdTRUE) {
                updateTFTDisplay();
                xSemaphoreGive(xSemaphoreSPI);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

void Task_Logger(void *pvParameters) {
    char logBuffer[256];

    for (;;) {
        if (xQueueReceive(xLogQueue, logBuffer, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(100)) == pdTRUE) {
                logToSD(logBuffer);
                xSemaphoreGive(xSemaphoreSPI);
            }
        }
    }
}