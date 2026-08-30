#include "display_ui.h"
#include "control_tasks.h"
#include "storage.h"
#include "debug_config.h"
#include "rtc.h"
#include "gpio_safe.h"
#if ENABLE_SERIAL_TFT
#include "serial_display.h"
#endif
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

// Limit switch warning popup
static char limitSwitchWarningMsg[64] = "";
static uint32_t limitSwitchWarningUntil = 0; // millis until which to show the popup

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

void showLimitSwitchWarning(const char* message) {
    snprintf(limitSwitchWarningMsg, sizeof(limitSwitchWarningMsg), "%s", message);
    limitSwitchWarningUntil = millis() + 5000; // show popup for 5 seconds
}

void initDisplayAndWeb() {
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(TFT_BLACK);
    tft.drawString("SUN LAZER INITIALIZING...", 20, 110, 2);

#if ENABLE_SERIAL_TFT
    // init virtual serial display mirror for headless testing
    vd_init();
#endif

    WiFi.softAP("SunLazer_Config", "sunlazer123");
    setupWebServer();
}

#if ENABLE_APP_REMOTE
static volatile uint8_t g_appButtonMask = 0;

void injectAppButton(uint8_t btnMask) {
    g_appButtonMask |= btnMask;
}
#endif

void handleButtonInputs() {
    static uint32_t lastButtonPress = 0;
    if (millis() - lastButtonPress < 150) return; // Non-blocking debounce

    bool btnUp = false, btnDown = false, btnLeft = false, btnRight = false, btnOk = false;

#if ENABLE_APP_REMOTE
    uint8_t remoteMask = g_appButtonMask;
    if (remoteMask != 0) {
        g_appButtonMask = 0;
        if (remoteMask & APP_BTN_UP_BIT)    btnUp = true;
        if (remoteMask & APP_BTN_DOWN_BIT)  btnDown = true;
        if (remoteMask & APP_BTN_LEFT_BIT)  btnLeft = true;
        if (remoteMask & APP_BTN_RIGHT_BIT) btnRight = true;
        if (remoteMask & APP_BTN_OK_BIT)    btnOk = true;
    }
#endif

#if ENABLE_PHYSICAL_BUTTONS
    if (safeDigitalRead(PIN_BTN_UP) == LOW)    btnUp = true;
    if (safeDigitalRead(PIN_BTN_DOWN) == LOW)  btnDown = true;
    if (safeDigitalRead(PIN_BTN_LEFT) == LOW)  btnLeft = true;
    if (safeDigitalRead(PIN_BTN_RIGHT) == LOW) btnRight = true;
    if (safeDigitalRead(PIN_BTN_OK) == LOW)    btnOk = true;
#endif

#if INPUT_USE_SERIAL
    // Serial-mode: support two interaction styles:
    //  1) Single-key buttons '1'..'5' map to UP/DOWN/LEFT/RIGHT/OK
    //  2) Command lines starting with ':' allow advanced simulation (when INPUT_SERIAL_SIMULATOR=1)
    //     commands are entered as a line terminated by Enter, e.g. ':h1 120.5' or ':down on'
    static char cmdBuf[128];
    static size_t cmdLen = 0;
    static bool cmdMode = false; // true when a ':' command is active

    while (Serial && Serial.available()) {
        char c = Serial.read();
        // ignore bare CR/LF outside command termination
        if (c == '\r') continue;

        // Start command mode when user types ':'
        if (c == ':' && !cmdMode) {
            cmdMode = true;
            cmdLen = 0;
            // echo prompt
            Serial.println("[INPUT] Serial-sim command mode - enter command and press Enter");
            continue;
        }

        if (cmdMode) {
            // accumulate until newline
            if (c == '\n') {
                cmdBuf[cmdLen] = '\0';
#if INPUT_SERIAL_SIMULATOR
                // process command
                Serial.printf("[INPUT] Command: %s\n", cmdBuf);
                // parse tokens
                char *tok = strtok(cmdBuf, " \t");
                if (tok != NULL) {
                    // helper to get next token
                    char *arg = strtok(NULL, " \t");
                    if (strcmp(tok, "down") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) sysStatus.down_limit_active = true;
                        else if (strcmp(arg, "off") == 0) sysStatus.down_limit_active = false;
                        Serial.printf("[INPUT] Down limit set to: %s\n", sysStatus.down_limit_active ? "ON" : "OFF");
                    } else if (strcmp(tok, "home") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) sysStatus.home_limit_active = true;
                        else if (strcmp(arg, "off") == 0) sysStatus.home_limit_active = false;
                        Serial.printf("[INPUT] Home limit set to: %s\n", sysStatus.home_limit_active ? "ON" : "OFF");
                    } else if ((strcmp(tok, "h1") == 0 || strcmp(tok, "H1") == 0) && arg != NULL) {
                        float v = atof(arg);
                        sysStatus.h1_actual_c = v;
                        Serial.printf("[INPUT] H1 actual temp set to %.2f C\n", v);
                    } else if ((strcmp(tok, "h2") == 0 || strcmp(tok, "H2") == 0) && arg != NULL) {
                        float v = atof(arg);
                        sysStatus.h2_actual_c = v;
                        Serial.printf("[INPUT] H2 actual temp set to %.2f C\n", v);
                    } else if (strcmp(tok, "torque") == 0 && arg != NULL) {
                        float v = atof(arg);
                        sysStatus.current_torque_nm = v;
                        Serial.printf("[INPUT] Torque set to %.3f Nm\n", v);
                    } else if (strcmp(tok, "ssr1") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) safeDigitalWrite(PIN_SSR_1, HIGH);
                        else if (strcmp(arg, "off") == 0) safeDigitalWrite(PIN_SSR_1, LOW);
                        Serial.printf("[INPUT] SSR1 -> %s\n", digitalRead(PIN_SSR_1) ? "ON" : "OFF");
                    } else if (strcmp(tok, "ssr2") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) safeDigitalWrite(PIN_SSR_2, HIGH);
                        else if (strcmp(arg, "off") == 0) safeDigitalWrite(PIN_SSR_2, LOW);
                        Serial.printf("[INPUT] SSR2 -> %s\n", digitalRead(PIN_SSR_2) ? "ON" : "OFF");
                    } else if (strcmp(tok, "motor_down") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
                        else if (strcmp(arg, "off") == 0) safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                        Serial.printf("[INPUT] Motor down -> %s\n", digitalRead(PIN_MOTOR_DOWN) ? "ON" : "OFF");
                    } else if (strcmp(tok, "motor_up") == 0 && arg != NULL) {
                        if (strcmp(arg, "on") == 0) safeDigitalWrite(PIN_MOTOR_UP, HIGH);
                        else if (strcmp(arg, "off") == 0) safeDigitalWrite(PIN_MOTOR_UP, LOW);
                        Serial.printf("[INPUT] Motor up -> %s\n", digitalRead(PIN_MOTOR_UP) ? "ON" : "OFF");
                    } else if (strcmp(tok, "show") == 0) {
                        Serial.printf("[INPUT] Status: state=%s, H1=%.2f, H2=%.2f, torque=%.3f, down=%s, home=%s\n",
                                      stateNames[sysStatus.currentState], sysStatus.h1_actual_c, sysStatus.h2_actual_c, sysStatus.current_torque_nm,
                                      sysStatus.down_limit_active?"ON":"OFF", sysStatus.home_limit_active?"ON":"OFF");
                        Serial.printf("[INPUT] DN Failures: %u, HOME Failures: %u\n", (unsigned)sysStatus.down_limit_fail_count, (unsigned)sysStatus.home_limit_fail_count);
                    } else if (strcmp(tok, "reset_fail") == 0) {
                        sysStatus.down_limit_fail_count = 0;
                        sysStatus.home_limit_fail_count = 0;
                        sysStatus.last_down_limit_fail_ms = 0;
                        sysStatus.last_home_limit_fail_ms = 0;
                        Serial.println("[INPUT] Limit switch failure counters reset.");
                    } else {
                        Serial.printf("[INPUT] Unknown command: %s\n", tok);
                    }
                }
#else
                Serial.println("[INPUT] Serial-simulator disabled at compile time");
#endif
                // reset command mode
                cmdMode = false;
                cmdLen = 0;
            } else {
                // accumulate command character
                if (cmdLen < sizeof(cmdBuf) - 1) cmdBuf[cmdLen++] = c;
            }
            // continue consuming available characters
            continue;
        }

        // Not in command mode: treat single-char as immediate key (existing behavior)
        if (c == '\n') continue;
        Serial.printf("[INPUT] Key received: %c\n", c);
        switch (c) {
            case '1': btnUp = true; break;
            case '2': btnDown = true; break;
            case '3': btnLeft = true; break;
            case '4': btnRight = true; break;
            case '5': btnOk = true; break;
            default:
                // ignore other keys
                break;
        }
    }
#endif

    // if no interactive keypresses from Remote App, Physical Buttons, or Serial, return
    if (!btnUp && !btnDown && !btnLeft && !btnRight && !btnOk) return;
    lastButtonPress = millis();

    // If a force-start confirmation is pending and we're on Home screen, handle confirmation inputs
    if (sysStatus.forceStartPending && currentScreen == SCREEN_HOME) {
        // If OK pressed while pending -> force start
        if (btnOk) {
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
            transitionToState(STATE_SAFETY_CHECK);
#if ENABLE_SERIAL_TFT
            vd_popup("Force-start confirmed. Starting process...");
#else
            Serial.println("[START] Force-start confirmed. Starting process...");
#endif
            return;
        }
        // If Left pressed while pending -> cancel
        if (btnLeft) {
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
#if ENABLE_SERIAL_TFT
            vd_popup("Force-start canceled.");
#else
            Serial.println("[START] Force-start canceled.");
#endif
            return;
        }
        // ignore other buttons while confirmation active
        return;
    }

    switch (currentScreen) {
        case SCREEN_HOME:
            if (btnRight) {
                currentScreen = SCREEN_PROGRAM_SELECT;
            } else if (btnLeft) {
                currentScreen = SCREEN_SERVICE;
            } else if (btnOk) {
                if (sysStatus.currentState == STATE_IDLE || sysStatus.currentState == STATE_READY) {
                    // Check start mode: auto prevents starting until setpoints are reached
                    bool allowStart = true;
                    if (sysStatus.start_mode_auto) {
                        ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];
#if ENABLE_H1
                        if (fabsf(sysStatus.h1_actual_c - prec.h1_setpoint_c) > prec.temp_tolerance_c) {
                            allowStart = false;
                        }
#endif
#if ENABLE_H2
                        if (fabsf(sysStatus.h2_actual_c - prec.h2_setpoint_c) > prec.temp_tolerance_c) {
                            allowStart = false;
                        }
#endif
                    }

                    if (allowStart) {
                        transitionToState(STATE_SAFETY_CHECK);
                    } else {
                        // Start blocked by Auto mode: offer force-start confirmation
                        sysStatus.forceStartPending = true;
                        sysStatus.forceStartUntilMs = millis() + 8000; // 8s window to confirm
#if ENABLE_SERIAL_TFT
                        vd_popup("Setpoint not reached. Press OK to Force Start or LEFT to Cancel (8s)");
#else
                        Serial.println("[START] Setpoint not reached. Press OK to Force Start or LEFT to Cancel (8s)");
#endif
                    }
                } else if (sysStatus.currentState == STATE_ALARM_FAULT) {
                    transitionToState(STATE_IDLE);
                }
            }
            break;

        case SCREEN_PROGRAM_SELECT:
            if (btnUp) {
                if (sysStatus.active_program_idx > 0) {
                    sysStatus.active_program_idx--;
                    saveActiveProgramToNVS(sysStatus.active_program_idx);
                }
            } else if (btnDown) {
                if (sysStatus.active_program_idx < 9) {
                    sysStatus.active_program_idx++;
                    saveActiveProgramToNVS(sysStatus.active_program_idx);
                }
            } else if (btnRight || btnOk) {
                saveActiveProgramToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_PROGRAM_EDIT;
                selectedEditField = 0;
            } else if (btnLeft) {
                saveActiveProgramToNVS(sysStatus.active_program_idx);
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
            } else if (btnOk && btnLeft) {
                // Save all current recipes to NVS (user-requested)
                saveAllRecipesToNVS();
#if ENABLE_SERIAL_TFT
                vd_popup("All recipes saved to NVS.");
#else
                Serial.println("[NVS] All recipes saved to NVS.");
#endif
                // remain on service screen
            } else if (btnDown && !btnOk) {
                // Toggle start mode: Auto (block start until setpoint) vs Manual (allow start anytime)
                sysStatus.start_mode_auto = !sysStatus.start_mode_auto;
                saveStartModeToNVS(sysStatus.start_mode_auto);
#if ENABLE_SERIAL_TFT
                vd_popup(sysStatus.start_mode_auto ? "Start Mode: AUTO" : "Start Mode: MANUAL");
#else
                Serial.printf("[NVS] Start Mode set to: %s\n", sysStatus.start_mode_auto ? "AUTO" : "MANUAL");
#endif
            } else if (btnDown && btnRight) {
                // Reset limit switch failure counters
                sysStatus.down_limit_fail_count = 0;
                sysStatus.home_limit_fail_count = 0;
                sysStatus.last_down_limit_fail_ms = 0;
                sysStatus.last_home_limit_fail_ms = 0;
#if ENABLE_SERIAL_TFT
                vd_popup("Limit switch failure counters reset.");
#else
                Serial.println("[SERVICE] Limit switch failure counters reset.");
#endif
            } else if (btnLeft) {
                // cancel any active service tests
                service_heater_test_active = false;
                service_motor_test_active = false;
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
                safeDigitalWrite(PIN_MOTOR_UP, LOW);
                currentScreen = SCREEN_HOME;
            } else if (btnOk) {
                // Heater test: toggle SSRs for 2 seconds
                if (!service_heater_test_active) {
                    service_heater_test_active = true;
                    service_heater_test_start = millis();
                    safeDigitalWrite(PIN_SSR_1, HIGH);
                    safeDigitalWrite(PIN_SSR_2, HIGH);
                }
            } else if (btnRight) {
                // Motor jog down for 2 seconds (if down limit not active)
                if (!service_motor_test_active && !sysStatus.down_limit_active) {
                    service_motor_test_active = true;
                    service_motor_test_start = millis();
                    service_motor_down = true;
                    // put controller into READY so the safety task does not override motor
                    transitionToState(STATE_READY);
                    safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
                }
            } else if (btnUp) {
                // Motor jog up for 2 seconds (if home limit not active)
                if (!service_motor_test_active && !sysStatus.home_limit_active) {
                    service_motor_test_active = true;
                    service_motor_test_start = millis();
                    service_motor_down = false;
                    transitionToState(STATE_READY);
                    safeDigitalWrite(PIN_MOTOR_UP, HIGH);
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
    tft.fillRect(0, 0, 320, 24, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("SUN LAZER - HOME", 10, 4, 2);

    // Row 1: Active Program
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Active PGM:", 10, 30, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(recipes[sysStatus.active_program_idx].name, 115, 30, 2);

    // Row 2: Heater 1 Setpoint / Actual
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
#if ENABLE_H1
    tft.drawString("H1 Set/Act:", 10, 52, 2);
    tft.drawFloat(recipes[sysStatus.active_program_idx].h1_setpoint_c, 1, 115, 52, 2);
    tft.drawString("C /", 168, 52, 2);
    if (sysStatus.h1_actual_c < -10.0f) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("FAULT", 198, 52, 2);
    } else {
        tft.drawFloat(sysStatus.h1_actual_c, 1, 198, 52, 2);
        tft.drawString("C", 258, 52, 2);
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
#else
    tft.drawString("H1: DISABLED", 10, 52, 2);
#endif

    // Row 3: Heater 2 Setpoint / Actual
#if ENABLE_H2
    tft.drawString("H2 Set/Act:", 10, 74, 2);
    tft.drawFloat(recipes[sysStatus.active_program_idx].h2_setpoint_c, 1, 115, 74, 2);
    tft.drawString("C /", 168, 74, 2);
    if (sysStatus.h2_actual_c < -10.0f) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("FAULT", 198, 74, 2);
    } else {
        tft.drawFloat(sysStatus.h2_actual_c, 1, 198, 74, 2);
        tft.drawString("C", 258, 74, 2);
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
#else
    tft.drawString("H2: DISABLED", 10, 74, 2);
#endif

    // Row 4: Torque & Timer side-by-side
    tft.drawString("Torque:", 10, 96, 2);
    tft.drawFloat(sysStatus.current_torque_nm, 2, 75, 96, 2);
    tft.drawString("Nm", 130, 96, 2);

    tft.drawString("Timer:", 180, 96, 2);
    if (sysStatus.remaining_time_sec > 0) {
        uint32_t t = sysStatus.remaining_time_sec;
        uint32_t mm = t / 60;
        uint32_t ss = t % 60;
        char tb[16];
        snprintf(tb, sizeof(tb), "%02u:%02u", (unsigned)mm, (unsigned)ss);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString(tb, 235, 96, 2);
    } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("--:--", 235, 96, 2);
    }

    // Status line lookup
    const char *statusLine = "IDLE";
    switch (sysStatus.currentState) {
        case STATE_IDLE: statusLine = "Idle - Awaiting Start (press OK)"; break;
        case STATE_SAFETY_CHECK: statusLine = "Running safety checks"; break;
        case STATE_MOVE_DOWN: statusLine = "Moving down to start position"; break;
        case STATE_DOWN_LIMIT: statusLine = "At down limit - preparing to heat"; break;
        case STATE_HEAT_TO_SETPOINT: statusLine = "Heating - waiting for setpoints"; break;
        case STATE_TEMPERATURE_READY: statusLine = "Temp ready - starting timer"; break;
        case STATE_PROCESS_TIMER: statusLine = "Process running"; break;
        case STATE_TIMER_COMPLETE: statusLine = "Timer done - moving up"; break;
        case STATE_MOVE_UP: statusLine = "Moving up to home"; break;
        case STATE_HOME_LIMIT: statusLine = "At home - saving record"; break;
        case STATE_SAVE_RECORD: statusLine = "Saving record"; break;
        case STATE_PROCESS_COMPLETE: statusLine = "Process complete"; break;
        case STATE_READY: statusLine = "Ready"; break;
        case STATE_ALARM_FAULT: statusLine = "ALARM FAULT"; break;
        default: statusLine = "Unknown"; break;
    }

    // Row 5 & 6: Dedicated Status / Alarm message area
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Status:", 10, 122, 2);

    if (!sysStatus.boot_ok) {
        // Boot error display
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("BOOT CHECK FAILED", 75, 122, 2);

        char buf[64];
        strncpy(buf, sysStatus.boot_msg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        tft.fillRect(6, 144, 308, 48, TFT_DARKGREY);
        tft.fillRect(8, 146, 304, 44, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Error Details:", 14, 150, 2);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString(buf, 14, 170, 2);
    } else if (sysStatus.currentState == STATE_ALARM_FAULT) {
        // Alarm trip display
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SAFETY TRIP / FAULT", 75, 122, 2);

        char buf[64];
        strncpy(buf, sysStatus.alarm_msg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        tft.fillRect(6, 144, 308, 48, TFT_DARKGREY);
        tft.fillRect(8, 146, 304, 44, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Trip Reason:", 14, 150, 2);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString(buf, 14, 170, 2);
    } else {
        // Normal operating status
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        if (sysStatus.currentState == STATE_IDLE || sysStatus.currentState == STATE_READY) {
            tft.drawString("Ready (Press OK to start)", 75, 122, 2);
        } else {
            tft.drawString(statusLine, 75, 122, 2);
        }

        // Limit switches and Mode indicators in normal operation
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        char limitBuf[64];
        snprintf(limitBuf, sizeof(limitBuf), "DN Sw: %s | Home Sw: %s",
                 sysStatus.down_limit_active ? "CLOSED" : "OPEN",
                 sysStatus.home_limit_active ? "CLOSED" : "OPEN");
        tft.drawString(limitBuf, 10, 150, 2);

        char modeBuf[64];
        snprintf(modeBuf, sizeof(modeBuf), "Start Mode: %s | Max Torque: %.2f Nm",
                 sysStatus.start_mode_auto ? "AUTO" : "MANUAL",
                 sysStatus.max_torque_nm);
        tft.drawString(modeBuf, 10, 172, 2);
    }

    // Row 7: Navigation Prompt
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[OK]: Start | [->]: Program Menu", 10, 210, 2);

    // Row 8: Bottom indicator strip
    uint16_t bannerColor = (!sysStatus.boot_ok || sysStatus.currentState == STATE_ALARM_FAULT) ? TFT_RED : TFT_DARKGREEN;
    tft.fillRect(0, 235, 320, 5, bannerColor);
}

void drawProgramSelectScreen() {
    tft.fillRect(0, 0, 320, 24, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString("SELECT RECIPE (P01 - P10)", 10, 4, 2);

    const int items = 10;
    const int startX = 20;
    const int tempX = 180;
    const int unitX = 220;
    const int startY = 32;
    const int lineH = 17; // compact spacing to fit all 10 lines comfortably above footer

    // clear the list area
    tft.fillRect(0, startY - 2, 320, items * lineH + 4, TFT_BLACK);

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
    tft.drawString("DN Limit: ", 10, 35, 2);
    tft.drawString(sysStatus.down_limit_active ? "ACTIVE" : "OPEN", 100, 35, 2);

    tft.drawString("HOME Limit: ", 10, 55, 2);
    tft.drawString(sysStatus.home_limit_active ? "ACTIVE" : "OPEN", 100, 55, 2);

    // Show limit switch failure counts with warning color if failures exist
    tft.setTextColor(sysStatus.down_limit_fail_count > 0 ? TFT_RED : TFT_WHITE, TFT_BLACK);
    char dnFailMsg[32];
    snprintf(dnFailMsg, sizeof(dnFailMsg), "DN Fail: %u", (unsigned)sysStatus.down_limit_fail_count);
    tft.drawString(dnFailMsg, 200, 35, 2);

    tft.setTextColor(sysStatus.home_limit_fail_count > 0 ? TFT_RED : TFT_WHITE, TFT_BLACK);
    char homeFailMsg[32];
    snprintf(homeFailMsg, sizeof(homeFailMsg), "HM Fail: %u", (unsigned)sysStatus.home_limit_fail_count);
    tft.drawString(homeFailMsg, 200, 55, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Torque (Nm): ", 10, 80, 2);
    tft.drawFloat(sysStatus.current_torque_nm, 2, 100, 80, 2);
    tft.drawString("Max: ", 150, 80, 2);
    tft.drawFloat(sysStatus.max_torque_nm, 2, 190, 80, 2);

    // Show service test statuses
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    if (service_heater_test_active) tft.drawString("Heater Test: RUNNING", 10, 105, 2);
    else tft.drawString("[OK]: Heater Test", 10, 105, 2);

    if (service_motor_test_active) {
        tft.drawString(service_motor_down ? "Motor Jog: DOWN" : "Motor Jog: UP", 150, 105, 2);
    } else {
        tft.drawString("[UP]/[->]: Motor Jog", 150, 105, 2);
    }

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[<-]: Back | [OK]: Heater Test | [UP]/[->]: Motor Jog", 10, 140, 2);
    tft.drawString("[->]+[OK]: PID Tune | [DN]+[OK]: Set RTC", 10, 160, 2);
    tft.drawString("[DN]: Toggle Start Mode | [DN]+[->]: Reset Fail Counters", 10, 180, 2);
    tft.drawString("[OK]+[<-]: Save Defaults", 10, 200, 2);
}

void updateTFTDisplay() {
    // handle service test timeouts
    if (service_heater_test_active && (millis() - service_heater_test_start > 2000)) {
        service_heater_test_active = false;
        safeDigitalWrite(PIN_SSR_1, LOW);
        safeDigitalWrite(PIN_SSR_2, LOW);
    }
    if (service_motor_test_active && (millis() - service_motor_test_start > 2000)) {
        service_motor_test_active = false;
        safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
        safeDigitalWrite(PIN_MOTOR_UP, LOW);
        // return to idle state
        transitionToState(STATE_IDLE);
    }

    tft.fillScreen(TFT_BLACK);
    switch (currentScreen) {
        case SCREEN_HOME: drawHomeScreen();
#if ENABLE_SERIAL_TFT
            vd_drawHomeScreen();
#endif
            break;
        case SCREEN_PROGRAM_SELECT: drawProgramSelectScreen();
#if ENABLE_SERIAL_TFT
            vd_drawProgramSelectScreen();
#endif
            break;
        case SCREEN_PROGRAM_EDIT: drawProgramEditScreen();
#if ENABLE_SERIAL_TFT
            vd_drawProgramEditScreen();
#endif
            break;
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
#if ENABLE_SERIAL_TFT
            vd_drawTimerEditor();
#endif
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
#if ENABLE_SERIAL_TFT
            vd_drawRTCSetScreen();
#endif
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
#if ENABLE_SERIAL_TFT
            vd_drawPIDTuningScreen();
#endif
            break;
        }
        case SCREEN_SERVICE: drawServiceScreen();
#if ENABLE_SERIAL_TFT
            vd_drawServiceScreen();
#endif
            break;
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

    // draw limit switch warning popup if any
    if (limitSwitchWarningUntil && millis() < limitSwitchWarningUntil) {
        int w = 280; int h = 50;
        int x = (320 - w) / 2; int y = (240 - h) / 2;
        tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_RED);
        tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("WARNING!", x + 8, y + 5, 2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(limitSwitchWarningMsg, x + 8, y + 25, 2);
    } else {
        limitSwitchWarningUntil = 0;
    }

    // draw force-start confirmation overlay if pending
    if (sysStatus.forceStartPending) {
        if ((int32_t)(sysStatus.forceStartUntilMs - millis()) <= 0) {
            // timeout, clear pending
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
        } else {
            int w = 300; int h = 60;
            int x = (320 - w) / 2; int y = (240 - h) / 2;
            tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_WHITE);
            tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("Confirm Force Start?", x + 8, y + 8, 2);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            uint32_t remaining = (sysStatus.forceStartUntilMs > millis()) ? (sysStatus.forceStartUntilMs - millis())/1000 : 0;
            char msg[64];
            snprintf(msg, sizeof(msg), "Press OK to Force Start, LEFT to Cancel (%us)", (unsigned)remaining);
            tft.drawString(msg, x + 8, y + 28, 2);
        }
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

    // Ensure TFT chip-select is explicitly de-asserted (HIGH) so other SPI devices can access the bus cleanly
    safeDigitalWrite(PIN_TFT_CS, HIGH);
}

static void sendCorsHeaders() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

#if ENABLE_APP_REMOTE

static const char* getScreenName(UIScreen_t screen) {
    switch (screen) {
        case SCREEN_HOME: return "HOME";
        case SCREEN_PROGRAM_SELECT: return "PROGRAM_SELECT";
        case SCREEN_PROGRAM_EDIT: return "PROGRAM_EDIT";
        case SCREEN_TIMER_EDIT: return "TIMER_EDIT";
        case SCREEN_PID_TUNING: return "PID_TUNING";
        case SCREEN_SERVICE: return "SERVICE";
        case SCREEN_RTC_SET: return "RTC_SET";
        default: return "UNKNOWN";
    }
}

static void handleApiStatus() {
    sendCorsHeaders();
    String json = "{";
    json += "\"state\":" + String((int)sysStatus.currentState) + ",";
    json += "\"state_name\":\"" + String(stateNames[sysStatus.currentState]) + "\",";
    json += "\"screen\":" + String((int)currentScreen) + ",";
    json += "\"screen_name\":\"" + String(getScreenName(currentScreen)) + "\",";
    json += "\"selected_edit_field\":" + String(selectedEditField) + ",";
    json += "\"timer_edit_field\":" + String(timerEdit_field) + ",";
    json += "\"pid_edit_field\":" + String(pidEdit_field) + ",";
    json += "\"rtc_edit_field\":" + String(rtcEdit_field) + ",";
    json += "\"h1_actual\":" + String(sysStatus.h1_actual_c, 2) + ",";
    json += "\"h2_actual\":" + String(sysStatus.h2_actual_c, 2) + ",";
    ProgramRecipe_t &curRec = recipes[sysStatus.active_program_idx];
    json += "\"h1_setpoint\":" + String(curRec.h1_setpoint_c, 2) + ",";
    json += "\"h2_setpoint\":" + String(curRec.h2_setpoint_c, 2) + ",";
    json += "\"torque\":" + String(sysStatus.current_torque_nm, 3) + ",";
    json += "\"max_torque\":" + String(sysStatus.max_torque_nm, 3) + ",";
    json += "\"torque_limit\":" + String(curRec.torque_limit_nm, 2) + ",";
    json += "\"temp_tolerance\":" + String(curRec.temp_tolerance_c, 2) + ",";
    json += "\"remaining_time_sec\":" + String(sysStatus.remaining_time_sec) + ",";
    json += "\"total_time_sec\":" + String(curRec.process_time_sec) + ",";
    json += "\"down_limit\":" + String(sysStatus.down_limit_active ? "true" : "false") + ",";
    json += "\"home_limit\":" + String(sysStatus.home_limit_active ? "true" : "false") + ",";
    json += "\"motor_down\":" + String(sysStatus.motor_down_running ? "true" : "false") + ",";
    json += "\"motor_up\":" + String(sysStatus.motor_up_running ? "true" : "false") + ",";
    json += "\"ssr1\":" + String(digitalRead(PIN_SSR_1) ? "true" : "false") + ",";
    json += "\"ssr2\":" + String(digitalRead(PIN_SSR_2) ? "true" : "false") + ",";
    json += "\"active_prog_idx\":" + String(sysStatus.active_program_idx) + ",";
    json += "\"prog_name\":\"" + String(curRec.name) + "\",";
    json += "\"alarm_msg\":\"" + String(sysStatus.alarm_msg) + "\",";
    json += "\"boot_ok\":" + String(sysStatus.boot_ok ? "true" : "false") + ",";
    json += "\"boot_msg\":\"" + String(sysStatus.boot_msg) + "\",";
    json += "\"start_mode_auto\":" + String(sysStatus.start_mode_auto ? "true" : "false") + ",";
    json += "\"force_start_pending\":" + String(sysStatus.forceStartPending ? "true" : "false") + ",";
    json += "\"down_fail_count\":" + String(sysStatus.down_limit_fail_count) + ",";
    json += "\"home_fail_count\":" + String(sysStatus.home_limit_fail_count) + ",";
    char rtcBuf[32];
    getTimestampForLog(rtcBuf, sizeof(rtcBuf));
    json += "\"rtc_time\":\"" + String(rtcBuf) + "\",";
    json += "\"uptime_ms\":" + String(millis());
    json += "}";
    webServer.send(200, "application/json", json);
}

static void handleApiButton() {
    sendCorsHeaders();
    String key = "";
    if (webServer.hasArg("key")) {
        key = webServer.arg("key");
    } else if (webServer.hasArg("plain")) {
        String body = webServer.arg("plain");
        if (body.indexOf("\"up\"") >= 0 || body.indexOf("UP") >= 0 || body.indexOf("up") >= 0) key = "up";
        else if (body.indexOf("\"down\"") >= 0 || body.indexOf("DOWN") >= 0 || body.indexOf("down") >= 0) key = "down";
        else if (body.indexOf("\"left\"") >= 0 || body.indexOf("LEFT") >= 0 || body.indexOf("left") >= 0) key = "left";
        else if (body.indexOf("\"right\"") >= 0 || body.indexOf("RIGHT") >= 0 || body.indexOf("right") >= 0) key = "right";
        else if (body.indexOf("\"ok\"") >= 0 || body.indexOf("OK") >= 0 || body.indexOf("ok") >= 0) key = "ok";
    }

    uint8_t mask = 0;
    if (key.equalsIgnoreCase("up") || key == "1") mask = APP_BTN_UP_BIT;
    else if (key.equalsIgnoreCase("down") || key == "2") mask = APP_BTN_DOWN_BIT;
    else if (key.equalsIgnoreCase("left") || key == "3") mask = APP_BTN_LEFT_BIT;
    else if (key.equalsIgnoreCase("right") || key == "4") mask = APP_BTN_RIGHT_BIT;
    else if (key.equalsIgnoreCase("ok") || key == "5") mask = APP_BTN_OK_BIT;

    if (mask != 0) {
        injectAppButton(mask);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"injected\":\"" + key + "\"}");
    } else {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid key (use up, down, left, right, ok)\"}");
    }
}

static void handleApiRecipesGet() {
    sendCorsHeaders();
    String json = "[";
    for (int i = 0; i < 10; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"idx\":" + String(i) + ",";
        json += "\"name\":\"" + String(recipes[i].name) + "\",";
        json += "\"h1_setpoint\":" + String(recipes[i].h1_setpoint_c, 1) + ",";
        json += "\"h2_setpoint\":" + String(recipes[i].h2_setpoint_c, 1) + ",";
        json += "\"process_time_sec\":" + String(recipes[i].process_time_sec) + ",";
        json += "\"torque_limit\":" + String(recipes[i].torque_limit_nm, 2) + ",";
        json += "\"temp_tolerance\":" + String(recipes[i].temp_tolerance_c, 1) + ",";
        json += "\"h1_Kp\":" + String(recipes[i].h1_Kp, 3) + ",";
        json += "\"h1_Ki\":" + String(recipes[i].h1_Ki, 3) + ",";
        json += "\"h1_Kd\":" + String(recipes[i].h1_Kd, 3) + ",";
        json += "\"h2_Kp\":" + String(recipes[i].h2_Kp, 3) + ",";
        json += "\"h2_Ki\":" + String(recipes[i].h2_Ki, 3) + ",";
        json += "\"h2_Kd\":" + String(recipes[i].h2_Kd, 3);
        json += "}";
    }
    json += "]";
    webServer.send(200, "application/json", json);
}

static void handleApiRecipePost() {
    sendCorsHeaders();
    if (!webServer.hasArg("idx")) {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing idx argument (0-9)\"}");
        return;
    }
    int idx = webServer.arg("idx").toInt();
    if (idx < 0 || idx > 9) {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"idx out of range (0-9)\"}");
        return;
    }

    if (webServer.hasArg("name")) {
        strncpy(recipes[idx].name, webServer.arg("name").c_str(), sizeof(recipes[idx].name) - 1);
        recipes[idx].name[sizeof(recipes[idx].name) - 1] = '\0';
    }
    if (webServer.hasArg("h1_setpoint")) recipes[idx].h1_setpoint_c = webServer.arg("h1_setpoint").toFloat();
    if (webServer.hasArg("h2_setpoint")) recipes[idx].h2_setpoint_c = webServer.arg("h2_setpoint").toFloat();
    if (webServer.hasArg("process_time_sec")) recipes[idx].process_time_sec = webServer.arg("process_time_sec").toInt();
    if (webServer.hasArg("torque_limit")) recipes[idx].torque_limit_nm = webServer.arg("torque_limit").toFloat();
    if (webServer.hasArg("temp_tolerance")) recipes[idx].temp_tolerance_c = webServer.arg("temp_tolerance").toFloat();
    if (webServer.hasArg("h1_Kp")) recipes[idx].h1_Kp = webServer.arg("h1_Kp").toFloat();
    if (webServer.hasArg("h1_Ki")) recipes[idx].h1_Ki = webServer.arg("h1_Ki").toFloat();
    if (webServer.hasArg("h1_Kd")) recipes[idx].h1_Kd = webServer.arg("h1_Kd").toFloat();
    if (webServer.hasArg("h2_Kp")) recipes[idx].h2_Kp = webServer.arg("h2_Kp").toFloat();
    if (webServer.hasArg("h2_Ki")) recipes[idx].h2_Ki = webServer.arg("h2_Ki").toFloat();
    if (webServer.hasArg("h2_Kd")) recipes[idx].h2_Kd = webServer.arg("h2_Kd").toFloat();

    saveRecipeToNVS(idx);
    webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Recipe updated and saved to NVS\"}");
}

static void handleApiControl() {
    sendCorsHeaders();
    String action = webServer.arg("action");
    if (action.equalsIgnoreCase("start")) {
        if (sysStatus.currentState == STATE_IDLE || sysStatus.currentState == STATE_READY) {
            bool allowStart = true;
            if (sysStatus.start_mode_auto) {
                ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];
#if ENABLE_H1
                if (fabsf(sysStatus.h1_actual_c - prec.h1_setpoint_c) > prec.temp_tolerance_c) {
                    allowStart = false;
                }
#endif
#if ENABLE_H2
                if (fabsf(sysStatus.h2_actual_c - prec.h2_setpoint_c) > prec.temp_tolerance_c) {
                    allowStart = false;
                }
#endif
            }
            if (allowStart) {
                transitionToState(STATE_SAFETY_CHECK);
                webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Process started\"}");
            } else {
                sysStatus.forceStartPending = true;
                sysStatus.forceStartUntilMs = millis() + 8000;
                webServer.send(200, "application/json", "{\"status\":\"pending\",\"message\":\"Setpoint not reached. Force start pending.\"}");
            }
            return;
        } else {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"System not in IDLE or READY state\"}");
            return;
        }
    } else if (action.equalsIgnoreCase("force_start")) {
        if (sysStatus.forceStartPending || sysStatus.currentState == STATE_IDLE || sysStatus.currentState == STATE_READY) {
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
            transitionToState(STATE_SAFETY_CHECK);
            webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Force start executed\"}");
            return;
        }
    } else if (action.equalsIgnoreCase("cancel_force")) {
        sysStatus.forceStartPending = false;
        sysStatus.forceStartUntilMs = 0;
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Force start canceled\"}");
        return;
    } else if (action.equalsIgnoreCase("reset_alarm") || action.equalsIgnoreCase("stop")) {
        if (sysStatus.currentState == STATE_ALARM_FAULT) {
            transitionToState(STATE_IDLE);
            webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Alarm reset to IDLE\"}");
            return;
        } else {
            triggerSafetyShutdown("MANUAL APP STOP TRIP");
            webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Manual stop triggered\"}");
            return;
        }
    } else if (action.equalsIgnoreCase("toggle_start_mode")) {
        sysStatus.start_mode_auto = !sysStatus.start_mode_auto;
        saveStartModeToNVS(sysStatus.start_mode_auto);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"start_mode_auto\":" + String(sysStatus.start_mode_auto ? "true" : "false") + "}");
        return;
    } else if (action.equalsIgnoreCase("reset_fails")) {
        sysStatus.down_limit_fail_count = 0;
        sysStatus.home_limit_fail_count = 0;
        sysStatus.last_down_limit_fail_ms = 0;
        sysStatus.last_home_limit_fail_ms = 0;
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Limit switch failures reset\"}");
        return;
    } else if (action.equalsIgnoreCase("select_program")) {
        if (webServer.hasArg("idx")) {
            int idx = webServer.arg("idx").toInt();
            if (idx >= 0 && idx < 10) {
                sysStatus.active_program_idx = idx;
                saveActiveProgramToNVS(idx);
                webServer.send(200, "application/json", "{\"status\":\"ok\",\"active_program_idx\":" + String(idx) + "}");
                return;
            }
        }
    } else if (action.equalsIgnoreCase("jog_up")) {
        safeDigitalWrite(PIN_MOTOR_UP, HIGH);
        delay(500);
        safeDigitalWrite(PIN_MOTOR_UP, LOW);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Jog up 500ms executed\"}");
        return;
    } else if (action.equalsIgnoreCase("jog_down")) {
        safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
        delay(500);
        safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Jog down 500ms executed\"}");
        return;
    } else if (action.equalsIgnoreCase("toggle_ssr1")) {
        int st = digitalRead(PIN_SSR_1);
        safeDigitalWrite(PIN_SSR_1, st ? LOW : HIGH);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"ssr1\":" + String(!st ? "true" : "false") + "}");
        return;
    } else if (action.equalsIgnoreCase("toggle_ssr2")) {
        int st = digitalRead(PIN_SSR_2);
        safeDigitalWrite(PIN_SSR_2, st ? LOW : HIGH);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"ssr2\":" + String(!st ? "true" : "false") + "}");
        return;
    } else if (action.equalsIgnoreCase("set_screen")) {
        if (webServer.hasArg("screen")) {
            int sc = webServer.arg("screen").toInt();
            if (sc >= 0 && sc <= 6) {
                currentScreen = (UIScreen_t)sc;
                webServer.send(200, "application/json", "{\"status\":\"ok\",\"screen\":" + String(sc) + "}");
                return;
            }
        }
    }

    webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown or invalid control action\"}");
}

static void handleApiLogs() {
    sendCorsHeaders();
    String json = "[";
    for (uint8_t i = 0; i < LOG_RECENT_COUNT; i++) {
        const char* l = getRecentLog(i);
        if (!l) break;
        if (i > 0) json += ",";
        json += "\"" + String(l) + "\"";
    }
    json += "]";
    webServer.send(200, "application/json", json);
}

void setupAppRemoteEndpoints() {
    // CORS preflight handlers
    auto sendOptions = []() {
        sendCorsHeaders();
        webServer.send(204);
    };

    webServer.on("/api/status", HTTP_OPTIONS, sendOptions);
    webServer.on("/api/button", HTTP_OPTIONS, sendOptions);
    webServer.on("/api/recipes", HTTP_OPTIONS, sendOptions);
    webServer.on("/api/recipe", HTTP_OPTIONS, sendOptions);
    webServer.on("/api/control", HTTP_OPTIONS, sendOptions);
    webServer.on("/api/logs", HTTP_OPTIONS, sendOptions);

    webServer.on("/api/status", HTTP_GET, handleApiStatus);
    webServer.on("/api/button", HTTP_POST, handleApiButton);
    webServer.on("/api/button", HTTP_GET, handleApiButton);
    webServer.on("/api/recipes", HTTP_GET, handleApiRecipesGet);
    webServer.on("/api/recipe", HTTP_POST, handleApiRecipePost);
    webServer.on("/api/control", HTTP_POST, handleApiControl);
    webServer.on("/api/control", HTTP_GET, handleApiControl);
    webServer.on("/api/logs", HTTP_GET, handleApiLogs);
}
#endif

void setupWebServer() {
    webServer.on("/", []() {
        String html = "<html><head><title>Sun Lazer Dashboard</title></head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:20px;'>";
        html += "<h2>Sun Lazer Dual Heater Controller</h2>";
        html += "<p><b>State:</b> " + String(stateNames[sysStatus.currentState]) + "</p>";
        html += "<p><b>H1 Temp:</b> " + String(sysStatus.h1_actual_c, 2) + " &deg;C</p>";
        html += "<p><b>H2 Temp:</b> " + String(sysStatus.h2_actual_c, 2) + " &deg;C</p>";
        html += "<p><b>Torque:</b> " + String(sysStatus.current_torque_nm, 3) + " Nm</p>";
        html += "<p><b>Active Program:</b> " + String(recipes[sysStatus.active_program_idx].name) + "</p>";
        html += "<hr><p>Android App Remote API: <code>/api/status</code>, <code>/api/button</code>, <code>/api/recipes</code>, <code>/api/control</code></p>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
    });
    // Add RTC web handlers (GET /rtc, GET/POST /rtc/set)
    addRTCWebHandlers(webServer);

#if ENABLE_APP_REMOTE
    setupAppRemoteEndpoints();
#endif

    // Catch-all handler for CORS OPTIONS preflight, captive portal checks, and 404s
    webServer.onNotFound([]() {
        sendCorsHeaders();
        String uri = webServer.uri();
        HTTPMethod method = webServer.method();
        // CORS preflight requests
        if (method == HTTP_OPTIONS) {
            webServer.send(204);
            return;
        }
        // Android & iOS captive portal / connectivity probes
        if (uri == "/generate_204" || uri == "/gen_204" || uri == "/connectivity-check.html" ||
            uri == "/ncsi.txt" || uri == "/hotspot-detect.html" || uri == "/favicon.ico") {
            webServer.send(204);
            return;
        }
        if (uri.startsWith("/api/")) {
            webServer.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Not found: " + uri + "\"}");
        } else {
            webServer.send(404, "text/plain", "Not found: " + uri);
        }
    });

    webServer.begin();
}