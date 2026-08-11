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

        sysStatus.down_limit_active = (digitalRead(PIN_DOWN_LIMIT) == LOW);
        sysStatus.home_limit_active = (digitalRead(PIN_HOME_LIMIT) == LOW);

        if (torqueScale.is_ready()) {
            float raw_torque = fabsf(torqueScale.get_units(1));
            sysStatus.current_torque_nm = raw_torque;
            // track max torque during a run
            if (raw_torque > sysStatus.max_torque_nm) sysStatus.max_torque_nm = raw_torque;

            if (raw_torque > recipes[sysStatus.active_program_idx].torque_limit_nm &&
               (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_ALARM_FAULT)) {
                triggerSafetyShutdown("TORQUE OVERLOAD TRIP");
            }
        }

        switch (sysStatus.currentState) {
            case STATE_IDLE:
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
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
                    safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                    transitionToState(STATE_DOWN_LIMIT);
                } else {
                        safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
                    if (millis() - movement_timer_ms > 30000) {
                        triggerSafetyShutdown("DOWN TRAVEL TIMEOUT");
                    }
                }
                break;

            case STATE_DOWN_LIMIT:
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
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
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                movement_timer_ms = millis();
                transitionToState(STATE_MOVE_UP);
                break;

            case STATE_MOVE_UP:
                if (sysStatus.home_limit_active) {
                    safeDigitalWrite(PIN_MOTOR_UP, LOW);
                    transitionToState(STATE_HOME_LIMIT);
                } else {
                    safeDigitalWrite(PIN_MOTOR_UP, HIGH);
                    if (millis() - movement_timer_ms > 30000) {
                        triggerSafetyShutdown("UP TRAVEL TIMEOUT");
                    }
                }
                break;

            case STATE_HOME_LIMIT:
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
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

            case STATE_READY:
                break;

            case STATE_ALARM_FAULT:
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                break;
        }

        DEBUG_TP_LOW();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

void Task_TemperaturePID(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const uint32_t cycleWindowMs = 2000; // time-proportional SSR cycle window

    // Initialize PID controllers with settings
    h1_pid.SetMode(AUTOMATIC);
    h1_pid.SetOutputLimits(0, 100);
    h2_pid.SetMode(AUTOMATIC);
    h2_pid.SetOutputLimits(0, 100);

    for (;;) {
        if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(20)) == pdTRUE) {
            sysStatus.h1_actual_c = max1.temperature(RNOMINAL, RREF);
            sysStatus.h2_actual_c = max2.temperature(RNOMINAL, RREF);
            xSemaphoreGive(xSemaphoreSPI);
        }

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
            // Compute PID
            h1_pid.Compute();
            h2_pid.Compute();

            // Time-proportioning control for SSRs
            uint32_t now = millis();
            uint32_t pos = now % cycleWindowMs;
            uint32_t onTime1 = (uint32_t)((h1_output / 100.0) * cycleWindowMs);
            uint32_t onTime2 = (uint32_t)((h2_output / 100.0) * cycleWindowMs);
            safeDigitalWrite(PIN_SSR_1, (pos < onTime1) ? HIGH : LOW);
            safeDigitalWrite(PIN_SSR_2, (pos < onTime2) ? HIGH : LOW);
        } else {
            safeDigitalWrite(PIN_SSR_1, LOW);
            safeDigitalWrite(PIN_SSR_2, LOW);
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

    for (;;) {
        handleButtonInputs();

        webServer.handleClient();

        if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(20)) == pdTRUE) {
            Serial.println("[TFT] Updating display...");
            updateTFTDisplay();
            xSemaphoreGive(xSemaphoreSPI);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
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