#include "control_tasks.h"
#include "storage.h"
#include "display_ui.h"
#include "debug_config.h"
#include "rtc.h"
#include "pid_helper.h"

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

    for (;;) {
        DEBUG_TP_HIGH();

#if INPUT_SERIAL_SIMULATOR
        // In serial simulator mode, limit switch states are supplied via terminal commands
        // and should not be read from hardware pins.
        // sysStatus.down_limit_active and sysStatus.home_limit_active are controlled by simulator commands.
        (void)PIN_DOWN_LIMIT; (void)PIN_HOME_LIMIT;
#else
        sysStatus.down_limit_active = (digitalRead(PIN_DOWN_LIMIT) == LOW);
        sysStatus.home_limit_active = (digitalRead(PIN_HOME_LIMIT) == LOW);
#endif

#if ENABLE_HX711
  #if INPUT_SERIAL_SIMULATOR
        // Use simulated torque value set by terminal commands (sysStatus.current_torque_nm)
        float raw_torque = fabsf(sysStatus.current_torque_nm);
        sysStatus.current_torque_nm = raw_torque;
        if (raw_torque > sysStatus.max_torque_nm) sysStatus.max_torque_nm = raw_torque;
        if (raw_torque > recipes[sysStatus.active_program_idx].torque_limit_nm &&
            (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT)) {
            triggerSafetyShutdown("TORQUE OVERLOAD TRIP");
        }
  #else
        if (torqueScale.is_ready()) {
            float raw_torque = fabsf(torqueScale.get_units(1));
            sysStatus.current_torque_nm = raw_torque;
            // track max torque during a run
            if (raw_torque > sysStatus.max_torque_nm) sysStatus.max_torque_nm = raw_torque;

            if (raw_torque > recipes[sysStatus.active_program_idx].torque_limit_nm &&
               (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_READY && sysStatus.currentState != STATE_ALARM_FAULT)) {
                triggerSafetyShutdown("TORQUE OVERLOAD TRIP");
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
#if INPUT_SERIAL_SIMULATOR
                // In simulator mode, motor outputs are controlled by terminal commands only
                (void)PIN_MOTOR_DOWN; (void)PIN_MOTOR_UP;
#else
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
#endif
                break;

            case STATE_SAFETY_CHECK:
                if (sysStatus.h1_actual_c < -10.0f || sysStatus.h2_actual_c < -10.0f) {
                    triggerSafetyShutdown("SENSOR DISCONNECT FAULT");
                } else {
                    // reset max torque for new run
                    sysStatus.max_torque_nm = 0.0f;
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_DOWN);
                }
                break;

            case STATE_MOVE_DOWN:
                if (sysStatus.down_limit_active) {
#if INPUT_SERIAL_SIMULATOR
                // Do not control motor pin in simulator mode
                (void)PIN_MOTOR_DOWN;
#else
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
#endif
                transitionToState(STATE_DOWN_LIMIT);
            } else {
#if INPUT_SERIAL_SIMULATOR
                // In simulator mode, terminal should drive motor state; just check timeout
                (void)PIN_MOTOR_DOWN;
#else
                safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
#endif
                if (millis() - movement_timer_ms > (LIMIT_SWITCH_DOWN_TIMEOUT_SEC * 1000UL)) {
                    // Timeout reached: stop motor and proceed to next stage to avoid blocking the process.
                    // Track failure and provide warning
                    sysStatus.down_limit_fail_count++;
                    sysStatus.last_down_limit_fail_ms = millis();
                    
                    char warningMsg[64];
                    snprintf(warningMsg, sizeof(warningMsg), "Down limit timeout! Failures: %u", (unsigned)sysStatus.down_limit_fail_count);
                    showLimitSwitchWarning(warningMsg);
                    
                    Serial.printf("[WARNING] Down limit switch not detected within %u s. Failure count: %u\n", 
                                 (unsigned)LIMIT_SWITCH_DOWN_TIMEOUT_SEC, (unsigned)sysStatus.down_limit_fail_count);
#if !INPUT_SERIAL_SIMULATOR
                    safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
#endif
                    transitionToState(STATE_DOWN_LIMIT);
                }
            }
            break;

            case STATE_DOWN_LIMIT:
#if INPUT_SERIAL_SIMULATOR
                (void)PIN_MOTOR_DOWN;
#else
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
#endif
                transitionToState(STATE_HEAT_TO_SETPOINT);
                break;

            case STATE_HEAT_TO_SETPOINT:
                if (fabs(sysStatus.h1_actual_c - recipes[sysStatus.active_program_idx].h1_setpoint_c) <= recipes[sysStatus.active_program_idx].temp_tolerance_c &&
                    fabs(sysStatus.h2_actual_c - recipes[sysStatus.active_program_idx].h2_setpoint_c) <= recipes[sysStatus.active_program_idx].temp_tolerance_c) {
                    transitionToState(STATE_TEMPERATURE_READY);
                }
                break;

            case STATE_TEMPERATURE_READY:
                sysStatus.remaining_time_sec = recipes[sysStatus.active_program_idx].process_time_sec;
                transitionToState(STATE_PROCESS_TIMER);
                break;

            case STATE_PROCESS_TIMER:
                if (sysStatus.remaining_time_sec == 0) {
                    transitionToState(STATE_TIMER_COMPLETE);
                }
                break;

            case STATE_TIMER_COMPLETE:
#if INPUT_SERIAL_SIMULATOR
                // Let terminal control SSRs in simulator mode
                (void)PIN_SSR_1; (void)PIN_SSR_2;
#else
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
#endif
                movement_timer_ms = millis();
                transitionToState(STATE_MOVE_UP);
                break;

            case STATE_MOVE_UP:
                if (sysStatus.home_limit_active) {
#if INPUT_SERIAL_SIMULATOR
                    (void)PIN_MOTOR_UP;
#else
                    safeDigitalWrite(PIN_MOTOR_UP, LOW);
#endif
                    transitionToState(STATE_HOME_LIMIT);
                } else {
#if INPUT_SERIAL_SIMULATOR
                    (void)PIN_MOTOR_UP;
#else
                    safeDigitalWrite(PIN_MOTOR_UP, HIGH);
#endif
                    if (millis() - movement_timer_ms > (LIMIT_SWITCH_HOME_TIMEOUT_SEC * 1000UL)) {
                        // Timeout reached: stop motor and proceed to next stage
                        // Track failure and provide warning
                        sysStatus.home_limit_fail_count++;
                        sysStatus.last_home_limit_fail_ms = millis();
                        
                        char warningMsg[64];
                        snprintf(warningMsg, sizeof(warningMsg), "Home limit timeout! Failures: %u", (unsigned)sysStatus.home_limit_fail_count);
                        showLimitSwitchWarning(warningMsg);
                        
                        Serial.printf("[WARNING] Home limit switch not detected within %u s. Failure count: %u\n", 
                                     (unsigned)LIMIT_SWITCH_HOME_TIMEOUT_SEC, (unsigned)sysStatus.home_limit_fail_count);
#if !INPUT_SERIAL_SIMULATOR
                        safeDigitalWrite(PIN_MOTOR_UP, LOW);
#endif
                        transitionToState(STATE_HOME_LIMIT);
                    }
                }
                break;

            case STATE_HOME_LIMIT:
#if INPUT_SERIAL_SIMULATOR
                (void)PIN_MOTOR_UP;
#else
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
#endif
                transitionToState(STATE_SAVE_RECORD);
                break;

            case STATE_SAVE_RECORD: {
                // Compose CSV record with timestamp, program, set/actual temps, process time, max torque, result and alarm code
                char csvBuf[256];
                char ts[32];
                getTimestampForLog(ts, sizeof(ts));
                const char *result = (sysStatus.currentState == STATE_ALARM_FAULT) ? "ALARM" : "OK";
                const char *alarmcode = (sysStatus.currentState == STATE_ALARM_FAULT) ? sysStatus.alarm_msg : "";
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
                transitionToState(STATE_PROCESS_COMPLETE);
                break;
            }

            case STATE_PROCESS_COMPLETE:
                transitionToState(STATE_READY);
                break;

            case STATE_ALARM_FAULT:
#if INPUT_SERIAL_SIMULATOR
                (void)PIN_MOTOR_DOWN; (void)PIN_MOTOR_UP; (void)PIN_SSR_1; (void)PIN_SSR_2;
#else
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
#endif
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
    const uint32_t cycleWindowMs = 1000; // 1s time proportioning window

    h1_pid.SetMode(AUTOMATIC);
    h1_pid.SetOutputLimits(0, 100);
    h2_pid.SetMode(AUTOMATIC);
    h2_pid.SetOutputLimits(0, 100);

    for (;;) {
#if ENABLE_MAX31865
  #if INPUT_SERIAL_SIMULATOR
        // Do not touch real sensors in simulator mode — use values provided by serial commands
        (void)xSemaphoreSPI;
        // sysStatus.h1_actual_c and h2_actual_c are driven by the simulator commands or defaults
  #else
        if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(250)) == pdTRUE) {
            uint8_t f1 = 0, f2 = 0;
            float raw1 = readPT100Temperature(max1, f1);
            float raw2 = readPT100Temperature(max2, f2);
            xSemaphoreGive(xSemaphoreSPI);

            // Heater 1 validation and noise smoothing
            if (!isnan(raw1)) {
                if (sysStatus.h1_actual_c < -40.0f || sysStatus.h1_actual_c == 0.0f) {
                    sysStatus.h1_actual_c = raw1;
                } else {
                    sysStatus.h1_actual_c = (sysStatus.h1_actual_c * 0.7f) + (raw1 * 0.3f);
                }
            } else if (f1 != 0) {
                DEBUG_PRINTF("[MAX31865] H1 Fault: 0x%02X\n", f1);
            }

            // Heater 2 validation and noise smoothing
            if (!isnan(raw2)) {
                if (sysStatus.h2_actual_c < -40.0f || sysStatus.h2_actual_c == 0.0f) {
                    sysStatus.h2_actual_c = raw2;
                } else {
                    sysStatus.h2_actual_c = (sysStatus.h2_actual_c * 0.7f) + (raw2 * 0.3f);
                }
            } else if (f2 != 0) {
                DEBUG_PRINTF("[MAX31865] H2 Fault: 0x%02X\n", f2);
            }
        }
  #endif
#else
        // MAX31865 disabled: emulate readings as the configured setpoints so process can proceed in test mode
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

            // Time-proportioning control for SSRs (only if heater enabled)
            uint32_t now = millis();
            uint32_t pos = now % cycleWindowMs;
#if ENABLE_H1
            uint32_t onTime1 = (uint32_t)((h1_output / 100.0) * cycleWindowMs);
#else
            uint32_t onTime1 = 0;
#endif
#if ENABLE_H2
            uint32_t onTime2 = (uint32_t)((h2_output / 100.0) * cycleWindowMs);
#else
            uint32_t onTime2 = 0;
#endif
#if INPUT_SERIAL_SIMULATOR
            // In simulator mode, SSR outputs are controlled from terminal commands only
            (void)pos; (void)onTime1; (void)onTime2;
#else
            safeDigitalWrite(PIN_SSR_1, (pos < onTime1) ? HIGH : LOW);
            safeDigitalWrite(PIN_SSR_2, (pos < onTime2) ? HIGH : LOW);
#endif
        } else {
#if INPUT_SERIAL_SIMULATOR
            (void)PIN_SSR_1; (void)PIN_SSR_2;
#else
            safeDigitalWrite(PIN_SSR_1, LOW);
            safeDigitalWrite(PIN_SSR_2, LOW);
#endif
        }

        static uint8_t tick_count = 0;
        if (sysStatus.currentState == STATE_PROCESS_TIMER) {
            if (++tick_count >= 10) {
                tick_count = 0;
                if (sysStatus.remaining_time_sec > 0) sysStatus.remaining_time_sec--;
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

void Task_UIAndWeb(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t lastDisplayUpdate = 0;

    for (;;) {
        handleButtonInputs();

        webServer.handleClient();

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