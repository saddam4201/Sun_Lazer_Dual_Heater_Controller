#ifndef GPIO_SAFE_H
#define GPIO_SAFE_H

#include <Arduino.h>

static inline bool gpio_is_valid_number(int pin) {
    // Valid GPIOs on ESP32 are 0..39. Exclude 6..11 (flash) as unsafe.
    if (pin < 0 || pin > 39) return false;
    if (pin >= 6 && pin <= 11) return false; // SDIO/flash pins
    return true;
}

static inline void safePinMode(int pin, int mode) {
    if (gpio_is_valid_number(pin)) {
        pinMode(pin, mode);
    } else {
        Serial.printf("[GPIO_SAFE] pinMode skipped: invalid pin %d\n", pin);
    }
}

static inline void safeDigitalWrite(int pin, int val) {
    if (gpio_is_valid_number(pin)) {
        digitalWrite(pin, val);
    } else {
        Serial.printf("[GPIO_SAFE] digitalWrite skipped: invalid pin %d\n", pin);
    }
}

static inline int safeDigitalRead(int pin) {
    if (gpio_is_valid_number(pin)) {
        return digitalRead(pin);
    } else {
        Serial.printf("[GPIO_SAFE] digitalRead skipped: invalid pin %d\n", pin);
        return LOW;
    }
}

#endif // GPIO_SAFE_H