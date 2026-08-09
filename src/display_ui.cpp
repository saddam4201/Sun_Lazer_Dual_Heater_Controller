#include "display_ui.h"
#include "control_tasks.h"
#include "storage.h"
#include "debug_config.h"
#include "rtc.h"
#include <WiFi.h>

UIScreen_t currentScreen = SCREEN_HOME;
uint8_t selectedEditField = 0; // 0: H1 Temp, 1: H2 Temp, 2: Process Time, 3: Torque Limit

// Timer edit state
static uint8_t timerEdit_h = 0;
static uint8_t timerEdit_m = 0;
static uint8_t timerEdit_s = 0;
static uint8_t timerEdit_field = 0; // 0=h,1=m,2=s

// PID tuning edit fields displayed under PID tuning screen
static uint8_t pidEdit_field = 0; // 0..5 : h1_Kp,h1_Ki,h1_Kd,h2_Kp,h2_Ki,h2_Kd

// RTC edit state
static uint16_t rtcEdit_year = 2026;
static uint8_t rtcEdit_month = 1;
static uint8_t rtcEdit_day = 1;
static uint8_t rtcEdit_hour = 0;
static uint8_t rtcEdit_minute = 0;
static uint8_t rtcEdit_second = 0;
static uint8_t rtcEdit_field = 0; // 0=year,1=month,2=day,3=hour,4=min,5=sec

// Service test state
static bool service_heater_test_active = false;
static uint32_t service_heater_test_start = 0;
static bool service_motor_test_active = false;
static uint32_t service_motor_test_start = 0;
static bool service_motor_down = false; // direction

// RTC transient confirmation popup
static char rtcConfirmMsg[64] = "";
static uint32_t rtcConfirmUntil = 0; // millis until which to show the popup

// RTC set blink/feedback (uses DEBUG_TP pin as LED/beeper output)
static const uint16_t rtcBlinkSuccessPattern[] = {100, 100, 100, 100}; // HIGH,LOW,HIGH,LOW (ms)
static const uint8_t  rtcBlinkSuccessLen = sizeof(rtcBlinkSuccessPattern)/sizeof(rtcBlinkSuccessPattern[0]);
static const uint16_t rtcBlinkFailPattern[] = {600, 200}; // LONG HIGH, then LOW
static const uint8_t  rtcBlinkFailLen = sizeof(rtcBlinkFailPattern)/sizeof(rtcBlinkFailPattern[0]);

static const uint16_t* rtcBlinkPattern = NULL;
static uint8_t rtcBlinkLen = 0;
static uint8_t rtcBlinkIdx = 0;
static uint32_t rtcBlinkNextToggle = 0;
static bool rtcBlinkActive = false;
static bool rtcBlinkStateHigh = false;

static void startRtcBlink(bool success) {
    rtcBlinkPattern = success ? rtcBlinkSuccessPattern : rtcBlinkFailPattern;
    rtcBlinkLen = success ? rtcBlinkSuccessLen : rtcBlinkFailLen;
    rtcBlinkIdx = 0;
    rtcBlinkActive = true;
    rtcBlinkStateHigh = true; // start with HIGH
    DEBUG_TP_HIGH();
    rtcBlinkNextToggle = millis() + rtcBlinkPattern[0];
}

void initDisplayAndWeb() {
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(TFT_BLACK);
    tft.drawString("SUN LAZER INITIALIZING...", 20, 110, 2);

    WiFi.softAP("SunLazer_Config", "sunlazer123");
    setupWebServer();
}

void handleButtonInputs() {
    static uint32_t lastButtonPress = 0;
    if (millis() - lastButtonPress < 150) return; // Non-blocking debounce

    bool btnUp    = (digitalRead(PIN_BTN_UP) == LOW);
    bool btnDown  = (digitalRead(PIN_BTN_DOWN) == LOW);
    bool btnLeft  = (digitalRead(PIN_BTN_LEFT) == LOW);
    bool btnRight = (digitalRead(PIN_BTN_RIGHT) == LOW);
    bool btnOk    = (digitalRead(PIN_BTN_OK) == LOW);

    if (!btnUp && !btnDown && !btnLeft && !btnRight && !btnOk) return;
    lastButtonPress = millis();

    switch (currentScreen) {
        case SCREEN_HOME:
            if (btnRight) {
                currentScreen = SCREEN_PROGRAM_SELECT;
            } else if (btnLeft) {
                currentScreen = SCREEN_SERVICE;
            } else if (btnOk) {
                if (sysStatus.currentState == STATE_IDLE) {
                    transitionToState(STATE_SAFETY_CHECK);
                } else if (sysStatus.currentState == STATE_ALARM_FAULT) {
                    transitionToState(STATE_IDLE);
                }
            }
            break;

        case SCREEN_PROGRAM_SELECT:
            if (btnUp) {
                if (sysStatus.active_program_idx > 0) sysStatus.active_program_idx--;
            } else if (btnDown) {
                if (sysStatus.active_program_idx < 9) sysStatus.active_program_idx++;
            } else if (btnRight || btnOk) {
                currentScreen = SCREEN_PROGRAM_EDIT;
                selectedEditField = 0;
            } else if (btnLeft) {
                currentScreen = SCREEN_HOME;
            }
            break;

        case SCREEN_PROGRAM_EDIT:
            if (btnUp) {
                if (selectedEditField == 0) recipes[sysStatus.active_program_idx].h1_setpoint_c += 5.0f;
                else if (selectedEditField == 1) recipes[sysStatus.active_program_idx].h2_setpoint_c += 5.0f;
                else if (selectedEditField == 2) recipes[sysStatus.active_program_idx].process_time_sec += 1; // change to 1s increments
                else if (selectedEditField == 3) recipes[sysStatus.active_program_idx].torque_limit_nm += 0.5f;
            } else if (btnDown) {
                if (selectedEditField == 0 && recipes[sysStatus.active_program_idx].h1_setpoint_c >= 5.0f)
                    recipes[sysStatus.active_program_idx].h1_setpoint_c -= 5.0f;
                else if (selectedEditField == 1 && recipes[sysStatus.active_program_idx].h2_setpoint_c >= 5.0f)
                    recipes[sysStatus.active_program_idx].h2_setpoint_c -= 5.0f;
                else if (selectedEditField == 2 && recipes[sysStatus.active_program_idx].process_time_sec >= 1)
                    recipes[sysStatus.active_program_idx].process_time_sec -= 1; // 1s step
                else if (selectedEditField == 3 && recipes[sysStatus.active_program_idx].torque_limit_nm >= 1.0f)
                    recipes[sysStatus.active_program_idx].torque_limit_nm -= 0.5f;
            } else if (btnRight) {
                selectedEditField = (selectedEditField + 1) % 4;
            } else if (btnOk) {
                // if user pressed OK while editing time field, go to dedicated timer editor
                if (selectedEditField == 2) {
                    // initialize timer editor with current value
                    uint32_t t = recipes[sysStatus.active_program_idx].process_time_sec;
                    timerEdit_h = t / 3600;
                    timerEdit_m = (t % 3600) / 60;
                    timerEdit_s = t % 60;
                    timerEdit_field = 2; // start editing seconds
                    currentScreen = SCREEN_TIMER_EDIT;
                } else {
                    saveRecipeToNVS(sysStatus.active_program_idx);
                    currentScreen = SCREEN_PROGRAM_SELECT;
                }
            } else if (btnLeft) {
                currentScreen = SCREEN_PROGRAM_SELECT;
            }
            break;

        case SCREEN_TIMER_EDIT:
            if (btnUp) {
                if (timerEdit_field == 0) timerEdit_h++;
                else if (timerEdit_field == 1) timerEdit_m = (timerEdit_m + 1) % 60;
                else if (timerEdit_field == 2) timerEdit_s = (timerEdit_s + 1) % 60;
            } else if (btnDown) {
                if (timerEdit_field == 0 && timerEdit_h > 0) timerEdit_h--;
                else if (timerEdit_field == 1 && timerEdit_m > 0) timerEdit_m--;
                else if (timerEdit_field == 2 && timerEdit_s > 0) timerEdit_s--;
            } else if (btnRight) {
                timerEdit_field = (timerEdit_field + 1) % 3;
            } else if (btnOk) {
                // save timer back to recipe
                uint32_t total = ((uint32_t)timerEdit_h * 3600) + ((uint32_t)timerEdit_m * 60) + (uint32_t)timerEdit_s;
                recipes[sysStatus.active_program_idx].process_time_sec = total;
                saveRecipeToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_PROGRAM_EDIT;
            } else if (btnLeft) {
                currentScreen = SCREEN_PROGRAM_EDIT;
            }
            break;

        case SCREEN_RTC_SET:
            if (btnUp) {
                switch (rtcEdit_field) {
                    case 0: rtcEdit_year++; break;
                    case 1: rtcEdit_month = (rtcEdit_month % 12) + 1; break;
                    case 2: rtcEdit_day = (rtcEdit_day % 31) + 1; break;
                    case 3: rtcEdit_hour = (rtcEdit_hour + 1) % 24; break;
                    case 4: rtcEdit_minute = (rtcEdit_minute + 1) % 60; break;
                    case 5: rtcEdit_second = (rtcEdit_second + 1) % 60; break;
                }
            } else if (btnDown) {
                switch (rtcEdit_field) {
                    case 0: if (rtcEdit_year > 2000) rtcEdit_year--; break;
                    case 1: rtcEdit_month = (rtcEdit_month == 1) ? 12 : (rtcEdit_month - 1); break;
                    case 2: rtcEdit_day = (rtcEdit_day == 1) ? 31 : (rtcEdit_day - 1); break;
                    case 3: rtcEdit_hour = (rtcEdit_hour == 0) ? 23 : (rtcEdit_hour - 1); break;
                    case 4: rtcEdit_minute = (rtcEdit_minute == 0) ? 59 : (rtcEdit_minute - 1); break;
                    case 5: rtcEdit_second = (rtcEdit_second == 0) ? 59 : (rtcEdit_second - 1); break;
                }
            } else if (btnRight) {
                rtcEdit_field = (rtcEdit_field + 1) % 6;
            } else if (btnOk) {
                // save RTC
                if (setRTCTime(rtcEdit_year, rtcEdit_month, rtcEdit_day, rtcEdit_hour, rtcEdit_minute, rtcEdit_second)) {
                    snprintf(rtcConfirmMsg, sizeof(rtcConfirmMsg), "RTC Set: %04u-%02u-%02u %02u:%02u:%02u", rtcEdit_year, rtcEdit_month, rtcEdit_day, rtcEdit_hour, rtcEdit_minute, rtcEdit_second);
                    startRtcBlink(true);
                } else {
                    snprintf(rtcConfirmMsg, sizeof(rtcConfirmMsg), "RTC Set Failed: Invalid Date/Time");
                    startRtcBlink(false);
                }
                rtcConfirmUntil = millis() + 3000; // show popup for 3s
                currentScreen = SCREEN_SERVICE;
            } else if (btnLeft) {
                currentScreen = SCREEN_SERVICE;
            }
            break;

        case SCREEN_PID_TUNING: {
            // edit PID for active program
            ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];
            if (btnUp) {
                switch (pidEdit_field) {
                    case 0: prec.h1_Kp += 0.1f; break;
                    case 1: prec.h1_Ki += 0.01f; break;
                    case 2: prec.h1_Kd += 0.1f; break;
                    case 3: prec.h2_Kp += 0.1f; break;
                    case 4: prec.h2_Ki += 0.01f; break;
                    case 5: prec.h2_Kd += 0.1f; break;
                }
            } else if (btnDown) {
                switch (pidEdit_field) {
                    case 0: if (prec.h1_Kp >= 0.1f) prec.h1_Kp -= 0.1f; break;
                    case 1: if (prec.h1_Ki >= 0.01f) prec.h1_Ki -= 0.01f; break;
                    case 2: if (prec.h1_Kd >= 0.1f) prec.h1_Kd -= 0.1f; break;
                    case 3: if (prec.h2_Kp >= 0.1f) prec.h2_Kp -= 0.1f; break;
                    case 4: if (prec.h2_Ki >= 0.01f) prec.h2_Ki -= 0.01f; break;
                    case 5: if (prec.h2_Kd >= 0.1f) prec.h2_Kd -= 0.1f; break;
                }
            } else if (btnRight) {
                pidEdit_field = (pidEdit_field + 1) % 6;
            } else if (btnOk) {
                saveRecipeToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_SERVICE;
            } else if (btnLeft) {
                currentScreen = SCREEN_SERVICE;
            }
            break;
        }

        case SCREEN_SERVICE:
            // special combo: Up + OK => enter RTC set screen
            if (btnUp && btnOk) {
                // initialize RTC edit fields from current RTC
                uint16_t y; uint8_t mo, d, hh, mm, ss;
                if (getRTCTimeComponents(&y, &mo, &d, &hh, &mm, &ss)) {
                    rtcEdit_year = y;
                    rtcEdit_month = mo;
                    rtcEdit_day = d;
                    rtcEdit_hour = hh;
                    rtcEdit_minute = mm;
                    rtcEdit_second = ss;
                }
                rtcEdit_field = 0;
                currentScreen = SCREEN_RTC_SET;
            } else if (btnLeft) {
                // cancel any active service tests
                service_heater_test_active = false;
                service_motor_test_active = false;
                digitalWrite(PIN_SSR_1, LOW);
                digitalWrite(PIN_SSR_2, LOW);
                digitalWrite(PIN_MOTOR_DOWN, LOW);
                digitalWrite(PIN_MOTOR_UP, LOW);
                currentScreen = SCREEN_HOME;
            } else if (btnOk) {
                // Heater test: toggle SSRs for 2 seconds
                if (!service_heater_test_active) {
                    service_heater_test_active = true;
                    service_heater_test_start = millis();
                    digitalWrite(PIN_SSR_1, HIGH);
                    digitalWrite(PIN_SSR_2, HIGH);
                }
            } else if (btnRight) {
                // Motor jog down for 2 seconds (if down limit not active)
                if (!service_motor_test_active && !sysStatus.down_limit_active) {
                    service_motor_test_active = true;
                    service_motor_test_start = millis();
                    service_motor_down = true;
                    // put controller into READY so the safety task does not override motor
                    transitionToState(STATE_READY);
                    digitalWrite(PIN_MOTOR_DOWN, HIGH);
                }
            } else if (btnUp) {
                // Motor jog up for 2 seconds (if home limit not active)
                if (!service_motor_test_active && !sysStatus.home_limit_active) {
                    service_motor_test_active = true;
                    service_motor_test_start = millis();
                    service_motor_down = false;
                    transitionToState(STATE_READY);
                    digitalWrite(PIN_MOTOR_UP, HIGH);
                }
            } else if (btnDown && btnOk) {
                // Enter RTC set screen (dedicated menu entry: Down + OK)
                uint16_t y; uint8_t mo, d, hh, mm, ss;
                if (getRTCTimeComponents(&y, &mo, &d, &hh, &mm, &ss)) {
                    rtcEdit_year = y;
                    rtcEdit_month = mo;
                    rtcEdit_day = d;
                    rtcEdit_hour = hh;
                    rtcEdit_minute = mm;
                    rtcEdit_second = ss;
                }
                rtcEdit_field = 0;
                currentScreen = SCREEN_RTC_SET;
            } else if (btnRight && btnOk) {
                // Enter PID tuning screen
                currentScreen = SCREEN_PID_TUNING;
                pidEdit_field = 0;
            }
            break;
    }
}

void drawHomeScreen() {
    tft.fillRect(0, 0, 320, 25, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("SUN LAZER - HOME", 10, 5, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Active PGM:", 10, 35, 2);
    tft.drawString(recipes[sysStatus.active_program_idx].name, 120, 35, 2);

    tft.drawString("H1 Set/Act:", 10, 65, 2);
    tft.drawFloat(recipes[sysStatus.active_program_idx].h1_setpoint_c, 1, 120, 65, 2);
    tft.drawString("/", 175, 65, 2);
    tft.drawFloat(sysStatus.h1_actual_c, 1, 190, 65, 2);

    tft.drawString("H2 Set/Act:", 10, 95, 2);
    tft.drawFloat(recipes[sysStatus.active_program_idx].h2_setpoint_c, 1, 120, 95, 2);
    tft.drawString("/", 175, 95, 2);
    tft.drawFloat(sysStatus.h2_actual_c, 1, 190, 95, 2);

    tft.drawString("Torque (Nm):", 10, 125, 2);
    tft.drawFloat(sysStatus.current_torque_nm, 1, 120, 125, 2);

    tft.drawString("Timer (Sec):", 10, 155, 2);
    tft.drawNumber(sysStatus.remaining_time_sec, 120, 155, 2);

    tft.drawString("[OK]: Start | [->]: Program Menu", 10, 185, 2);

    uint16_t bannerColor = (sysStatus.currentState == STATE_ALARM_FAULT) ? TFT_RED : TFT_DARKGREEN;
    tft.fillRect(0, 210, 320, 30, bannerColor);
    tft.setTextColor(TFT_WHITE, bannerColor);
    if (sysStatus.currentState == STATE_ALARM_FAULT) {
        tft.drawString(sysStatus.alarm_msg, 10, 215, 2);
    } else {
        tft.drawString(stateNames[sysStatus.currentState], 10, 215, 2);
    }
}

void drawProgramSelectScreen() {
    tft.fillRect(0, 0, 320, 25, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("SELECT RECIPE (P01 - P10)", 10, 5, 2);

    const int items = 10;
    const int startX = 20;
    const int tempX = 180;
    const int unitX = 220;
    const int startY = 35;
    const int lineH = 20; // tighter spacing to fit 10 lines

    // clear the list area
    tft.fillRect(0, startY - 5, 320, items * lineH + 10, TFT_BLACK);

    for (int i = 0; i < items; i++) {
        uint8_t idx = i;
        uint16_t color = (sysStatus.active_program_idx == idx) ? TFT_YELLOW : TFT_WHITE;
        tft.setTextColor(color, TFT_BLACK);
        tft.drawString(recipes[idx].name, startX, startY + (i * lineH), 2);
        tft.drawFloat(recipes[idx].h1_setpoint_c, 0, tempX, startY + (i * lineH), 2);
        tft.drawString("C", unitX, startY + (i * lineH), 2);
    }

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[UP/DN]: Navigate | [OK/->]: Edit | [<-]: Back", 10, 215, 2);
}


void drawProgramEditScreen() {
    tft.fillRect(0, 0, 320, 25, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("EDIT RECIPE PARAMETERS", 10, 5, 2);

    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];

    tft.setTextColor(selectedEditField == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.drawString("H1 Target Temp: ", 10, 45, 2);
    tft.drawFloat(rec.h1_setpoint_c, 1, 180, 45, 2);

    tft.setTextColor(selectedEditField == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.drawString("H2 Target Temp: ", 10, 80, 2);
    tft.drawFloat(rec.h2_setpoint_c, 1, 180, 80, 2);

    tft.setTextColor(selectedEditField == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.drawString("Process Time (s): ", 10, 115, 2);
    tft.drawNumber(rec.process_time_sec, 180, 115, 2);

    tft.setTextColor(selectedEditField == 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.drawString("Torque Limit (Nm): ", 10, 150, 2);
    tft.drawFloat(rec.torque_limit_nm, 1, 180, 150, 2);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[UP/DN]: Change | [->]: Next | [OK]: Save", 10, 215, 2);
}

void drawServiceScreen() {
    tft.fillRect(0, 0, 320, 25, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("SERVICE DIAGNOSTICS", 10, 5, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("DN Limit Switch: ", 10, 45, 2);
    tft.drawString(sysStatus.down_limit_active ? "ACTIVE" : "OPEN", 180, 45, 2);

    tft.drawString("HOME Limit Switch: ", 10, 80, 2);
    tft.drawString(sysStatus.home_limit_active ? "ACTIVE" : "OPEN", 180, 80, 2);

    tft.drawString("Raw Torque (Nm): ", 10, 115, 2);
    tft.drawFloat(sysStatus.current_torque_nm, 2, 180, 115, 2);

    tft.drawString("Max Torque (Nm): ", 10, 145, 2);
    tft.drawFloat(sysStatus.max_torque_nm, 2, 180, 145, 2);

    // Show service test statuses
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    if (service_heater_test_active) tft.drawString("Heater Test: RUNNING", 10, 175, 2);
    else tft.drawString("[OK]: Heater Test", 10, 175, 2);

    if (service_motor_test_active) {
        tft.drawString(service_motor_down ? "Motor Jog: DOWN" : "Motor Jog: UP", 180, 175, 2);
    } else {
        tft.drawString("[UP]/[->]: Motor Jog", 180, 175, 2);
    }

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[<-]: Back | [OK]: Heater Test | [->]: Motor Down | [UP]: Motor Up | [->]+[OK]: PID Tune", 10, 200, 2);
    tft.drawString("[DN]+[OK]: Set RTC", 10, 220, 2);
}

void updateTFTDisplay() {
    // handle service test timeouts
    if (service_heater_test_active && (millis() - service_heater_test_start > 2000)) {
        service_heater_test_active = false;
        digitalWrite(PIN_SSR_1, LOW);
        digitalWrite(PIN_SSR_2, LOW);
    }
    if (service_motor_test_active && (millis() - service_motor_test_start > 2000)) {
        service_motor_test_active = false;
        digitalWrite(PIN_MOTOR_DOWN, LOW);
        digitalWrite(PIN_MOTOR_UP, LOW);
        // return to idle state
        transitionToState(STATE_IDLE);
    }

    tft.fillScreen(TFT_BLACK);
    switch (currentScreen) {
        case SCREEN_HOME: drawHomeScreen(); break;
        case SCREEN_PROGRAM_SELECT: drawProgramSelectScreen(); break;
        case SCREEN_PROGRAM_EDIT: drawProgramEditScreen(); break;
        case SCREEN_TIMER_EDIT: {
            // draw timer editor
            tft.fillRect(0, 0, 320, 25, TFT_NAVY);
            tft.setTextColor(TFT_WHITE, TFT_NAVY);
            tft.drawString("EDIT PROCESS TIME (HH:MM:SS)", 10, 5, 2);

            tft.setTextColor(timerEdit_field == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(timerEdit_h, 60, 60, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(":", 110, 60, 4);
            tft.setTextColor(timerEdit_field == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(timerEdit_m, 130, 60, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(":", 180, 60, 4);
            tft.setTextColor(timerEdit_field == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(timerEdit_s, 200, 60, 4);

            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.drawString("[UP/DN]: Change | [->]: Next | [OK]: Save | [<-]: Cancel", 10, 215, 2);
            break;
        }
        case SCREEN_RTC_SET: {
            tft.fillRect(0, 0, 320, 25, TFT_NAVY);
            tft.setTextColor(TFT_WHITE, TFT_NAVY);
            tft.drawString("SET RTC (YYYY-MM-DD HH:MM:SS)", 10, 5, 2);

            // Year
            tft.setTextColor(rtcEdit_field == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_year, 40, 60, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("-", 110, 60, 4);
            // Month
            tft.setTextColor(rtcEdit_field == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_month, 130, 60, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("-", 180, 60, 4);
            // Day
            tft.setTextColor(rtcEdit_field == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_day, 200, 60, 4);

            // Time
            tft.setTextColor(rtcEdit_field == 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_hour, 60, 120, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(":", 110, 120, 4);
            tft.setTextColor(rtcEdit_field == 4 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_minute, 130, 120, 4);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(":", 180, 120, 4);
            tft.setTextColor(rtcEdit_field == 5 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawNumber(rtcEdit_second, 200, 120, 4);

            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.drawString("[UP/DN]: Change | [->]: Next | [OK]: Save | [<-]: Cancel", 10, 215, 2);
            break;
        }
        case SCREEN_PID_TUNING: {
            // display PID values for active program
            ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];
            tft.fillRect(0, 0, 320, 25, TFT_NAVY);
            tft.setTextColor(TFT_WHITE, TFT_NAVY);
            tft.drawString("PID TUNING (Active PGM)", 10, 5, 2);

            tft.setTextColor(pidEdit_field == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H1 Kp:", 10, 40, 2);
            tft.drawFloat(prec.h1_Kp, 2, 120, 40, 2);

            tft.setTextColor(pidEdit_field == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H1 Ki:", 10, 70, 2);
            tft.drawFloat(prec.h1_Ki, 3, 120, 70, 2);

            tft.setTextColor(pidEdit_field == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H1 Kd:", 10, 100, 2);
            tft.drawFloat(prec.h1_Kd, 2, 120, 100, 2);

            tft.setTextColor(pidEdit_field == 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H2 Kp:", 10, 130, 2);
            tft.drawFloat(prec.h2_Kp, 2, 120, 130, 2);

            tft.setTextColor(pidEdit_field == 4 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H2 Ki:", 10, 160, 2);
            tft.drawFloat(prec.h2_Ki, 3, 120, 160, 2);

            tft.setTextColor(pidEdit_field == 5 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
            tft.drawString("H2 Kd:", 10, 190, 2);
            tft.drawFloat(prec.h2_Kd, 2, 120, 190, 2);

            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.drawString("[UP/DN]: Change | [->]: Next | [OK]: Save | [<-]: Back", 10, 215, 2);
            break;
        }
        case SCREEN_SERVICE: drawServiceScreen(); break;
    }

    // draw transient RTC confirmation popup if any
    if (rtcConfirmUntil && millis() < rtcConfirmUntil) {
        int w = 260; int h = 40;
        int x = (320 - w) / 2; int y = (240 - h) / 2;
        tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_WHITE);
        tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(rtcConfirmMsg, x + 8, y + 10, 2);
    } else {
        rtcConfirmUntil = 0;
    }

    // process RTC blink pattern (non-blocking)
    if (rtcBlinkActive) {
        if ((int32_t)(millis() - rtcBlinkNextToggle) >= 0) {
            // advance index
            rtcBlinkIdx++;
            if (rtcBlinkIdx >= rtcBlinkLen) {
                rtcBlinkActive = false;
                rtcBlinkPattern = NULL;
                rtcBlinkLen = 0;
                rtcBlinkIdx = 0;
                DEBUG_TP_LOW();
            } else {
                // toggle state
                rtcBlinkStateHigh = !rtcBlinkStateHigh;
                if (rtcBlinkStateHigh) DEBUG_TP_HIGH(); else DEBUG_TP_LOW();
                rtcBlinkNextToggle = millis() + rtcBlinkPattern[rtcBlinkIdx];
            }
        }
    }
}

void setupWebServer() {
    webServer.on("/", []() {
        String html = "<html><body><h1>Sun Lazer Dashboard</h1>";
        html += "<p>H1 Temp: " + String(sysStatus.h1_actual_c) + " C</p>";
        html += "<p>H2 Temp: " + String(sysStatus.h2_actual_c) + " C</p>";
        html += "<p>Torque: " + String(sysStatus.current_torque_nm) + " Nm</p>";
        html += "<p>State: " + String(stateNames[sysStatus.currentState]) + "</p>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
    });
    // Add RTC web handlers (GET /rtc, GET/POST /rtc/set)
    addRTCWebHandlers(webServer);
    webServer.begin();
}