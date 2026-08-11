#include "rtc.h"
#include "debug_config.h"
#include "config.h"

#if ENABLE_RTC
#include <Wire.h>
#include <RTClib.h>
static RTC_DS3231 rtc;

bool initRTC() {
    Wire.begin();
    if (!rtc.begin()) {
        DEBUG_PRINTLN("[RTC] DS3231 not found");
        return false;
    }
    if (rtc.lostPower()) {
        // set to compile time
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        DEBUG_PRINTLN("[RTC] Power lost, set to compile time");
    }
    DEBUG_PRINTLN("[RTC] Initialized");
    return true;
}

const char* getTimestampForLog(char* buf, size_t len) {
    DateTime now = rtc.now();
    snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return buf;
}

static bool isLeapYear(uint16_t y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static uint8_t daysInMonth(uint16_t y, uint8_t m) {
    static const uint8_t mdays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) return mdays[1] + (isLeapYear(y) ? 1 : 0);
    if (m >= 1 && m <= 12) return mdays[m-1];
    return 31;
}

static bool validateDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    if (year < 2000 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    uint8_t dim = daysInMonth(year, month);
    if (day < 1 || day > dim) return false;
    if (hour > 23) return false;
    if (minute > 59) return false;
    if (second > 59) return false;
    return true;
}

bool setRTCTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    if (!validateDateTime(year, month, day, hour, minute, second)) return false;
    if (!rtc.begin()) return false;
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    return true;
}

bool getRTCTimeComponents(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    if (!rtc.begin()) return false;
    DateTime now = rtc.now();
    if (year) *year = now.year();
    if (month) *month = now.month();
    if (day) *day = now.day();
    if (hour) *hour = now.hour();
    if (minute) *minute = now.minute();
    if (second) *second = now.second();
    return true;
}

void addRTCWebHandlers(WebServer &server) {
    // GET /rtc -> returns current RTC time as text/plain (YYYY-MM-DD HH:MM:SS)
    server.on("/rtc", [&server]() {
        char ts[32];
        getTimestampForLog(ts, sizeof(ts));
        server.send(200, "text/plain", String(ts));
    });

    // POST /rtc/set?iso=YYYY-MM-DDTHH:MM:SS or GET /rtc/set?iso=...
    server.on("/rtc/set", [&server]() {
        String iso = server.arg("iso");
        if (iso.length() == 0) {
            server.send(400, "text/plain", "Missing 'iso' parameter. Use format YYYY-MM-DDTHH:MM:SS");
            return;
        }
        // parse minimally
        int y = iso.substring(0,4).toInt();
        int m = iso.substring(5,7).toInt();
        int d = iso.substring(8,10).toInt();
        int hh = iso.substring(11,13).toInt();
        int mm = iso.substring(14,16).toInt();
        int ss = iso.substring(17,19).toInt();
        bool ok = setRTCTime((uint16_t)y, (uint8_t)m, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss);
        if (ok) {
            server.send(200, "text/plain", "RTC set");
        } else {
            server.send(500, "text/plain", "RTC not available");
        }
    });
}

#else // ENABLE_RTC == 0

bool initRTC() {
    DEBUG_PRINTLN("[RTC] Disabled at compile-time (ENABLE_RTC=0)");
    return false;
}

const char* getTimestampForLog(char* buf, size_t len) {
    // Fallback: use compile-time build timestamp
    snprintf(buf, len, "%s %s", __DATE__, __TIME__);
    return buf;
}

bool setRTCTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second;
    DEBUG_PRINTLN("[RTC] setRTCTime called but RTC disabled");
    return false;
}

bool getRTCTimeComponents(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second;
    return false;
}

void addRTCWebHandlers(WebServer &server) {
    server.on("/rtc", [&server]() {
        server.send(404, "text/plain", "RTC disabled");
    });
    server.on("/rtc/set", [&server]() {
        server.send(404, "text/plain", "RTC disabled");
    });
}

#endif // ENABLE_RTC
