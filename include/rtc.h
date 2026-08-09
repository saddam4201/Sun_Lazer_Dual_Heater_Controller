#ifndef RTC_HELPER_H
#define RTC_HELPER_H

#include <Arduino.h>
#include <WebServer.h>

bool initRTC();
const char* getTimestampForLog(char* buf, size_t len);
void addRTCWebHandlers(WebServer &server);

// Convenience setter: sets RTC to provided components (UTC assumed)
bool setRTCTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);

// Read components from RTC
bool getRTCTimeComponents(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second);

#endif // RTC_HELPER_H
