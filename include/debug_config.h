#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <Arduino.h>

// Set to 1 for test development; set to 0 for production release
#define ENABLE_DEBUG_TEST_POINTS 0
#define DEBUG_TP_GPIO 26 // Disabled in this build to avoid GPIO conflicts with buttons


#if ENABLE_DEBUG_TEST_POINTS
  #define DEBUG_INIT(baud)             Serial.begin(baud)
  #define DEBUG_PRINT(x)               Serial.print(x)
  #define DEBUG_PRINTLN(x)             Serial.println(x)
  #define DEBUG_PRINTF(...)            Serial.printf(__VA_ARGS__)
  #define DEBUG_TP_INIT()              pinMode(DEBUG_TP_GPIO, OUTPUT)
  #define DEBUG_TP_HIGH()              digitalWrite(DEBUG_TP_GPIO, HIGH)
  #define DEBUG_TP_LOW()               digitalWrite(DEBUG_TP_GPIO, LOW)
  #define DEBUG_TP_TOGGLE()            digitalWrite(DEBUG_TP_GPIO, !digitalRead(DEBUG_TP_GPIO))
  #define DEBUG_LOG_STATE_CHANGE(o, n) Serial.printf("[TP_STATE] %s -> %s | Time: %lu ms\n", o, n, millis())
  #define DEBUG_LOG_SAFETY_TRIP(r, v)  Serial.printf("[TP_SAFETY_TRIP] Reason: %s | Value: %.2f\n", r, (float)v)
#else
  #define DEBUG_INIT(baud)             ((void)0)
  #define DEBUG_PRINT(x)               ((void)0)
  #define DEBUG_PRINTLN(x)             ((void)0)
  #define DEBUG_PRINTF(...)            ((void)0)
  #define DEBUG_TP_INIT()              ((void)0)
  #define DEBUG_TP_HIGH()              ((void)0)
  #define DEBUG_TP_LOW()               ((void)0)
  #define DEBUG_TP_TOGGLE()            ((void)0)
  #define DEBUG_LOG_STATE_CHANGE(o, n) ((void)0)
  #define DEBUG_LOG_SAFETY_TRIP(r, v)  ((void)0)
#endif

#endif // DEBUG_CONFIG_H