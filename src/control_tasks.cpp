#include "control_tasks.h"
#include "storage.h"
#include "display_ui.h"
#include "debug_config.h"

void transitionToState(ProcessState_t newState) {
    DEBUG_LOG_STATE_CHANGE(stateNames[sysStatus.currentState], stateNames[newState]);
    sysStatus.currentState = newState;
}

void triggerSafetyShutdown(const char* reason) {
    snprintf(sysStatus.alarm_msg, sizeof(sysStatus.alarm_msg), "%s", reason);
    DEBUG_LOG_SAFETY_TRIP(reason, 0);
    transitionToState(STATE_ALARM_FAULT);
}

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

            if (raw_torque > recipes[sysStatus.active_program_idx].torque_limit_nm &&
               (sysStatus.currentState != STATE_IDLE && sysStatus.currentState != STATE_ALARM_FAULT)) {
                triggerSafetyShutdown("TORQUE OVERLOAD TRIP");
            }
        }

        switch (sysStatus.currentState) {
            case STATE_IDLE:
                digitalWrite(PIN_MOTOR_DOWN, LOW);
                digitalWrite(PIN_MOTOR_UP, LOW);
                break;

            case STATE_SAFETY_CHECK:
                if (sysStatus.h1_actual_c < -10.0f || sysStatus.h2_actual_c < -10.0f) {
                    triggerSafetyShutdown("SENSOR DISCONNECT FAULT");
                } else {
                    movement_timer_ms = millis();
                    transitionToState(STATE_MOVE_DOWN);
                }
                break;

            case STATE_MOVE_DOWN:
                if (sysStatus.down_limit_active) {
                    digitalWrite(PIN_MOTOR_DOWN, LOW);
                    transitionToState(STATE_DOWN_LIMIT);
                } else {
                    digitalWrite(PIN_MOTOR_DOWN, HIGH);
                    if (millis() - movement_timer_ms > 30000) {
                        triggerSafetyShutdown("DOWN TRAVEL TIMEOUT");
                    }
                }
                break;

            case STATE_DOWN_LIMIT:
                digitalWrite(PIN_MOTOR_DOWN, LOW);
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
                digitalWrite(PIN_SSR_1, LOW);
                digitalWrite(PIN_SSR_2, LOW);
                movement_timer_ms = millis();
                transitionToState(STATE_MOVE_UP);
                break;

            case STATE_MOVE_UP:
                if (sysStatus.home_limit_active) {
                    digitalWrite(PIN_MOTOR_UP, LOW);
                    transitionToState(STATE_HOME_LIMIT);
                } else {
                    digitalWrite(PIN_MOTOR_UP, HIGH);
                    if (millis() - movement_timer_ms > 30000) {
                        triggerSafetyShutdown("UP TRAVEL TIMEOUT");
                    }
                }
                break;

            case STATE_HOME_LIMIT:
                digitalWrite(PIN_MOTOR_UP, LOW);
                transitionToState(STATE_SAVE_RECORD);
                break;

            case STATE_SAVE_RECORD: {
                char logBuf[128];
                snprintf(logBuf, sizeof(logBuf), "[REC] P%02d | H1:%.1f | H2:%.1f | Torque:%.1fNm | STATUS:OK",
                         sysStatus.active_program_idx + 1, sysStatus.h1_actual_c, sysStatus.h2_actual_c, sysStatus.current_torque_nm);
                xQueueSend(xLogQueue, &logBuf, 0);
                transitionToState(STATE_PROCESS_COMPLETE);
                break;
            }

            case STATE_PROCESS_COMPLETE:
                transitionToState(STATE_READY);
                break;

            case STATE_READY:
                break;

            case STATE_ALARM_FAULT:
                digitalWrite(PIN_MOTOR_DOWN, LOW);
                digitalWrite(PIN_MOTOR_UP, LOW);
                digitalWrite(PIN_SSR_1, LOW);
                digitalWrite(PIN_SSR_2, LOW);
                break;
        }

        DEBUG_TP_LOW();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

void Task_TemperaturePID(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(20)) == pdTRUE) {
            sysStatus.h1_actual_c = max1.temperature(RNOMINAL, RREF);
            sysStatus.h2_actual_c = max2.temperature(RNOMINAL, RREF);
            xSemaphoreGive(xSemaphoreSPI);
        }

        if (sysStatus.currentState == STATE_HEAT_TO_SETPOINT || sysStatus.currentState == STATE_PROCESS_TIMER) {
            float set1 = recipes[sysStatus.active_program_idx].h1_setpoint_c;
            float set2 = recipes[sysStatus.active_program_idx].h2_setpoint_c;
            digitalWrite(PIN_SSR_1, (sysStatus.h1_actual_c < set1) ? HIGH : LOW);
            digitalWrite(PIN_SSR_2, (sysStatus.h2_actual_c < set2) ? HIGH : LOW);
        } else {
            digitalWrite(PIN_SSR_1, LOW);
            digitalWrite(PIN_SSR_2, LOW);
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
            updateTFTDisplay();
            xSemaphoreGive(xSemaphoreSPI);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
    }
}

void Task_Logger(void *pvParameters) {
    char logBuffer[128];

    for (;;) {
        if (xQueueReceive(xLogQueue, &logBuffer, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(100)) == pdTRUE) {
                logToSD(logBuffer);
                xSemaphoreGive(xSemaphoreSPI);
            }
        }
    }
}