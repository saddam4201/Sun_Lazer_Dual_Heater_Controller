#ifndef CONTROL_TASKS_H
#define CONTROL_TASKS_H

#include "config.h"

void transitionToState(ProcessState_t newState);
void triggerSafetyShutdown(const char* reason);
float readPT100Temperature(Adafruit_MAX31865 &maxSensor, uint8_t &faultCode);

// FreeRTOS Task Prototypes
void Task_SafetyAndControl(void *pvParameters); // Core 1 (10ms)
void Task_TemperaturePID(void *pvParameters);  // Core 1 (100ms)
void Task_UIAndWeb(void *pvParameters);        // Core 0 (50ms)
void Task_Logger(void *pvParameters);          // Core 0 (Event Driven)

#endif // CONTROL_TASKS_H