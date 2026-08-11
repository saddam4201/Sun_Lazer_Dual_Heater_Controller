#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <Arduino.h>
#include "config.h"  // bring pin definitions for conflict checks
#include "gpio_safe.h"  // safe GPIO helpers (range-checked)

// Compile-time checks to prevent assigning the debug test-point to pins used by critical hardware


// Set to 1 for test development; set to 0 for production release
#define ENABLE_DEBUG_TEST_POINTS 0 // debug test-point disabled (removed by user request)
#define DEBUG_TP_GPIO 24 // Moved to GPIO24 (user selected) to avoid SD CS and button conflicts

// Prevent accidental pin collisions at compile-time
#if defined(PIN_SD_CS) && (DEBUG_TP_GPIO == PIN_SD_CS)
  #error "DEBUG_TP_GPIO collides with PIN_SD_CS (SD card chip-select). Choose a different debug pin."
#endif

#if defined(PIN_BTN_UP) && (DEBUG_TP_GPIO == PIN_BTN_UP)
  #error "DEBUG_TP_GPIO collides with PIN_BTN_UP. Choose a different debug pin."
#endif
#if defined(PIN_BTN_DOWN) && (DEBUG_TP_GPIO == PIN_BTN_DOWN)
  #error "DEBUG_TP_GPIO collides with PIN_BTN_DOWN. Choose a different debug pin."
#endif
#if defined(PIN_BTN_RIGHT) && (DEBUG_TP_GPIO == PIN_BTN_RIGHT)
  #error "DEBUG_TP_GPIO collides with PIN_BTN_RIGHT. Choose a different debug pin."
#endif
#if defined(PIN_BTN_OK) && (DEBUG_TP_GPIO == PIN_BTN_OK)
  #error "DEBUG_TP_GPIO collides with PIN_BTN_OK. Choose a different debug pin."
#endif
#if defined(PIN_BTN_LEFT) && (DEBUG_TP_GPIO == PIN_BTN_LEFT)
  #error "DEBUG_TP_GPIO collides with PIN_BTN_LEFT. Choose a different debug pin."
#endif

// Additional critical pin collisions to guard against
#if defined(PIN_MAX31865_CS1) && (DEBUG_TP_GPIO == PIN_MAX31865_CS1)
  #error "DEBUG_TP_GPIO collides with PIN_MAX31865_CS1. Choose a different debug pin."
#endif
#if defined(PIN_MAX31865_CS2) && (DEBUG_TP_GPIO == PIN_MAX31865_CS2)
  #error "DEBUG_TP_GPIO collides with PIN_MAX31865_CS2. Choose a different debug pin."
#endif
#if defined(PIN_SSR_1) && (DEBUG_TP_GPIO == PIN_SSR_1)
  #error "DEBUG_TP_GPIO collides with PIN_SSR_1. Choose a different debug pin."
#endif
#if defined(PIN_SSR_2) && (DEBUG_TP_GPIO == PIN_SSR_2)
  #error "DEBUG_TP_GPIO collides with PIN_SSR_2. Choose a different debug pin."
#endif
#if defined(PIN_MOTOR_DOWN) && (DEBUG_TP_GPIO == PIN_MOTOR_DOWN)
  #error "DEBUG_TP_GPIO collides with PIN_MOTOR_DOWN. Choose a different debug pin."
#endif
#if defined(PIN_MOTOR_UP) && (DEBUG_TP_GPIO == PIN_MOTOR_UP)
  #error "DEBUG_TP_GPIO collides with PIN_MOTOR_UP. Choose a different debug pin."
#endif

#if ENABLE_DEBUG_TEST_POINTS
  #define DEBUG_INIT(baud)             Serial.begin(baud)
  #define DEBUG_PRINT(x)               Serial.print(x)
  #define DEBUG_PRINTLN(x)             Serial.println(x)
  #define DEBUG_PRINTF(...)            Serial.printf(__VA_ARGS__)
  #define DEBUG_TP_INIT()              safePinMode(DEBUG_TP_GPIO, OUTPUT)
  #define DEBUG_TP_HIGH()              safeDigitalWrite(DEBUG_TP_GPIO, HIGH)
  #define DEBUG_TP_LOW()               safeDigitalWrite(DEBUG_TP_GPIO, LOW)
  #define DEBUG_TP_TOGGLE()            safeDigitalWrite(DEBUG_TP_GPIO, !safeDigitalRead(DEBUG_TP_GPIO))
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