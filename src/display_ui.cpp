#include "display_ui.h"
#include "control_tasks.h"
#include "storage.h"
#include "debug_config.h"
#include "rtc.h"
#include "gpio_safe.h"
#if ENABLE_SERIAL_TFT
#include "serial_display.h"
#endif
#if ENABLE_WIFI_WEBSERVER
#include <WiFi.h>
#endif

// FreeSansBold Font Definitions for TFT Display (included via TFT_eSPI.h)
#define FONT_FREE_BOLD_9  (&FreeSansBold9pt7b)
#define FONT_FREE_BOLD_12 (&FreeSansBold12pt7b)
#define FONT_FREE_BOLD_18 (&FreeSansBold18pt7b)

UIScreen_t currentScreen = SCREEN_HOME;
uint8_t selectedEditField = 0; // 0..7: Name, H1, H2, Time, Tol, H1 Offset %, H2 Offset %, Torque Unit
static bool s_inValueEditMode = false; // false = Navigation Mode, true = Value Edit Mode
static uint8_t settingsMenuIdx = 0; // 0: Recipes, 1: Start Mode, 2: Relay Type, 3: PID, 4: RTC, 5: Reset, 6: Exit

// Factory Reset PIN state (default PIN: 12345)
static uint8_t s_pinDigits[5] = {0, 0, 0, 0, 0};
static uint8_t s_pinFocus = 0; // 0..4: digits, 5: [CONFIRM], 6: [CANCEL]
static uint32_t s_pinMsgUntil = 0;
static bool s_pinSuccess = false;

// Program Name Edit state
static char s_nameEditBuf[16] = {0};
static uint8_t s_nameSlotIdx = 0; // 0..11
static uint8_t s_nameFocus = 0; // 0: slots, 1: [SAVE], 2: [CANCEL], 3: [CLEAR]

// Timer edit state
static uint8_t timerEdit_h = 0;
static uint8_t timerEdit_m = 0;
static uint8_t timerEdit_s = 0;
static uint8_t timerEdit_field = 0; // 0=h,1=m,2=s

static uint8_t tempManipField = 0; // 0=H1, 1=H2

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

static uint32_t s_bootTimeSec = 0;
static void initBootTime() {
    int h = 0, m = 0, s = 0;
    if (sscanf(__TIME__, "%d:%d:%d", &h, &m, &s) == 3) {
        s_bootTimeSec = h * 3600 + m * 60 + s;
    } else {
        s_bootTimeSec = 12 * 3600; // 12:00:00 default
    }
}

static void getCurrentTimeString(char *timeBuf, size_t timeBufLen) {
    uint16_t y = 2026;
    uint8_t mo = 1, d = 1, hh = 0, mm = 0, ss = 0;
    if (getRTCTimeComponents(&y, &mo, &d, &hh, &mm, &ss)) {
        snprintf(timeBuf, timeBufLen, "%02u:%02u:%02u", hh, mm, ss);
    } else {
        if (s_bootTimeSec == 0) initBootTime();
        uint32_t curSec = (s_bootTimeSec + (millis() / 1000)) % 86400;
        uint32_t s = curSec % 60;
        uint32_t m = (curSec / 60) % 60;
        uint32_t h = (curSec / 3600) % 24;
        snprintf(timeBuf, timeBufLen, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    }
}

void drawMobileHeader(const char* rightBadgeText, uint16_t badgeColor = TFT_YELLOW) {
    // Setting Mode & Sub-screens Header: Full 40px height with Status Bar (y = 0..40, h = 40)
    tft.fillRect(0, 0, 320, 40, 0x0841); // Dark charcoal
    tft.drawFastHLine(0, 40, 320, TFT_DARKCYAN);

    // Status Bar Row (y = 0..16)
    char timeStr[16];
    getCurrentTimeString(timeStr, sizeof(timeStr));
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawString(timeStr, 10, 2);

    // Status Badges on Top-Right: [AUTO/MAN] [SD] [WiFi]
    int badgeX = 310;
    const char *modeStr = sysStatus.start_mode_auto ? "AUTO" : "MAN";
    uint16_t modeCol = sysStatus.start_mode_auto ? TFT_GREEN : TFT_YELLOW;
    badgeX -= (strlen(modeStr) * 9 + 6);
    tft.setTextColor(modeCol, 0x0841);
    tft.drawString(modeStr, badgeX, 2);

    badgeX -= 32;
    tft.setTextColor(sysStatus.sd_present ? TFT_GREEN : 0x52AA, 0x0841);
    tft.drawString("SD", badgeX, 2);

    badgeX -= 44;
#if ENABLE_WIFI_WEBSERVER
    tft.setTextColor(TFT_CYAN, 0x0841);
#else
    tft.setTextColor(0x52AA, 0x0841); // Dimmed
#endif
    tft.drawString("WiFi", badgeX, 2);

    // App Bar Row (y = 16..39)
    tft.setFreeFont(FONT_FREE_BOLD_12);
    tft.setTextColor(TFT_CYAN, 0x0841);
    tft.drawString("Sun Smart", 10, 20);

    // Right Context Badge
    if (rightBadgeText && rightBadgeText[0]) {
        tft.setTextColor(badgeColor, 0x0841);
        tft.setTextPadding(140);
        tft.drawString(rightBadgeText, 170, 20);
        tft.setTextPadding(0);
    }
    tft.setFreeFont(FONT_FREE_BOLD_9);
}

static uint32_t s_welcomeStartMs = 0;
static bool s_welcomeDone = false;

void drawWelcomeScreen(uint32_t elapsedMs) {
    static bool s_welcomeStaticDrawn = false;
    if (!s_welcomeStaticDrawn || elapsedMs == 0) {
        tft.fillScreen(TFT_BLACK);

        // 1. Outer Modern Card with Dark Cyan Border (x=10, y=10, w=300, h=220)
        tft.drawRoundRect(10, 10, 300, 220, 8, 0x03EF);

        // 2. Top Title Header (y=12..48, h=36)
        tft.fillRect(12, 12, 296, 36, 0x0841);
        tft.drawFastHLine(10, 48, 300, 0x03EF);

        // Title: SUN SMART (FreeSansBold18)
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(TFT_CYAN, 0x0841);
        tft.drawCentreString("SUN SMART", 160, 18);

        // Subtitle: DUAL HEATER CONTROLLER (FreeSansBold9)
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawCentreString("DUAL HEATER CONTROLLER", 160, 56);

        // Divider Line
        tft.drawFastHLine(24, 76, 272, 0x4A69);

        // 3. Information Details (Labels in Light Grey, Values in Color)
        uint64_t chipId = ESP.getEfuseMac();
        char snStr[24];
        snprintf(snStr, sizeof(snStr), "%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);

        // FW Version
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("FW VERSION:", 28, 88);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString(FIRMWARE_VERSION, 160, 88);

        // ESP32 Serial Number
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("SERIAL NO:", 28, 114);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(snStr, 160, 114);

        // Software Build Date
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("BUILD DATE:", 28, 140);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(__DATE__, 160, 140);

        // Software Build Time
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("BUILD TIME:", 28, 166);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(__TIME__, 160, 166);

        // 4. Bottom Loading Bar Outline
        tft.drawRoundRect(28, 212, 264, 8, 3, 0x4A69);

        s_welcomeStaticDrawn = true;
    }

    // Dynamic Progress Bar & Countdown
    uint32_t remainingSec = (elapsedMs < 5000) ? ((5000 - elapsedMs + 999) / 1000) : 0;
    char secBuf[16];
    snprintf(secBuf, sizeof(secBuf), "%us", (unsigned)remainingSec);

    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(0x52AA, TFT_BLACK);
    tft.drawString("STARTING SYSTEM...", 28, 194);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextPadding(40);
    tft.drawRightString(secBuf, 292, 194);
    tft.setTextPadding(0);

    // Fill progress bar (0 to 260px)
    int fillW = (int)((elapsedMs * 260) / 5000);
    if (fillW > 260) fillW = 260;
    if (fillW > 0) {
        tft.fillRoundRect(30, 214, fillW, 4, 2, TFT_GREEN);
    }
}

void initDisplayAndWeb() {
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    s_welcomeStartMs = millis();
    s_welcomeDone = false;
    drawWelcomeScreen(0);

#if ENABLE_SERIAL_TFT
    // init virtual serial display mirror for headless testing
    vd_init();
#endif

#if ENABLE_WIFI_WEBSERVER
    WiFi.softAP("SunLazer_Config", "sunlazer123");
    setupWebServer();
#endif
}

#if ENABLE_APP_REMOTE
static volatile uint8_t g_appButtonMask = 0;

void injectAppButton(uint8_t btnMask) {
    g_appButtonMask |= btnMask;
}
#endif

void handleButtonInputs() {
    if (!s_welcomeDone) {
        return; // Suppress button inputs during 5-second welcome screen
    }

    // Industrial Button State Machine: 30ms stable debounce filter & edge detection
    enum BtnIndex { BTN_IDX_UP = 0, BTN_IDX_DOWN, BTN_IDX_LEFT, BTN_IDX_RIGHT, BTN_IDX_OK, BTN_COUNT };
    static const uint8_t s_btnPins[BTN_COUNT] = { PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_OK };
    static bool s_stableState[BTN_COUNT]      = { false, false, false, false, false };
    static bool s_lastRaw[BTN_COUNT]          = { false, false, false, false, false };
    static uint32_t s_lastChangeMs[BTN_COUNT] = { 0, 0, 0, 0, 0 };

    // Typematic auto-repeat state for UP and DOWN
    static uint32_t s_holdStartTime[2]  = { 0, 0 }; // [0]=UP, [1]=DOWN
    static uint32_t s_nextRepeatMs[2]   = { 0, 0 };
    static uint16_t s_upHoldCount       = 0;
    static uint16_t s_downHoldCount     = 0;

    uint32_t now = millis();
    bool justPressed[BTN_COUNT]  = { false, false, false, false, false };
    bool justReleased[BTN_COUNT] = { false, false, false, false, false };

#if ENABLE_PHYSICAL_BUTTONS
    for (int i = 0; i < BTN_COUNT; i++) {
        bool rawPressed = (safeDigitalRead(s_btnPins[i]) == LOW);
        if (rawPressed != s_lastRaw[i]) {
            s_lastRaw[i] = rawPressed;
            s_lastChangeMs[i] = now;
        } else if ((now - s_lastChangeMs[i]) >= 30) {
            // Raw state has remained stable for >= 30ms: latch state change
            if (rawPressed != s_stableState[i]) {
                s_stableState[i] = rawPressed;
                if (s_stableState[i]) {
                    justPressed[i] = true;
                } else {
                    justReleased[i] = true;
                }
            }
        }
    }
#endif

    // One-shot edge-triggered buttons: OK (Start/Stop), LEFT (Back), RIGHT (Select/Enter)
    // In industrial systems, these MUST NEVER auto-repeat when held.
    bool btnOk    = justPressed[BTN_IDX_OK];
    bool btnLeft  = justPressed[BTN_IDX_LEFT];
    bool btnRight = justPressed[BTN_IDX_RIGHT];

    bool rawUp = false;
    bool rawDown = false;
#if ENABLE_PHYSICAL_BUTTONS
    rawUp = (safeDigitalRead(PIN_BTN_UP) == LOW);
    rawDown = (safeDigitalRead(PIN_BTN_DOWN) == LOW);
#endif

    // Typematic Repeat Buttons: UP and DOWN
    // 1. Initial Press (Single Click): fires immediately on justPressed
    // 2. Initial Hold Delay: 400ms before auto-repeating
    // 3. Multi-Stage Accelerated Repeat:
    //    - 400ms .. 1400ms: 120ms interval (smooth single-step navigation)
    //    - 1400ms .. 3000ms: 60ms interval (fast tuning)
    //    - > 3000ms: 35ms interval (high-speed rapid scroll)
    bool btnUp = false;
    if (justPressed[BTN_IDX_UP]) {
        btnUp = true;
        s_holdStartTime[0] = now;
        s_nextRepeatMs[0]  = now + 400; // 400ms initial hold delay
        s_upHoldCount      = 0;
    } else if (s_stableState[BTN_IDX_UP] && rawUp) {
        if (now >= s_nextRepeatMs[0]) {
            btnUp = true;
            s_upHoldCount++;
            uint32_t heldMs = now - s_holdStartTime[0];
            uint32_t interval = 120;
            if (heldMs > 3000) {
                interval = 35;
            } else if (heldMs > 1400) {
                interval = 60;
            }
            s_nextRepeatMs[0] = now + interval;
        }
    }
    if (justReleased[BTN_IDX_UP] || !s_stableState[BTN_IDX_UP] || !rawUp) {
        s_upHoldCount = 0;
        s_holdStartTime[0] = 0;
    }

    bool btnDown = false;
    if (justPressed[BTN_IDX_DOWN]) {
        btnDown = true;
        s_holdStartTime[1] = now;
        s_nextRepeatMs[1]  = now + 400; // 400ms initial hold delay
        s_downHoldCount    = 0;
    } else if (s_stableState[BTN_IDX_DOWN] && rawDown) {
        if (now >= s_nextRepeatMs[1]) {
            btnDown = true;
            s_downHoldCount++;
            uint32_t heldMs = now - s_holdStartTime[1];
            uint32_t interval = 120;
            if (heldMs > 3000) {
                interval = 35;
            } else if (heldMs > 1400) {
                interval = 60;
            }
            s_nextRepeatMs[1] = now + interval;
        }
    }
    if (justReleased[BTN_IDX_DOWN] || !s_stableState[BTN_IDX_DOWN] || !rawDown) {
        s_downHoldCount = 0;
        s_holdStartTime[1] = 0;
    }

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

#if INPUT_USE_SERIAL
    // Serial-mode keys (bench testing / virtual display):
    while (Serial && Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') continue;
        switch (c) {
            case '1': btnUp = true; break;
            case '2': btnDown = true; break;
            case '3': btnLeft = true; break;
            case '4': btnRight = true; break;
            case '5': btnOk = true; break;
            case 'e':
            case 'E':
                sysStatus.emergency_stop_active = true;
                safeDigitalWrite(PIN_SSR_1, LOW);
                safeDigitalWrite(PIN_SSR_2, LOW);
                safeDigitalWrite(PIN_PNEUMATIC, LOW);
                safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
                sysStatus.torque_motor_running = false;
                sysStatus.motor_up_running = false;
                sysStatus.motor_down_running = false;
                sysStatus.remaining_time_sec = 0;
                sysStatus.forceStartActive = false;
                sysStatus.forceStartPending = false;
                currentScreen = SCREEN_HOME;
                transitionToState(STATE_IDLE);
                break;
            default: break;
        }
    }
#endif

    // If Emergency Stop is active, lock system and disable all buttons
    if (sysStatus.emergency_stop_active) {
        return; // All buttons disabled; user must restart the system
    }

    // Mutual Button Isolation: Button 5 (Start/Stop) only operates on Home screen
    if (currentScreen != SCREEN_HOME) {
        // In settings: Start/Stop button is completely disabled so it never interferes with navigation/settings
        btnOk = false;
    }

    if (!btnUp && !btnDown && !btnLeft && !btnRight && !btnOk) return;

    // Button 5 (btnOk): Exclusively START / STOP machine cycle operation
    if (btnOk) {
        if (currentScreen != SCREEN_HOME) {
            // In settings: start/stop button will NOT work until we came out the settings
            return;
        }

        if (sysStatus.forceStartPending) {
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
            sysStatus.forceStartActive = true; // Bypasses temp wait at down limit
            currentScreen = SCREEN_HOME;
            transitionToState(STATE_SAFETY_CHECK);
#if ENABLE_SERIAL_TFT
            vd_popup("Force-start confirmed. Starting process...");
#else
            Serial.println("[START] Force-start confirmed. Starting process...");
#endif
            return;
        }

        if (sysStatus.currentState == STATE_IDLE || sysStatus.currentState == STATE_READY) {
            bool allowStart = true;
            if (sysStatus.start_mode_auto) {
                // In AUTO mode: wait for temperatures to reach setpoint within tolerance
                ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];
                float tol = fabsf(prec.temp_tolerance_c);
#if ENABLE_H1
                if (fabsf(sysStatus.h1_actual_c - prec.h1_setpoint_c) > tol) {
                    allowStart = false;
                }
#endif
#if ENABLE_H2
                if (fabsf(sysStatus.h2_actual_c - prec.h2_setpoint_c) > tol) {
                    allowStart = false;
                }
#endif
            }
            // In MANUAL mode: allowStart remains true (starts immediately without waiting for setpoint)

            if (allowStart) {
                sysStatus.forceStartActive = false;
                currentScreen = SCREEN_HOME;
                transitionToState(STATE_SAFETY_CHECK);
            } else {
                // In AUTO mode with temp outside tolerance: offer force-start confirmation
                currentScreen = SCREEN_HOME;
                sysStatus.forceStartPending = true;
                sysStatus.forceStartUntilMs = millis() + 8000; // 8s window to confirm
#if ENABLE_SERIAL_TFT
                vd_popup("Setpoint not reached. Press [START/STOP] to Force Start or [<-] to Cancel (8s)");
#else
                Serial.println("[START] Setpoint not reached. Press [START/STOP] to Force Start or [<-] to Cancel (8s)");
#endif
            }
        } else if (sysStatus.currentState == STATE_ALARM_FAULT) {
            sysStatus.forceStartActive = false;
            transitionToState(STATE_IDLE);
        } else {
            // Machine cycle is actively running: STOP cycle immediately
            safeDigitalWrite(PIN_SSR_1, LOW);
            safeDigitalWrite(PIN_SSR_2, LOW);
            safeDigitalWrite(PIN_PNEUMATIC, LOW);
            safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
            sysStatus.torque_motor_running = false;
            sysStatus.motor_up_running = false;
            sysStatus.motor_down_running = false;
            sysStatus.remaining_time_sec = 0;
            sysStatus.forceStartActive = false;
            transitionToState(STATE_IDLE);
#if ENABLE_SERIAL_TFT
            vd_popup("Process STOPPED by user.");
#else
            Serial.println("[STOP] Process STOPPED by user.");
#endif
        }
        return;
    }

    // Handle forceStartPending cancel via LEFT button
    if (sysStatus.forceStartPending) {
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
        return;
    }

    switch (currentScreen) {
        case SCREEN_HOME: {
            bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                     sysStatus.currentState != STATE_READY && 
                                     sysStatus.currentState != STATE_ALARM_FAULT);
            if (isProcessRunning) {
                // Process started: settings will not work until process finish or stop
                break;
            }
            if (btnRight) {
                currentScreen = SCREEN_SETTINGS_MENU;
                settingsMenuIdx = 0;
            } else if (btnUp) {
                if (sysStatus.active_program_idx > 0) {
                    sysStatus.active_program_idx--;
                    saveActiveProgramToNVS(sysStatus.active_program_idx);
                }
            } else if (btnDown) {
                if (sysStatus.active_program_idx < 9) {
                    sysStatus.active_program_idx++;
                    saveActiveProgramToNVS(sysStatus.active_program_idx);
                }
            }
            break;
        }

        case SCREEN_SETTINGS_MENU:
            if (btnUp) {
                if (settingsMenuIdx > 0) settingsMenuIdx--;
            } else if (btnDown) {
                if (settingsMenuIdx < 6) settingsMenuIdx++;
            } else if (btnRight) {
                if (settingsMenuIdx == 0) {
                    // 1. PROGRAM RECIPES (1-10)
                    currentScreen = SCREEN_RECIPES_LIST;
                } else if (settingsMenuIdx == 1) {
                    // 2. START MODE: Toggle Auto / Manual
                    sysStatus.start_mode_auto = !sysStatus.start_mode_auto;
                    saveStartModeToNVS(sysStatus.start_mode_auto);
                } else if (settingsMenuIdx == 2) {
                    // 3. RELAY TYPE: Toggle SSR / Normal
                    g_relayType = (g_relayType == RELAY_TYPE_SSR) ? RELAY_TYPE_NORMAL : RELAY_TYPE_SSR;
                    saveRelayTypeToNVS(g_relayType);
                } else if (settingsMenuIdx == 3) {
                    // 4. PID TUNING
                    currentScreen = SCREEN_PID_TUNING;
                    pidEdit_field = 0;
                } else if (settingsMenuIdx == 4) {
                    // 5. DATE & TIME (RTC)
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
                } else if (settingsMenuIdx == 5) {
                    // 6. RESET TO DEFAULT: Open PIN screen
                    memset(s_pinDigits, 0, sizeof(s_pinDigits));
                    s_pinFocus = 0;
                    s_pinMsgUntil = 0;
                    s_pinSuccess = false;
                    currentScreen = SCREEN_FACTORY_RESET_PIN;
                } else if (settingsMenuIdx == 6) {
                    // 7. EXIT TO HOME
                    currentScreen = SCREEN_HOME;
                }
            } else if (btnLeft) {
                currentScreen = SCREEN_HOME;
            }
            break;

        case SCREEN_FACTORY_RESET_PIN:
            if (btnUp) {
                if (s_pinFocus < 5) {
                    s_pinDigits[s_pinFocus] = (s_pinDigits[s_pinFocus] + 1) % 10;
                } else {
                    s_pinFocus = 0;
                }
            } else if (btnDown) {
                if (s_pinFocus < 5) {
                    s_pinDigits[s_pinFocus] = (s_pinDigits[s_pinFocus] == 0) ? 9 : (s_pinDigits[s_pinFocus] - 1);
                } else {
                    s_pinFocus = 5;
                }
            } else if (btnRight) {
                if (s_pinFocus < 4) {
                    s_pinFocus++;
                } else if (s_pinFocus == 4) {
                    s_pinFocus = 5; // Move to [CONFIRM RESET]
                } else if (s_pinFocus == 5) {
                    // Check PIN: default is 12345
                    if (s_pinDigits[0] == 1 && s_pinDigits[1] == 2 && s_pinDigits[2] == 3 && s_pinDigits[3] == 4 && s_pinDigits[4] == 5) {
                        resetAllToFactoryDefaults();
                        s_pinSuccess = true;
                        s_pinMsgUntil = millis() + 2000;
                        currentScreen = SCREEN_HOME;
                    } else {
                        s_pinSuccess = false;
                        s_pinMsgUntil = millis() + 2500;
                        memset(s_pinDigits, 0, sizeof(s_pinDigits));
                        s_pinFocus = 0;
                    }
                } else if (s_pinFocus == 6) {
                    currentScreen = SCREEN_SETTINGS_MENU;
                }
            } else if (btnLeft) {
                if (s_pinFocus > 0 && s_pinFocus < 5) {
                    s_pinFocus--;
                } else if (s_pinFocus == 0) {
                    currentScreen = SCREEN_SETTINGS_MENU;
                } else if (s_pinFocus == 5) {
                    s_pinFocus = 4;
                } else if (s_pinFocus == 6) {
                    s_pinFocus = 5;
                }
            }
            break;

        case SCREEN_RECIPES_LIST:
            if (btnUp) {
                if (sysStatus.active_program_idx > 0) {
                    sysStatus.active_program_idx--;
                }
            } else if (btnDown) {
                if (sysStatus.active_program_idx < 9) { // 10 programs (Program 01..10)
                    sysStatus.active_program_idx++;
                }
            } else if (btnRight) {
                saveActiveProgramToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_RECIPE_EDIT;
                selectedEditField = 0;
                s_inValueEditMode = false;
            } else if (btnLeft) {
                saveActiveProgramToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_SETTINGS_MENU;
            }
            break;

        case SCREEN_RECIPE_EDIT: {
            ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];
            if (!s_inValueEditMode) {
                // Navigation Mode: UP/DOWN moves highlight, RIGHT enters Value Edit Mode, LEFT saves & returns
                if (btnUp) {
                    if (selectedEditField > 0) selectedEditField--;
                } else if (btnDown) {
                    if (selectedEditField < 7) selectedEditField++;
                } else if (btnRight) {
                    if (selectedEditField == 0) {
                        // Open Program Name Editor
                        memset(s_nameEditBuf, ' ', sizeof(s_nameEditBuf));
                        s_nameEditBuf[12] = '\0';
                        strncpy(s_nameEditBuf, rec.name, 12);
                        for (int k = strlen(s_nameEditBuf); k < 12; k++) s_nameEditBuf[k] = ' ';
                        s_nameSlotIdx = 0;
                        s_nameFocus = 0;
                        currentScreen = SCREEN_NAME_EDIT;
                    } else {
                        s_inValueEditMode = true; // Enter Value Edit Mode
                    }
                } else if (btnLeft) {
                    saveRecipeToNVS(sysStatus.active_program_idx);
                    currentScreen = SCREEN_RECIPES_LIST;
                }
            } else {
                // Value Edit Mode:
                // Field 1: H1 Target Temp
                // Field 2: H2 Target Temp
                // Field 3: Process Time
                // Field 4: Temp Tolerance
                // Field 5: H1 Offset %
                // Field 6: H2 Offset %
                // Field 7: Torque Unit
                float h_step = (s_upHoldCount > 12) ? 2.0f : ((s_upHoldCount > 5) ? 1.0f : 0.5f);
                float h_down_step = (s_downHoldCount > 12) ? 2.0f : ((s_downHoldCount > 5) ? 1.0f : 0.5f);

                if (btnUp) {
                    if (selectedEditField == 1) {
                        rec.h1_setpoint_c += h_step;
                        if (rec.h1_setpoint_c > MAX_SETPOINT_TEMP_C) rec.h1_setpoint_c = MAX_SETPOINT_TEMP_C;
                    } else if (selectedEditField == 2) {
                        rec.h2_setpoint_c += h_step;
                        if (rec.h2_setpoint_c > MAX_SETPOINT_TEMP_C) rec.h2_setpoint_c = MAX_SETPOINT_TEMP_C;
                    } else if (selectedEditField == 3) {
                        uint32_t inc = 1;
                        if (s_upHoldCount > 15) inc = 10;
                        else if (s_upHoldCount > 5) inc = 5;
                        rec.process_time_sec += inc;
                        if (rec.process_time_sec > 9999) rec.process_time_sec = 9999;
                    } else if (selectedEditField == 4) {
                        if (rec.temp_tolerance_c < MAX_TEMP_TOLERANCE_C) rec.temp_tolerance_c += 0.5f;
                    } else if (selectedEditField == 5) {
                        if (rec.h1_temp_offset_pct < 20.0f) rec.h1_temp_offset_pct += 0.5f;
                    } else if (selectedEditField == 6) {
                        if (rec.h2_temp_offset_pct < 20.0f) rec.h2_temp_offset_pct += 0.5f;
                    } else if (selectedEditField == 7) {
                        rec.torque_unit = (rec.torque_unit + 1) % 3;
                        g_torqueUnit = (TorqueUnit_t)rec.torque_unit;
                    }
                } else if (btnDown) {
                    if (selectedEditField == 1) {
                        rec.h1_setpoint_c -= h_down_step;
                        if (rec.h1_setpoint_c < MIN_SETPOINT_TEMP_C) rec.h1_setpoint_c = MIN_SETPOINT_TEMP_C;
                    } else if (selectedEditField == 2) {
                        rec.h2_setpoint_c -= h_down_step;
                        if (rec.h2_setpoint_c < MIN_SETPOINT_TEMP_C) rec.h2_setpoint_c = MIN_SETPOINT_TEMP_C;
                    } else if (selectedEditField == 3) {
                        uint32_t dec = 1;
                        if (s_downHoldCount > 15) dec = 10;
                        else if (s_downHoldCount > 5) dec = 5;
                        if (rec.process_time_sec >= dec + 1) rec.process_time_sec -= dec;
                        else rec.process_time_sec = 1;
                    } else if (selectedEditField == 4) {
                        if (rec.temp_tolerance_c > MIN_TEMP_TOLERANCE_C) rec.temp_tolerance_c -= 0.5f;
                    } else if (selectedEditField == 5) {
                        if (rec.h1_temp_offset_pct > -20.0f) rec.h1_temp_offset_pct -= 0.5f;
                    } else if (selectedEditField == 6) {
                        if (rec.h2_temp_offset_pct > -20.0f) rec.h2_temp_offset_pct -= 0.5f;
                    } else if (selectedEditField == 7) {
                        rec.torque_unit = (rec.torque_unit == 0) ? 2 : (rec.torque_unit - 1);
                        g_torqueUnit = (TorqueUnit_t)rec.torque_unit;
                    }
                } else if (btnRight || btnLeft) {
                    saveRecipeToNVS(sysStatus.active_program_idx);
                    s_inValueEditMode = false; // Exit edit mode
                }
            }
            break;
        }

        case SCREEN_NAME_EDIT: {
            static const char s_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            static const size_t s_charsetLen = sizeof(s_charset) - 1;

            if (s_nameFocus == 0) {
                // Character slot editing
                char curChar = s_nameEditBuf[s_nameSlotIdx];
                const char* p = strchr(s_charset, curChar);
                int charIdx = (p != NULL) ? (int)(p - s_charset) : 0;

                if (btnUp) {
                    charIdx = (charIdx + 1) % s_charsetLen;
                    s_nameEditBuf[s_nameSlotIdx] = s_charset[charIdx];
                } else if (btnDown) {
                    charIdx = (charIdx == 0) ? (s_charsetLen - 1) : (charIdx - 1);
                    s_nameEditBuf[s_nameSlotIdx] = s_charset[charIdx];
                } else if (btnRight) {
                    if (s_nameSlotIdx < 11) {
                        s_nameSlotIdx++;
                    } else {
                        s_nameFocus = 1; // Move to [SAVE]
                    }
                } else if (btnLeft) {
                    if (s_nameSlotIdx > 0) {
                        s_nameSlotIdx--;
                    } else {
                        s_nameFocus = 2; // Move to [CANCEL]
                    }
                }
            } else if (s_nameFocus == 1) { // [SAVE]
                if (btnUp) {
                    s_nameFocus = 0;
                } else if (btnDown) {
                    s_nameFocus = 2; // [CANCEL]
                } else if (btnLeft) {
                    s_nameFocus = 0;
                    s_nameSlotIdx = 11;
                } else if (btnRight) {
                    // [SAVE] Action execution:
                    int lastNonSpace = 11;
                    while (lastNonSpace >= 0 && s_nameEditBuf[lastNonSpace] == ' ') lastNonSpace--;
                    if (lastNonSpace < 0) {
                        snprintf(recipes[sysStatus.active_program_idx].name, sizeof(recipes[0].name), "Program %02d", sysStatus.active_program_idx + 1);
                    } else {
                        s_nameEditBuf[lastNonSpace + 1] = '\0';
                        strncpy(recipes[sysStatus.active_program_idx].name, s_nameEditBuf, sizeof(recipes[0].name) - 1);
                        recipes[sysStatus.active_program_idx].name[sizeof(recipes[0].name) - 1] = '\0';
                    }
                    saveRecipeToNVS(sysStatus.active_program_idx);
                    currentScreen = SCREEN_RECIPE_EDIT;
                }
            } else if (s_nameFocus == 2) { // [CANCEL]
                if (btnUp) {
                    s_nameFocus = 1; // [SAVE]
                } else if (btnDown) {
                    s_nameFocus = 3; // [CLEAR]
                } else if (btnLeft) {
                    s_nameFocus = 1; // [SAVE]
                } else if (btnRight) {
                    // [CANCEL] Action execution: return without saving
                    currentScreen = SCREEN_RECIPE_EDIT;
                }
            } else if (s_nameFocus == 3) { // [CLEAR]
                if (btnUp) {
                    s_nameFocus = 2; // [CANCEL]
                } else if (btnDown) {
                    s_nameFocus = 1; // [SAVE]
                } else if (btnLeft) {
                    s_nameFocus = 2; // [CANCEL]
                } else if (btnRight) {
                    // [CLEAR] Action execution:
                    memset(s_nameEditBuf, ' ', 12);
                    s_nameEditBuf[12] = '\0';
                    s_nameSlotIdx = 0;
                    s_nameFocus = 0;
                }
            }
            break;
        }

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
            } else if (btnLeft) {
                uint32_t total = ((uint32_t)timerEdit_h * 3600) + ((uint32_t)timerEdit_m * 60) + (uint32_t)timerEdit_s;
                recipes[sysStatus.active_program_idx].process_time_sec = total;
                saveRecipeToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_RECIPE_EDIT;
            }
            break;

        case SCREEN_TEMP_MANIP:
            if (btnUp) {
                if (tempManipField == 0) {
                    if (g_h1_temp_manip_pct < 20.0f) g_h1_temp_manip_pct += 0.5f;
                } else {
                    if (g_h2_temp_manip_pct < 20.0f) g_h2_temp_manip_pct += 0.5f;
                }
            } else if (btnDown) {
                if (tempManipField == 0) {
                    if (g_h1_temp_manip_pct > -20.0f) g_h1_temp_manip_pct -= 0.5f;
                } else {
                    if (g_h2_temp_manip_pct > -20.0f) g_h2_temp_manip_pct -= 0.5f;
                }
            } else if (btnRight) {
                tempManipField = (tempManipField == 0) ? 1 : 0;
            } else if (btnLeft) {
                saveTempManipToNVS(g_h1_temp_manip_pct, g_h2_temp_manip_pct);
                currentScreen = SCREEN_SETTINGS_MENU;
            }
            break;

        case SCREEN_PID_TUNING: {
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
            } else if (btnLeft) {
                saveRecipeToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_SETTINGS_MENU;
            }
            break;
        }

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
            } else if (btnLeft) {
                if (setRTCTime(rtcEdit_year, rtcEdit_month, rtcEdit_day, rtcEdit_hour, rtcEdit_minute, rtcEdit_second)) {
                    snprintf(rtcConfirmMsg, sizeof(rtcConfirmMsg), "RTC Set: %04u-%02u-%02u %02u:%02u:%02u", rtcEdit_year, rtcEdit_month, rtcEdit_day, rtcEdit_hour, rtcEdit_minute, rtcEdit_second);
                    startRtcBlink(true);
                } else {
                    snprintf(rtcConfirmMsg, sizeof(rtcConfirmMsg), "RTC Set Failed: Invalid Date/Time");
                    startRtcBlink(false);
                }
                rtcConfirmUntil = millis() + 3000; // show popup for 3s
                currentScreen = SCREEN_SETTINGS_MENU;
            }
            break;
    }
}

void drawHomeScreen(bool fullRedraw) {
    bool hasPopup = (rtcConfirmUntil && millis() < rtcConfirmUntil) ||
                    (limitSwitchWarningUntil && millis() < limitSwitchWarningUntil) ||
                    (sysStatus.forceStartPending) ||
                    (sysStatus.emergency_stop_active);
    if (hasPopup) {
        return; // Suppress background cards from overwriting any active popup
    }

    // 1. Static Layout (drawn once per screen change)
    if (fullRedraw) {
        // 4 Modern Information Cards (Outlines & Headers)
        // Card 1: Heater 1 (Top-Left: x=6, y=26, w=150, h=84)
        tft.drawRoundRect(6, 26, 150, 84, 4, 0x4A69);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("HEATER 1", 14, 30);
#if !ENABLE_H1
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("DISABLED", 14, 52);
#endif

        // Card 2: Heater 2 (Top-Right: x=164, y=26, w=150, h=84)
        tft.drawRoundRect(164, 26, 150, 84, 4, 0x4A69);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("HEATER 2", 172, 30);
#if !ENABLE_H2
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("DISABLED", 172, 52);
#endif

        // Card 3: Torque (Bottom-Left: x=6, y=116, w=150, h=84)
        tft.drawRoundRect(6, 116, 150, 84, 4, 0x4A69);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("TORQUE", 14, 120);

        // Card 4: Process Timer (Bottom-Right: x=164, y=116, w=150, h=84)
        tft.drawRoundRect(164, 116, 150, 84, 4, 0x4A69);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("TIMER", 172, 120);

        // Footer Navigation Bar (initial background)
        tft.fillRect(0, 206, 320, 30, 0x0841);
        tft.drawFastHLine(0, 206, 320, TFT_DARKCYAN);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // 2. Top Status & Recipe Bar (y = 2..22, h = 20)
    static ProcessState_t last_state = (ProcessState_t)0xFF;
    static bool last_boot_ok = true;
    static bool last_start_mode = false;
    static int last_prog_idx = -1;

    bool state_changed = (sysStatus.currentState != last_state || sysStatus.boot_ok != last_boot_ok || 
                          sysStatus.start_mode_auto != last_start_mode || sysStatus.active_program_idx != last_prog_idx);

    if (fullRedraw || state_changed) {
        uint16_t statusBorder = TFT_DARKCYAN;
        uint16_t statusTextColor = TFT_WHITE;
        const char *statusLine = "READY";

        switch (sysStatus.currentState) {
            case STATE_IDLE:
            case STATE_READY:
                statusLine = sysStatus.start_mode_auto ? "READY (AUTO)" : "READY (MANUAL)";
                statusBorder = 0x03E0; // Dark Green
                statusTextColor = TFT_GREEN;
                break;
            case STATE_SAFETY_CHECK:
                statusLine = "SAFETY CHECKS...";
                statusBorder = TFT_YELLOW;
                statusTextColor = TFT_YELLOW;
                break;
            case STATE_MOVE_DOWN:
                statusLine = "PNEUMATIC EXTEND";
                statusBorder = TFT_CYAN;
                statusTextColor = TFT_CYAN;
                break;
            case STATE_DOWN_LIMIT:
                statusLine = "AT DOWN LIMIT";
                statusBorder = TFT_CYAN;
                statusTextColor = TFT_CYAN;
                break;
            case STATE_HEAT_TO_SETPOINT:
                statusLine = "HEATING...";
                statusBorder = TFT_ORANGE;
                statusTextColor = TFT_ORANGE;
                break;
            case STATE_TEMPERATURE_READY:
                statusLine = "TEMP READY";
                statusBorder = TFT_GREEN;
                statusTextColor = TFT_GREEN;
                break;
            case STATE_PROCESS_TIMER:
                statusLine = "TORQUE MOTOR RUN";
                statusBorder = TFT_GREEN;
                statusTextColor = TFT_GREEN;
                break;
            case STATE_TIMER_COMPLETE:
                statusLine = "TIMER COMPLETE";
                statusBorder = TFT_CYAN;
                statusTextColor = TFT_CYAN;
                break;
            case STATE_MOVE_UP:
                statusLine = "RETRACTING...";
                statusBorder = TFT_CYAN;
                statusTextColor = TFT_CYAN;
                break;
            case STATE_HOME_LIMIT:
            case STATE_SAVE_RECORD:
            case STATE_PROCESS_COMPLETE:
                statusLine = "COMPLETE";
                statusBorder = TFT_GREEN;
                statusTextColor = TFT_GREEN;
                break;
            case STATE_ALARM_FAULT:
                statusLine = sysStatus.alarm_msg[0] ? sysStatus.alarm_msg : "SAFETY TRIP";
                statusBorder = TFT_RED;
                statusTextColor = TFT_RED;
                break;
            default:
                statusLine = "SYSTEM IDLE";
                statusBorder = TFT_LIGHTGREY;
                statusTextColor = TFT_WHITE;
                break;
        }

        if (!sysStatus.boot_ok) {
            statusLine = sysStatus.boot_msg[0] ? sysStatus.boot_msg : "BOOT FAILED";
            statusBorder = TFT_RED;
            statusTextColor = TFT_RED;
        }

        tft.drawRoundRect(6, 2, 308, 20, 3, statusBorder);
        tft.fillRect(7, 3, 306, 18, TFT_BLACK);

        tft.setTextFont(2);
        // Left: Active Program Name
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        char pgmBuf[24];
        snprintf(pgmBuf, sizeof(pgmBuf), "%s", recipes[sysStatus.active_program_idx].name);
        tft.drawString(pgmBuf, 12, 4);
        int16_t nameW = tft.textWidth(pgmBuf, 2);
        int16_t divX = 12 + nameW + 6;

        // Divider
        tft.setTextColor(TFT_DARKCYAN, TFT_BLACK);
        tft.drawString("|", divX, 4);

        // Right: Status
        tft.setTextColor(statusTextColor, TFT_BLACK);
        tft.drawString(statusLine, divX + 10, 4);

        tft.setFreeFont(FONT_FREE_BOLD_9);

        last_state = sysStatus.currentState;
        last_boot_ok = sysStatus.boot_ok;
        last_start_mode = sysStatus.start_mode_auto;
        last_prog_idx = sysStatus.active_program_idx;
    }

    // 3. Card 1: Heater 1 (y = 26..110, h = 84)
#if ENABLE_H1
    static int last_h1_act_tenth = -99999;
    int cur_h1_act_tenth = (int)roundf(sysStatus.h1_actual_c * 10.0f);
    if (fullRedraw || cur_h1_act_tenth != last_h1_act_tenth) {
        tft.fillRect(10, 48, 140, 34, TFT_BLACK);
        if (isnan(sysStatus.h1_actual_c) || sysStatus.h1_actual_c < -45.0f) {
            tft.setFreeFont(FONT_FREE_BOLD_18);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("FAULT", 14, 52);
        } else {
            char valBuf[16];
            snprintf(valBuf, sizeof(valBuf), "%.1f", sysStatus.h1_actual_c);
            tft.setFreeFont(FONT_FREE_BOLD_18);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(valBuf, 14, 52);
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.drawString("C", 105, 54);
        }
        last_h1_act_tenth = cur_h1_act_tenth;
    }

    static int last_h1_set_tenth = -99999;
    int cur_h1_set_tenth = (int)roundf(recipes[sysStatus.active_program_idx].h1_setpoint_c * 10.0f);
    if (fullRedraw || cur_h1_set_tenth != last_h1_set_tenth) {
        char setBuf[32];
        snprintf(setBuf, sizeof(setBuf), "SET: %.1f C", recipes[sysStatus.active_program_idx].h1_setpoint_c);
        tft.fillRect(10, 86, 140, 18, TFT_BLACK);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString(setBuf, 14, 88);
        last_h1_set_tenth = cur_h1_set_tenth;
    }
#endif

    // 4. Card 2: Heater 2 (y = 26..110, h = 84)
#if ENABLE_H2
    static int last_h2_act_tenth = -99999;
    int cur_h2_act_tenth = (int)roundf(sysStatus.h2_actual_c * 10.0f);
    if (fullRedraw || cur_h2_act_tenth != last_h2_act_tenth) {
        tft.fillRect(168, 48, 140, 34, TFT_BLACK);
        if (isnan(sysStatus.h2_actual_c) || sysStatus.h2_actual_c < -45.0f) {
            tft.setFreeFont(FONT_FREE_BOLD_18);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("FAULT", 172, 52);
        } else {
            char valBuf[16];
            snprintf(valBuf, sizeof(valBuf), "%.1f", sysStatus.h2_actual_c);
            tft.setFreeFont(FONT_FREE_BOLD_18);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(valBuf, 172, 52);
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.drawString("C", 265, 54);
        }
        last_h2_act_tenth = cur_h2_act_tenth;
    }

    static int last_h2_set_tenth = -99999;
    int cur_h2_set_tenth = (int)roundf(recipes[sysStatus.active_program_idx].h2_setpoint_c * 10.0f);
    if (fullRedraw || cur_h2_set_tenth != last_h2_set_tenth) {
        char setBuf[32];
        snprintf(setBuf, sizeof(setBuf), "SET: %.1f C", recipes[sysStatus.active_program_idx].h2_setpoint_c);
        tft.fillRect(168, 86, 140, 18, TFT_BLACK);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString(setBuf, 172, 88);
        last_h2_set_tenth = cur_h2_set_tenth;
    }
#endif

    // 5. Card 3: Torque (y = 116..200, h = 84)
    static int last_torque_hundredth = -99999;
    static TorqueUnit_t last_tq_unit = (TorqueUnit_t)0xFF;
    TorqueUnit_t cur_unit = (TorqueUnit_t)recipes[sysStatus.active_program_idx].torque_unit;
    float conv_torque = sysStatus.current_torque_nm * getTorqueConversionFactor(cur_unit);
    int cur_torque_hundredth = (int)roundf(conv_torque * 100.0f);
    if (fullRedraw || cur_torque_hundredth != last_torque_hundredth || cur_unit != last_tq_unit) {
        tft.fillRect(10, 142, 140, 36, TFT_BLACK);
        char tqBuf[16];
        snprintf(tqBuf, sizeof(tqBuf), "%.2f", conv_torque);
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(tqBuf, 14, 148);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.drawString(getTorqueUnitName(cur_unit), 105, 150);
        last_torque_hundredth = cur_torque_hundredth;
        last_tq_unit = cur_unit;
    }

    // 6. Card 4: Process Timer (y = 116..200, h = 84)
    static uint32_t last_remaining = 0xFFFFFFFF;
    if (fullRedraw || sysStatus.remaining_time_sec != last_remaining) {
        tft.fillRect(168, 142, 140, 36, TFT_BLACK);
        tft.setFreeFont(FONT_FREE_BOLD_18);
        if (sysStatus.remaining_time_sec > 0) {
            uint32_t t = sysStatus.remaining_time_sec;
            uint32_t mm = t / 60;
            uint32_t ss = t % 60;
            char tb[16];
            snprintf(tb, sizeof(tb), "%02u:%02u", (unsigned)mm, (unsigned)ss);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.drawString(tb, 172, 148);
        } else {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("--:--", 172, 148);
        }
        last_remaining = sysStatus.remaining_time_sec;
    }

    // 8. Footer Navigation Bar Dynamic Update (Running vs Idle) using compact Font 2
    static int last_running_footer = -1;
    bool is_running = (sysStatus.currentState != STATE_IDLE && 
                       sysStatus.currentState != STATE_READY && 
                       sysStatus.currentState != STATE_ALARM_FAULT);
    if (fullRedraw || (int)is_running != last_running_footer) {
        tft.fillRect(0, 206, 320, 30, 0x0841);
        tft.drawFastHLine(0, 206, 320, TFT_DARKCYAN);
        tft.setTextFont(2);
        if (is_running) {
            tft.setTextColor(TFT_RED, 0x0841);
            tft.drawString("[STOP]: Stop Cycle", 10, 213);
            tft.setTextColor(0x7BEF, 0x0841); // Dimmed grey
            tft.drawString("[MENU]: Locked", 210, 213);
        } else {
            tft.setTextColor(TFT_WHITE, 0x0841);
            tft.drawString("[START]: Run", 10, 213);
            tft.drawString("[->]: Menu", 235, 213);
        }
        tft.setFreeFont(FONT_FREE_BOLD_9);
        last_running_footer = (int)is_running;
    }

    // 9. Bottom Accent Strip (y=236..240)
    static uint16_t last_banner = 0;
    uint16_t bannerColor = (!sysStatus.boot_ok || sysStatus.currentState == STATE_ALARM_FAULT) ? TFT_RED :
                           (sysStatus.currentState == STATE_PROCESS_TIMER ? TFT_CYAN :
                           (sysStatus.currentState == STATE_HEAT_TO_SETPOINT ? TFT_YELLOW : TFT_DARKGREEN));
    if (fullRedraw || bannerColor != last_banner) {
        tft.fillRect(0, 236, 320, 4, bannerColor);
        last_banner = bannerColor;
    }
}

void drawSettingsMenu(bool fullRedraw) {
    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader("CONFIG", TFT_YELLOW);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] Move", 10, 212);
        tft.drawString("[->] Select", 145, 212);
        tft.drawString("[<-] Back", 245, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    static uint8_t last_sel = 0xFF;
    static RelayType_t last_relay = (RelayType_t)0xFF;
    static bool last_auto = false;
    static int topIdx = 0;

    bool needsRedraw = fullRedraw || (settingsMenuIdx != last_sel) || (g_relayType != last_relay) || (sysStatus.start_mode_auto != last_auto);
    if (!needsRedraw) return;

    struct SettingItem_t {
        char name[20];
        char val[16];
    };
    SettingItem_t menuItems[7];
    strncpy(menuItems[0].name, "Recipes", sizeof(menuItems[0].name));
    strncpy(menuItems[0].val,  "Select", sizeof(menuItems[0].val));

    strncpy(menuItems[1].name, "Start Mode", sizeof(menuItems[1].name));
    strncpy(menuItems[1].val,  sysStatus.start_mode_auto ? "Auto" : "Manual", sizeof(menuItems[1].val));

    strncpy(menuItems[2].name, "Relay Type", sizeof(menuItems[2].name));
    strncpy(menuItems[2].val,  g_relayType == RELAY_TYPE_SSR ? "SSR" : "Normal", sizeof(menuItems[2].val));

    strncpy(menuItems[3].name, "PID Tuning", sizeof(menuItems[3].name));
    strncpy(menuItems[3].val,  "Coeffs", sizeof(menuItems[3].val));

    strncpy(menuItems[4].name, "Date & Time", sizeof(menuItems[4].name));
    strncpy(menuItems[4].val,  "Adjust", sizeof(menuItems[4].val));

    strncpy(menuItems[5].name, "Reset Defaults", sizeof(menuItems[5].name));
    strncpy(menuItems[5].val,  "Default", sizeof(menuItems[5].val));

    strncpy(menuItems[6].name, "Exit to Home", sizeof(menuItems[6].name));
    strncpy(menuItems[6].val,  "Home", sizeof(menuItems[6].val));

    // Show 4 items per page in scroll window
    if (settingsMenuIdx < topIdx) topIdx = settingsMenuIdx;
    if (settingsMenuIdx > topIdx + 3) topIdx = settingsMenuIdx - 3;
    if (topIdx > 3) topIdx = 3;
    if (topIdx < 0) topIdx = 0;

    const int startY = 44;
    const int cardH = 36;
    const int gap = 4;

    tft.setFreeFont(FONT_FREE_BOLD_9);
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t itemIdx = topIdx + i;
        if (itemIdx >= 7) break;
        int curY = startY + i * (cardH + gap);
        bool isSel = (settingsMenuIdx == itemIdx);

        // Card frame matching recipe screens
        tft.drawRoundRect(10, curY, 300, cardH, 4, isSel ? TFT_GREEN : 0x4A69);
        tft.fillRect(11, curY + 1, 298, cardH - 2, isSel ? 0x10C2 : TFT_BLACK);

        tft.setTextColor(isSel ? TFT_GREEN : TFT_LIGHTGREY, isSel ? 0x10C2 : TFT_BLACK);
        if (isSel) {
            tft.drawString(">", 16, curY + 9);
        }
        tft.drawString(menuItems[itemIdx].name, 28, curY + 9);
        tft.drawString(":", 170, curY + 9);
        tft.setTextColor(isSel ? TFT_YELLOW : TFT_WHITE, isSel ? 0x10C2 : TFT_BLACK);
        tft.drawString(menuItems[itemIdx].val, 185, curY + 9);
    }

    last_sel = settingsMenuIdx;
    last_relay = g_relayType;
    last_auto = sysStatus.start_mode_auto;
}

void drawRecipesListScreen(bool fullRedraw) {
    static int last_active_idx = -1;
    static int last_scroll_offset = -1;

    const int totalPrograms = 10;
    const int pageSize = 4;

    // Adjust scroll offset to keep active_program_idx in view (4 items per page)
    static int s_scrollOffset = 0;
    if (sysStatus.active_program_idx < s_scrollOffset) {
        s_scrollOffset = sysStatus.active_program_idx;
    } else if (sysStatus.active_program_idx >= s_scrollOffset + pageSize) {
        s_scrollOffset = sysStatus.active_program_idx - pageSize + 1;
    }
    if (s_scrollOffset < 0) s_scrollOffset = 0;
    if (s_scrollOffset > totalPrograms - pageSize) s_scrollOffset = totalPrograms - pageSize;

    bool scrollChanged = (s_scrollOffset != last_scroll_offset);
    bool idxChanged = (sysStatus.active_program_idx != last_active_idx);

    if (!fullRedraw && !scrollChanged && !idxChanged) return;

    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%02d/%02d", sysStatus.active_program_idx + 1, totalPrograms);

    if (fullRedraw || scrollChanged) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader(countBuf, TFT_YELLOW);

        // Container Card (y = 43..199, h = 156)
        tft.drawRoundRect(6, 43, 308, 156, 4, 0x4A69);

        // Table Header
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("RECIPE", 14, 47);
        tft.drawString("H1", 155, 47);
        tft.drawString("H2", 210, 47);
        tft.drawString("TIME", 265, 47);
        tft.drawFastHLine(10, 68, 300, 0x3186);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] Scroll", 10, 212);
        tft.drawString("[->] Edit", 140, 212);
        tft.drawString("[<-] Back", 240, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    } else {
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_YELLOW, 0x0841);
        tft.setTextPadding(140);
        tft.drawString(countBuf, 170, 20);
        tft.setTextPadding(0);
    }

    const int startX = 14;
    const int h1X = 155;
    const int h2X = 210;
    const int timeX = 265;
    const int startY = 72;
    const int lineH = 31;

    tft.setFreeFont(FONT_FREE_BOLD_9);
    for (int i = 0; i < pageSize; i++) {
        uint8_t idx = s_scrollOffset + i;
        if (idx >= totalPrograms) break;
        bool isSelected = (sysStatus.active_program_idx == idx);
        int curY = startY + (i * lineH);

        if (isSelected) {
            tft.fillRect(10, curY, 300, lineH - 2, 0x18E3);
            tft.drawRoundRect(10, curY, 300, lineH - 2, 3, TFT_GREEN);
            tft.setTextColor(TFT_YELLOW, 0x18E3);
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "> %s", recipes[idx].name);
            tft.drawString(nameBuf, startX, curY + 6);
            char h1Buf[16], h2Buf[16], tBuf[16];
            snprintf(h1Buf, sizeof(h1Buf), "%.0fC", recipes[idx].h1_setpoint_c);
            snprintf(h2Buf, sizeof(h2Buf), "%.0fC", recipes[idx].h2_setpoint_c);
            snprintf(tBuf, sizeof(tBuf), "%u", (unsigned)recipes[idx].process_time_sec);
            tft.drawString(h1Buf, h1X, curY + 6);
            tft.drawString(h2Buf, h2X, curY + 6);
            tft.drawString(tBuf, timeX, curY + 6);
        } else {
            tft.fillRect(10, curY, 300, lineH - 2, TFT_BLACK);
            tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "  %s", recipes[idx].name);
            tft.drawString(nameBuf, startX, curY + 6);
            char h1Buf[16], h2Buf[16], tBuf[16];
            snprintf(h1Buf, sizeof(h1Buf), "%.0fC", recipes[idx].h1_setpoint_c);
            snprintf(h2Buf, sizeof(h2Buf), "%.0fC", recipes[idx].h2_setpoint_c);
            snprintf(tBuf, sizeof(tBuf), "%u", (unsigned)recipes[idx].process_time_sec);
            tft.drawString(h1Buf, h1X, curY + 6);
            tft.drawString(h2Buf, h2X, curY + 6);
            tft.drawString(tBuf, timeX, curY + 6);
        }
    }

    last_active_idx = sysStatus.active_program_idx;
    last_scroll_offset = s_scrollOffset;
}

void drawProgramEditScreen(bool fullRedraw) {
    static int last_prog = -1;
    static uint8_t last_edit_field = 0xFF;
    static bool last_edit_mode = false;
    static float last_h1 = NAN, last_h2 = NAN, last_tol = NAN, last_o1 = NAN, last_o2 = NAN;
    static uint32_t last_time = 0xFFFFFFFF;
    static uint8_t last_unit = 0xFF;
    static char last_name[16] = {0};

    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];

    bool changed = fullRedraw ||
                   (sysStatus.active_program_idx != last_prog) ||
                   (selectedEditField != last_edit_field) ||
                   (s_inValueEditMode != last_edit_mode) ||
                   (strcmp(rec.name, last_name) != 0) ||
                   (rec.h1_setpoint_c != last_h1) ||
                   (rec.h2_setpoint_c != last_h2) ||
                   (rec.process_time_sec != last_time) ||
                   (rec.temp_tolerance_c != last_tol) ||
                   (rec.h1_temp_offset_pct != last_o1) ||
                   (rec.h2_temp_offset_pct != last_o2) ||
                   (rec.torque_unit != last_unit);

    if (!changed) return;

    if (fullRedraw || sysStatus.active_program_idx != last_prog || s_inValueEditMode != last_edit_mode) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader(rec.name, TFT_YELLOW);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, 0x0841);
        if (s_inValueEditMode) {
            tft.drawString("[UP/DN] Change", 10, 212);
            tft.drawString("[<-/->] Save & Exit", 140, 212);
        } else {
            tft.drawString("[UP/DN] Move", 10, 212);
            tft.drawString("[->] Edit", 130, 212);
            tft.drawString("[<-] Back", 240, 212);
        }
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // 8 Parameters with 4-item scroll window
    // 0: Program Name
    // 1: H1 Target Temp
    // 2: H2 Target Temp
    // 3: Process Time
    // 4: Temp Tolerance
    // 5: H1 Offset %
    // 6: H2 Offset %
    // 7: Torque Unit
    int topIdx = 0;
    if (selectedEditField >= 4) {
        topIdx = selectedEditField - 3;
        if (topIdx > 4) topIdx = 4;
    }

    const char* paramLabels[8] = {
        "NAME:",
        "H1 TARGET TEMP:",
        "H2 TARGET TEMP:",
        "PROCESS TIME:",
        "TEMP TOLERANCE:",
        "H1 OFFSET %:",
        "H2 OFFSET %:",
        "TORQUE UNIT:"
    };

    char valBuffers[8][24];
    snprintf(valBuffers[0], sizeof(valBuffers[0]), "%s", rec.name);
    snprintf(valBuffers[1], sizeof(valBuffers[1]), "%.1f C", rec.h1_setpoint_c);
    snprintf(valBuffers[2], sizeof(valBuffers[2]), "%.1f C", rec.h2_setpoint_c);
    uint32_t tSec = TIMER_UNITS_TO_SECONDS(rec.process_time_sec);
    snprintf(valBuffers[3], sizeof(valBuffers[3]), "%u (%02u:%02u)", (unsigned)rec.process_time_sec, (unsigned)(tSec / 60), (unsigned)(tSec % 60));
    snprintf(valBuffers[4], sizeof(valBuffers[4]), "%+.1f C", rec.temp_tolerance_c);
    snprintf(valBuffers[5], sizeof(valBuffers[5]), "%+.1f %%", rec.h1_temp_offset_pct);
    snprintf(valBuffers[6], sizeof(valBuffers[6]), "%+.1f %%", rec.h2_temp_offset_pct);
    snprintf(valBuffers[7], sizeof(valBuffers[7]), "%s", getTorqueUnitName((TorqueUnit_t)rec.torque_unit));

    const int startY = 44;
    const int cardH = 36;
    const int gap = 4;

    tft.setFreeFont(FONT_FREE_BOLD_9);
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t itemIdx = topIdx + i;
        if (itemIdx >= 8) break;
        int curY = startY + i * (cardH + gap);
        bool isSel = (selectedEditField == itemIdx);

        uint16_t borderCol = isSel ? (s_inValueEditMode ? TFT_YELLOW : TFT_GREEN) : 0x4A69;
        uint16_t bgCol     = isSel ? (s_inValueEditMode ? 0x2100 : 0x10C2) : TFT_BLACK;
        uint16_t labelCol  = isSel ? (s_inValueEditMode ? TFT_YELLOW : TFT_GREEN) : TFT_CYAN;
        uint16_t valCol    = isSel ? TFT_WHITE : TFT_LIGHTGREY;

        tft.drawRoundRect(6, curY, 308, cardH, 4, borderCol);
        tft.fillRect(7, curY + 1, 306, cardH - 2, bgCol);

        tft.setTextColor(labelCol, bgCol);
        tft.drawString(paramLabels[itemIdx], 14, curY + 9);

        tft.setTextColor(valCol, bgCol);
        tft.drawString(valBuffers[itemIdx], 165, curY + 9);

        if (isSel) {
            if (s_inValueEditMode) {
                tft.setTextColor(TFT_YELLOW, bgCol);
                tft.drawString("[EDIT]", 255, curY + 9);
            } else {
                tft.setTextColor(TFT_GREEN, bgCol);
                tft.drawString(">", 295, curY + 9);
            }
        }
    }

    last_prog = sysStatus.active_program_idx;
    last_edit_field = selectedEditField;
    last_edit_mode = s_inValueEditMode;
    strncpy(last_name, rec.name, sizeof(last_name) - 1);
    last_h1 = rec.h1_setpoint_c;
    last_h2 = rec.h2_setpoint_c;
    last_time = rec.process_time_sec;
    last_tol = rec.temp_tolerance_c;
    last_o1 = rec.h1_temp_offset_pct;
    last_o2 = rec.h2_temp_offset_pct;
    last_unit = rec.torque_unit;
}

void drawFactoryResetPinScreen(bool fullRedraw) {
    static uint8_t last_digits[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static uint8_t last_focus = 0xFF;
    static uint32_t last_msg_until = 0;

    bool digitsChanged = false;
    for (int i = 0; i < 5; i++) {
        if (s_pinDigits[i] != last_digits[i]) { digitsChanged = true; break; }
    }
    bool changed = fullRedraw || digitsChanged || (s_pinFocus != last_focus) || (s_pinMsgUntil != last_msg_until);
    if (!changed) return;

    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader("PIN: 12345", TFT_YELLOW);

        // Warning / Instructions
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Enter PIN to restore all defaults:", 16, 45);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] Digit", 10, 212);
        tft.drawString("[->] Next/Do", 130, 212);
        tft.drawString("[<-] Back", 240, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // 5 Digit Boxes (centered: total width = 5 * 44 + 4 * 12 = 268px, left = 26)
    const int startX = 26;
    const int boxW = 44;
    const int boxH = 46;
    const int gap = 12;
    const int boxY = 66;

    for (int i = 0; i < 5; i++) {
        bool isFocus = (s_pinFocus == i);
        int curX = startX + i * (boxW + gap);

        tft.drawRoundRect(curX, boxY, boxW, boxH, 4, isFocus ? TFT_YELLOW : 0x4A69);
        tft.fillRect(curX + 1, boxY + 1, boxW - 2, boxH - 2, isFocus ? 0x2100 : TFT_BLACK);

        char dStr[2] = {(char)('0' + s_pinDigits[i]), '\0'};
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(isFocus ? TFT_YELLOW : TFT_WHITE, isFocus ? 0x2100 : TFT_BLACK);
        tft.drawString(dStr, curX + 13, boxY + 6);
    }

    // Message Banner (y=118..142)
    tft.fillRect(10, 118, 300, 24, TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    if (s_pinMsgUntil > millis()) {
        if (s_pinSuccess) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.drawString("RESET SUCCESSFUL! Defaults restored.", 14, 122);
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("INCORRECT PIN! Default is 12345.", 24, 122);
        }
    } else {
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("Resets recipes & configurations", 35, 122);
    }

    // Action Buttons: [CONFIRM RESET] and [CANCEL]
    bool isConfirm = (s_pinFocus == 5);
    bool isCancel = (s_pinFocus == 6);

    // Confirm Button (x=20, y=152, w=135, h=34)
    tft.drawRoundRect(20, 152, 135, 34, 4, isConfirm ? TFT_GREEN : 0x4A69);
    tft.fillRect(21, 153, 133, 32, isConfirm ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(isConfirm ? TFT_GREEN : TFT_WHITE, isConfirm ? 0x10C2 : TFT_BLACK);
    tft.drawString("CONFIRM", 48, 160);

    // Cancel Button (x=165, y=152, w=135, h=34)
    tft.drawRoundRect(165, 152, 135, 34, 4, isCancel ? TFT_RED : 0x4A69);
    tft.fillRect(166, 153, 133, 32, isCancel ? 0x3000 : TFT_BLACK);
    tft.setTextColor(isCancel ? TFT_RED : TFT_LIGHTGREY, isCancel ? 0x3000 : TFT_BLACK);
    tft.drawString("CANCEL", 198, 160);

    for (int i = 0; i < 5; i++) last_digits[i] = s_pinDigits[i];
    last_focus = s_pinFocus;
    last_msg_until = s_pinMsgUntil;
}

void drawProgramNameEditScreen(bool fullRedraw) {
    static char last_buf[16] = {0};
    static uint8_t last_slot = 0xFF;
    static uint8_t last_focus = 0xFF;

    bool bufChanged = (strcmp(s_nameEditBuf, last_buf) != 0);
    bool changed = fullRedraw || bufChanged || (s_nameSlotIdx != last_slot) || (s_nameFocus != last_focus);
    if (!changed) return;

    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        char pBuf[16];
        snprintf(pBuf, sizeof(pBuf), "P%02d", sysStatus.active_program_idx + 1);
        drawMobileHeader(pBuf, TFT_YELLOW);

        // Instruction
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Set custom name (max 12 chars):", 14, 45);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] Char", 10, 212);
        tft.drawString("[->] Next/Do", 130, 212);
        tft.drawString("[<-] Prev/Back", 215, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // 12 Character Slot Boxes (centered: total width = 12 * 22 + 11 * 3 = 297px, startX = 11)
    const int startX = 11;
    const int slotW = 22;
    const int slotH = 36;
    const int gap = 3;
    const int slotY = 80;

    // Clear arrow area
    tft.fillRect(startX, slotY - 16, 297, 14, TFT_BLACK);
    tft.fillRect(startX, slotY + slotH + 2, 297, 14, TFT_BLACK);

    for (int i = 0; i < 12; i++) {
        bool isSlot = (s_nameFocus == 0 && s_nameSlotIdx == i);
        int curX = startX + i * (slotW + gap);

        tft.drawRoundRect(curX, slotY, slotW, slotH, 3, isSlot ? TFT_YELLOW : 0x4A69);
        tft.fillRect(curX + 1, slotY + 1, slotW - 2, slotH - 2, isSlot ? 0x2100 : TFT_BLACK);

        // Render arrow indicators for active slot
        if (isSlot) {
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("^", curX + 6, slotY - 14);
            tft.drawString("v", curX + 6, slotY + slotH + 2);
        }

        // Character
        char cStr[2] = {s_nameEditBuf[i], '\0'};
        if (cStr[0] == '\0' || cStr[0] == ' ') {
            cStr[0] = ' ';
        }
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(isSlot ? TFT_YELLOW : TFT_WHITE, isSlot ? 0x2100 : TFT_BLACK);
        tft.drawString(cStr, curX + 6, slotY + 9);
    }

    // Action Buttons: [SAVE], [CANCEL], [CLEAR] (y=155..187)
    bool isSave = (s_nameFocus == 1);
    bool isCancel = (s_nameFocus == 2);
    bool isClear = (s_nameFocus == 3);

    // Save Button (x=15, w=85, h=32)
    tft.drawRoundRect(15, 155, 85, 32, 4, isSave ? TFT_GREEN : 0x4A69);
    tft.fillRect(16, 156, 83, 30, isSave ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(isSave ? TFT_GREEN : TFT_WHITE, isSave ? 0x10C2 : TFT_BLACK);
    tft.drawString("SAVE", 35, 162);

    // Cancel Button (x=115, w=85, h=32)
    tft.drawRoundRect(115, 155, 85, 32, 4, isCancel ? TFT_RED : 0x4A69);
    tft.fillRect(116, 156, 83, 30, isCancel ? 0x3000 : TFT_BLACK);
    tft.setTextColor(isCancel ? TFT_RED : TFT_LIGHTGREY, isCancel ? 0x3000 : TFT_BLACK);
    tft.drawString("CANCEL", 123, 162);

    // Clear Button (x=215, w=85, h=32)
    tft.drawRoundRect(215, 155, 85, 32, 4, isClear ? TFT_YELLOW : 0x4A69);
    tft.fillRect(216, 156, 83, 30, isClear ? 0x2100 : TFT_BLACK);
    tft.setTextColor(isClear ? TFT_YELLOW : TFT_LIGHTGREY, isClear ? 0x2100 : TFT_BLACK);
    tft.drawString("CLEAR", 230, 162);

    strncpy(last_buf, s_nameEditBuf, sizeof(last_buf) - 1);
    last_slot = s_nameSlotIdx;
    last_focus = s_nameFocus;
}

void drawTempManipScreen(bool fullRedraw) {
    static uint8_t last_f = 0xFF;
    static float last_h1_pct = -999.0f, last_h2_pct = -999.0f;
    bool changed = fullRedraw || (tempManipField != last_f) ||
                   (g_h1_temp_manip_pct != last_h1_pct) ||
                   (g_h2_temp_manip_pct != last_h2_pct);
    if (!changed) return;

    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader("+-20%", TFT_YELLOW);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] +/-", 10, 212);
        tft.drawString("[->] H1/H2", 125, 212);
        tft.drawString("[<-] Back", 225, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // Card 1: Heater 1 Offset % (y=44..118)
    bool f0_sel = (tempManipField == 0);
    tft.drawRoundRect(10, 44, 300, 74, 4, f0_sel ? TFT_GREEN : 0x4A69);
    tft.fillRect(11, 45, 298, 72, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(f0_sel ? TFT_GREEN : TFT_CYAN, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("HEATER 1 OFFSET %:", 20, 48);

    char h1Buf[16];
    snprintf(h1Buf, sizeof(h1Buf), "%+.1f %%", g_h1_temp_manip_pct);
    tft.setFreeFont(FONT_FREE_BOLD_18);
    tft.setTextColor(f0_sel ? TFT_YELLOW : TFT_WHITE, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString(h1Buf, 24, 68);

    float h1_set = recipes[sysStatus.active_program_idx].h1_setpoint_c;
    float h1_deg = (g_h1_temp_manip_pct / 100.0f) * h1_set;
    char h1Sub[48];
    snprintf(h1Sub, sizeof(h1Sub), "Offset: %+.1f C (at SET %.0f C)", h1_deg, h1_set);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(f0_sel ? TFT_GREEN : TFT_LIGHTGREY, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString(h1Sub, 24, 96);

    // Card 2: Heater 2 Offset % (y=124..198)
    bool f1_sel = (tempManipField == 1);
    tft.drawRoundRect(10, 124, 300, 74, 4, f1_sel ? TFT_GREEN : 0x4A69);
    tft.fillRect(11, 125, 298, 72, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(f1_sel ? TFT_GREEN : TFT_CYAN, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("HEATER 2 OFFSET %:", 20, 128);

    char h2Buf[16];
    snprintf(h2Buf, sizeof(h2Buf), "%+.1f %%", g_h2_temp_manip_pct);
    tft.setFreeFont(FONT_FREE_BOLD_18);
    tft.setTextColor(f1_sel ? TFT_YELLOW : TFT_WHITE, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString(h2Buf, 24, 148);

    float h2_set = recipes[sysStatus.active_program_idx].h2_setpoint_c;
    float h2_deg = (g_h2_temp_manip_pct / 100.0f) * h2_set;
    char h2Sub[48];
    snprintf(h2Sub, sizeof(h2Sub), "Offset: %+.1f C (at SET %.0f C)", h2_deg, h2_set);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(f1_sel ? TFT_GREEN : TFT_LIGHTGREY, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString(h2Sub, 24, 176);

    last_f = tempManipField;
    last_h1_pct = g_h1_temp_manip_pct;
    last_h2_pct = g_h2_temp_manip_pct;
}

void drawTimerEditor(bool fullRedraw) {
    static uint8_t last_f = 0xFF, last_h = 0xFF, last_m = 0xFF, last_s = 0xFF;
    bool changed = fullRedraw || (timerEdit_field != last_f) ||
                   (timerEdit_h != last_h) || (timerEdit_m != last_m) || (timerEdit_s != last_s);
    if (!changed) return;

    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader("HH:MM:SS", TFT_YELLOW);

        // Central Timer Container (y = 44..198, h = 154)
        tft.drawRoundRect(16, 44, 288, 154, 6, 0x4A69);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("SET DURATION", 28, 48);

        // Sub-labels above digits
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("HOURS", 38, 70);
        tft.drawString("MINS", 132, 70);
        tft.drawString("SECS", 224, 70);

        // Separator colons
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(":", 104, 98);
        tft.drawString(":", 196, 98);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] +/-", 10, 212);
        tft.drawString("[->] Next Digit", 115, 212);
        tft.drawString("[<-] Save", 225, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // Segment Box 0: Hours
    bool h_sel = (timerEdit_field == 0);
    tft.drawRoundRect(28, 90, 68, 42, 4, h_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(29, 91, 66, 40, h_sel ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_18);
    tft.setTextColor(h_sel ? TFT_YELLOW : TFT_WHITE, h_sel ? 0x10C2 : TFT_BLACK);
    char hBuf[8]; snprintf(hBuf, sizeof(hBuf), "%02u", (unsigned)timerEdit_h);
    tft.drawString(hBuf, 38, 98);

    // Segment Box 1: Minutes
    bool m_sel = (timerEdit_field == 1);
    tft.drawRoundRect(120, 90, 68, 42, 4, m_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(121, 91, 66, 40, m_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(m_sel ? TFT_YELLOW : TFT_WHITE, m_sel ? 0x10C2 : TFT_BLACK);
    char mBuf[8]; snprintf(mBuf, sizeof(mBuf), "%02u", (unsigned)timerEdit_m);
    tft.drawString(mBuf, 130, 98);

    // Segment Box 2: Seconds
    bool s_sel = (timerEdit_field == 2);
    tft.drawRoundRect(212, 90, 68, 42, 4, s_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(213, 91, 66, 40, s_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(s_sel ? TFT_YELLOW : TFT_WHITE, s_sel ? 0x10C2 : TFT_BLACK);
    char sBuf[8]; snprintf(sBuf, sizeof(sBuf), "%02u", (unsigned)timerEdit_s);
    tft.drawString(sBuf, 222, 98);

    // Total Duration Preview
    uint32_t totalSec = (timerEdit_h * 3600) + (timerEdit_m * 60) + timerEdit_s;
    char totMsg[48];
    snprintf(totMsg, sizeof(totMsg), "Total Duration: %u seconds", (unsigned)totalSec);
    tft.fillRect(30, 158, 260, 20, TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_9);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(totMsg, 30, 158);

    last_f = timerEdit_field;
    last_h = timerEdit_h;
    last_m = timerEdit_m;
    last_s = timerEdit_s;
}

void drawRTCSetScreen(bool fullRedraw) {
    static uint8_t last_f = 0xFF;
    static uint16_t last_y = 0;
    static uint8_t last_mo = 0, last_d = 0, last_hh = 0, last_mm = 0, last_ss = 0;

    bool changed = fullRedraw || (rtcEdit_field != last_f) ||
                   (rtcEdit_year != last_y) || (rtcEdit_month != last_mo) || (rtcEdit_day != last_d) ||
                   (rtcEdit_hour != last_hh) || (rtcEdit_minute != last_mm) || (rtcEdit_second != last_ss);
    if (!changed) return;

    if (fullRedraw) {
        // Mobile Style Top Header (y = 0..40)
        drawMobileHeader("DS3231 RTC", TFT_YELLOW);

        // Card 1: Calendar Date (y=44..118, h=74)
        tft.drawRoundRect(10, 44, 300, 74, 4, 0x4A69);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("CALENDAR DATE (YYYY-MM-DD)", 20, 48);
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("-", 122, 78);
        tft.drawString("-", 212, 78);

        // Card 2: Time of Day (y=124..198, h=74)
        tft.drawRoundRect(10, 124, 300, 74, 4, 0x4A69);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("TIME OF DAY (24-HR HH:MM:SS)", 20, 128);
        tft.setFreeFont(FONT_FREE_BOLD_18);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(":", 120, 156);
        tft.drawString(":", 210, 156);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] +/-", 10, 212);
        tft.drawString("[->] Next", 125, 212);
        tft.drawString("[<-] Save", 225, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    // Segment 0: Year (YYYY)
    bool y_sel = (rtcEdit_field == 0);
    tft.drawRoundRect(25, 66, 90, 42, 3, y_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(26, 67, 88, 40, y_sel ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_12);
    tft.setTextColor(y_sel ? TFT_YELLOW : TFT_WHITE, y_sel ? 0x10C2 : TFT_BLACK);
    char yBuf[8]; snprintf(yBuf, sizeof(yBuf), "%04u", (unsigned)rtcEdit_year);
    tft.drawString(yBuf, 35, 76);

    // Segment 1: Month (MM)
    bool mo_sel = (rtcEdit_field == 1);
    tft.drawRoundRect(140, 66, 65, 42, 3, mo_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(141, 67, 63, 40, mo_sel ? 0x10C2 : TFT_BLACK);
    tft.setFreeFont(FONT_FREE_BOLD_18);
    tft.setTextColor(mo_sel ? TFT_YELLOW : TFT_WHITE, mo_sel ? 0x10C2 : TFT_BLACK);
    char moBuf[8]; snprintf(moBuf, sizeof(moBuf), "%02u", (unsigned)rtcEdit_month);
    tft.drawString(moBuf, 150, 74);

    // Segment 2: Day (DD)
    bool d_sel = (rtcEdit_field == 2);
    tft.drawRoundRect(230, 66, 65, 42, 3, d_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(231, 67, 63, 40, d_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(d_sel ? TFT_YELLOW : TFT_WHITE, d_sel ? 0x10C2 : TFT_BLACK);
    char dBuf[8]; snprintf(dBuf, sizeof(dBuf), "%02u", (unsigned)rtcEdit_day);
    tft.drawString(dBuf, 240, 74);

    // Segment 3: Hour (HH)
    bool hh_sel = (rtcEdit_field == 3);
    tft.drawRoundRect(45, 146, 65, 42, 3, hh_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(46, 147, 63, 40, hh_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(hh_sel ? TFT_YELLOW : TFT_WHITE, hh_sel ? 0x10C2 : TFT_BLACK);
    char hhBuf[8]; snprintf(hhBuf, sizeof(hhBuf), "%02u", (unsigned)rtcEdit_hour);
    tft.drawString(hhBuf, 55, 154);

    // Segment 4: Minute (MM)
    bool mm_sel = (rtcEdit_field == 4);
    tft.drawRoundRect(140, 146, 65, 42, 3, mm_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(141, 147, 63, 40, mm_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(mm_sel ? TFT_YELLOW : TFT_WHITE, mm_sel ? 0x10C2 : TFT_BLACK);
    char mmBuf[8]; snprintf(mmBuf, sizeof(mmBuf), "%02u", (unsigned)rtcEdit_minute);
    tft.drawString(mmBuf, 150, 154);

    // Segment 5: Second (SS)
    bool ss_sel = (rtcEdit_field == 5);
    tft.drawRoundRect(230, 146, 65, 42, 3, ss_sel ? TFT_GREEN : 0x3186);
    tft.fillRect(231, 147, 63, 40, ss_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(ss_sel ? TFT_YELLOW : TFT_WHITE, ss_sel ? 0x10C2 : TFT_BLACK);
    char ssBuf[8]; snprintf(ssBuf, sizeof(ssBuf), "%02u", (unsigned)rtcEdit_second);
    tft.drawString(ssBuf, 240, 154);

    last_f = rtcEdit_field;
    last_y = rtcEdit_year;
    last_mo = rtcEdit_month;
    last_d = rtcEdit_day;
    last_hh = rtcEdit_hour;
    last_mm = rtcEdit_minute;
    last_ss = rtcEdit_second;
}

void drawPIDTuningScreen(bool fullRedraw) {
    static int last_prog = -1;
    static uint8_t last_f = 0xFF;
    static float last_h1_kp = NAN, last_h1_ki = NAN, last_h1_kd = NAN;
    static float last_h2_kp = NAN, last_h2_ki = NAN, last_h2_kd = NAN;

    ProgramRecipe_t &prec = recipes[sysStatus.active_program_idx];

    bool changed = fullRedraw ||
                   (sysStatus.active_program_idx != last_prog) ||
                   (pidEdit_field != last_f) ||
                   (prec.h1_Kp != last_h1_kp) || (prec.h1_Ki != last_h1_ki) || (prec.h1_Kd != last_h1_kd) ||
                   (prec.h2_Kp != last_h2_kp) || (prec.h2_Ki != last_h2_ki) || (prec.h2_Kd != last_h2_kd);

    if (!changed) return;

    if (fullRedraw || sysStatus.active_program_idx != last_prog) {
        // Mobile Style Top Header (y = 0..40)
        char pBuf[32];
        snprintf(pBuf, sizeof(pBuf), "P%02d: %s", sysStatus.active_program_idx + 1, prec.name);
        drawMobileHeader(pBuf, TFT_YELLOW);

        // Two Column Cards
        // Card 1: H1 PID (Left: x=6, y=44, w=150, h=154)
        tft.drawRoundRect(6, 44, 150, 154, 4, 0x4A69);
        tft.setFreeFont(FONT_FREE_BOLD_9);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("HEATER 1 PID", 14, 48);
        tft.drawFastHLine(12, 64, 138, 0x3186);

        // Card 2: H2 PID (Right: x=164, y=44, w=150, h=154)
        tft.drawRoundRect(164, 44, 150, 154, 4, 0x4A69);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("HEATER 2 PID", 172, 48);
        tft.drawFastHLine(170, 64, 138, 0x3186);

        // Footer Navigation Bar (y = 204..240)
        tft.fillRect(0, 204, 320, 32, 0x0841);
        tft.drawFastHLine(0, 204, 320, TFT_DARKCYAN);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawString("[UP/DN] +/-", 10, 212);
        tft.drawString("[->] Next", 125, 212);
        tft.drawString("[<-] Save", 225, 212);
        tft.fillRect(0, 236, 320, 4, TFT_DARKGREEN);
    }

    tft.setFreeFont(FONT_FREE_BOLD_9);

    // Left Card: H1 Fields
    // Field 0: H1 Kp
    bool f0_sel = (pidEdit_field == 0);
    tft.drawRoundRect(12, 70, 138, 36, 3, f0_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(13, 71, 136, 34, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f0_sel ? TFT_GREEN : TFT_LIGHTGREY, f0_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Kp:", 18, 78);
    tft.setTextColor(f0_sel ? TFT_YELLOW : TFT_WHITE, f0_sel ? 0x10C2 : TFT_BLACK);
    char h1kp[16]; snprintf(h1kp, sizeof(h1kp), "%.2f", prec.h1_Kp);
    tft.drawString(h1kp, 55, 78);

    // Field 1: H1 Ki
    bool f1_sel = (pidEdit_field == 1);
    tft.drawRoundRect(12, 112, 138, 36, 3, f1_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(13, 113, 136, 34, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f1_sel ? TFT_GREEN : TFT_LIGHTGREY, f1_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Ki:", 18, 120);
    tft.setTextColor(f1_sel ? TFT_YELLOW : TFT_WHITE, f1_sel ? 0x10C2 : TFT_BLACK);
    char h1ki[16]; snprintf(h1ki, sizeof(h1ki), "%.3f", prec.h1_Ki);
    tft.drawString(h1ki, 55, 120);

    // Field 2: H1 Kd
    bool f2_sel = (pidEdit_field == 2);
    tft.drawRoundRect(12, 154, 138, 36, 3, f2_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(13, 155, 136, 34, f2_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f2_sel ? TFT_GREEN : TFT_LIGHTGREY, f2_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Kd:", 18, 162);
    tft.setTextColor(f2_sel ? TFT_YELLOW : TFT_WHITE, f2_sel ? 0x10C2 : TFT_BLACK);
    char h1kd[16]; snprintf(h1kd, sizeof(h1kd), "%.2f", prec.h1_Kd);
    tft.drawString(h1kd, 55, 162);

    // Right Card: H2 Fields
    // Field 3: H2 Kp
    bool f3_sel = (pidEdit_field == 3);
    tft.drawRoundRect(170, 70, 138, 36, 3, f3_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(171, 71, 136, 34, f3_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f3_sel ? TFT_GREEN : TFT_LIGHTGREY, f3_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Kp:", 176, 78);
    tft.setTextColor(f3_sel ? TFT_YELLOW : TFT_WHITE, f3_sel ? 0x10C2 : TFT_BLACK);
    char h2kp[16]; snprintf(h2kp, sizeof(h2kp), "%.2f", prec.h2_Kp);
    tft.drawString(h2kp, 213, 78);

    // Field 4: H2 Ki
    bool f4_sel = (pidEdit_field == 4);
    tft.drawRoundRect(170, 112, 138, 36, 3, f4_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(171, 113, 136, 34, f4_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f4_sel ? TFT_GREEN : TFT_LIGHTGREY, f4_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Ki:", 176, 120);
    tft.setTextColor(f4_sel ? TFT_YELLOW : TFT_WHITE, f4_sel ? 0x10C2 : TFT_BLACK);
    char h2ki[16]; snprintf(h2ki, sizeof(h2ki), "%.3f", prec.h2_Ki);
    tft.drawString(h2ki, 213, 120);

    // Field 5: H2 Kd
    bool f5_sel = (pidEdit_field == 5);
    tft.drawRoundRect(170, 154, 138, 36, 3, f5_sel ? TFT_GREEN : 0x2104);
    tft.fillRect(171, 155, 136, 34, f5_sel ? 0x10C2 : TFT_BLACK);
    tft.setTextColor(f5_sel ? TFT_GREEN : TFT_LIGHTGREY, f5_sel ? 0x10C2 : TFT_BLACK);
    tft.drawString("Kd:", 176, 162);
    tft.setTextColor(f5_sel ? TFT_YELLOW : TFT_WHITE, f5_sel ? 0x10C2 : TFT_BLACK);
    char h2kd[16]; snprintf(h2kd, sizeof(h2kd), "%.2f", prec.h2_Kd);
    tft.drawString(h2kd, 213, 162);

    last_prog = sysStatus.active_program_idx;
    last_f = pidEdit_field;
    last_h1_kp = prec.h1_Kp; last_h1_ki = prec.h1_Ki; last_h1_kd = prec.h1_Kd;
    last_h2_kp = prec.h2_Kp; last_h2_ki = prec.h2_Ki; last_h2_kd = prec.h2_Kd;
}

void drawServiceScreen(bool fullRedraw) {
    // Hardware control screen and strings are removed from the display.
    // Control info is only visible on serial when ENABLE_SERIAL_TFT is enabled.
    (void)fullRedraw;
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

    static UIScreen_t s_lastDrawnScreen = (UIScreen_t)0xFF;
    static bool s_popupActive = false;

    // 5-Second Welcome Screen handling on boot
    if (!s_welcomeDone) {
        uint32_t elapsed = millis() - s_welcomeStartMs;
        if (elapsed < 5000) {
            drawWelcomeScreen(elapsed);
            return;
        } else {
            s_welcomeDone = true;
            tft.fillScreen(TFT_BLACK);
            s_lastDrawnScreen = (UIScreen_t)0xFF; // force full redraw of HOME screen
        }
    }

    bool isRunning = (sysStatus.currentState != STATE_IDLE && 
                      sysStatus.currentState != STATE_READY && 
                      sysStatus.currentState != STATE_ALARM_FAULT);
    if (isRunning && currentScreen != SCREEN_HOME) {
        // If process is running, settings cannot be accessed: force back to HOME
        currentScreen = SCREEN_HOME;
    }

    bool hasPopup = (rtcConfirmUntil && millis() < rtcConfirmUntil) ||
                    (limitSwitchWarningUntil && millis() < limitSwitchWarningUntil) ||
                    (sysStatus.forceStartPending);

    bool fullRedraw = (currentScreen != s_lastDrawnScreen);

    // If popup just closed, trigger full redraw of current screen to clear popup overlay cleanly
    if (s_popupActive && !hasPopup) {
        fullRedraw = true;
    }
    s_popupActive = hasPopup;

    if (fullRedraw) {
        tft.fillScreen(TFT_BLACK);
        s_lastDrawnScreen = currentScreen;
    }

    switch (currentScreen) {
        case SCREEN_HOME:
            drawHomeScreen(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawHomeScreen();
#endif
            break;
        case SCREEN_SETTINGS_MENU:
            drawSettingsMenu(fullRedraw);
            break;
        case SCREEN_RECIPES_LIST:
            drawRecipesListScreen(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawProgramSelectScreen();
#endif
            break;
        case SCREEN_RECIPE_EDIT:
            drawProgramEditScreen(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawProgramEditScreen();
#endif
            break;
        case SCREEN_TIMER_EDIT:
            drawTimerEditor(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawTimerEditor();
#endif
            break;
        case SCREEN_RTC_SET:
            drawRTCSetScreen(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawRTCSetScreen();
#endif
            break;
        case SCREEN_PID_TUNING:
            drawPIDTuningScreen(fullRedraw);
#if ENABLE_SERIAL_TFT
            vd_drawPIDTuningScreen();
#endif
            break;
        case SCREEN_TEMP_MANIP:
            drawTempManipScreen(fullRedraw);
            break;
        case SCREEN_FACTORY_RESET_PIN:
            drawFactoryResetPinScreen(fullRedraw);
            break;
        case SCREEN_NAME_EDIT:
            drawProgramNameEditScreen(fullRedraw);
            break;
    }

    // 1-second live RTC clock update in mobile header (only in setting/sub-screens, not on home)
    static uint32_t s_lastClockSec = 0xFFFFFFFF;
    uint32_t curClockSec = millis() / 1000;
    if (!hasPopup && curClockSec != s_lastClockSec) {
        if (currentScreen != SCREEN_HOME) {
            char timeStr[16];
            getCurrentTimeString(timeStr, sizeof(timeStr));
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.setTextColor(TFT_WHITE, 0x0841);
            tft.fillRect(10, 2, 85, 16, 0x0841);
            tft.drawString(timeStr, 10, 2);
        }
        s_lastClockSec = curClockSec;
    }

    // draw transient RTC confirmation popup if any (drawn once per activation to avoid blinking)
    static uint32_t s_lastRtcConfirmUntil = 0;
    if (rtcConfirmUntil && millis() < rtcConfirmUntil) {
        if (rtcConfirmUntil != s_lastRtcConfirmUntil) {
            int w = 260; int h = 40;
            int x = (320 - w) / 2; int y = (240 - h) / 2;
            tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_WHITE);
            tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(rtcConfirmMsg, x + 8, y + 10);
            s_lastRtcConfirmUntil = rtcConfirmUntil;
        }
    } else {
        rtcConfirmUntil = 0;
        s_lastRtcConfirmUntil = 0;
    }

    // draw limit switch warning popup if any (drawn once per activation to avoid blinking)
    static uint32_t s_lastLimitWarningUntil = 0;
    static char s_lastLimitWarningMsg[64] = "";
    if (limitSwitchWarningUntil && millis() < limitSwitchWarningUntil) {
        if (limitSwitchWarningUntil != s_lastLimitWarningUntil || strcmp(limitSwitchWarningMsg, s_lastLimitWarningMsg) != 0) {
            int w = 280; int h = 50;
            int x = (320 - w) / 2; int y = (240 - h) / 2;
            tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_RED);
            tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("WARNING!", x + 8, y + 6);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(limitSwitchWarningMsg, x + 8, y + 26);
            s_lastLimitWarningUntil = limitSwitchWarningUntil;
            strncpy(s_lastLimitWarningMsg, limitSwitchWarningMsg, sizeof(s_lastLimitWarningMsg) - 1);
        }
    } else {
        limitSwitchWarningUntil = 0;
        s_lastLimitWarningUntil = 0;
        s_lastLimitWarningMsg[0] = '\0';
    }

    // draw force-start confirmation overlay if pending (static frame drawn once, text updated with padding)
    static bool s_forceStartDrawn = false;
    static uint32_t s_lastForceRemaining = 0xFFFFFFFF;
    if (sysStatus.forceStartPending) {
        if ((int32_t)(sysStatus.forceStartUntilMs - millis()) <= 0) {
            // timeout, clear pending
            sysStatus.forceStartPending = false;
            sysStatus.forceStartUntilMs = 0;
            s_forceStartDrawn = false;
            s_lastForceRemaining = 0xFFFFFFFF;
        } else {
            int w = 300; int h = 66;
            int x = (320 - w) / 2; int y = (240 - h) / 2;
            uint32_t remaining = (sysStatus.forceStartUntilMs > millis()) ? (sysStatus.forceStartUntilMs - millis()) / 1000 : 0;

            if (!s_forceStartDrawn) {
                tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_WHITE);
                tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
                tft.setFreeFont(FONT_FREE_BOLD_9);
                tft.setTextColor(TFT_YELLOW, TFT_BLACK);
                tft.drawString("SETPOINT NOT REACHED!", x + 10, y + 8);
                tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
                tft.drawString("[<-] Cancel", x + 10, y + 44);
                s_forceStartDrawn = true;
            }

            if (remaining != s_lastForceRemaining) {
                tft.setFreeFont(FONT_FREE_BOLD_9);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.setTextPadding(260);
                char msg[48];
                snprintf(msg, sizeof(msg), "[START] Force Start (%us)", (unsigned)remaining);
                tft.drawString(msg, x + 10, y + 26);
                tft.setTextPadding(0);
                s_lastForceRemaining = remaining;
            }
        }
    } else {
        s_forceStartDrawn = false;
        s_lastForceRemaining = 0xFFFFFFFF;
    }

    // Draw Emergency Stop popup overlay if active
    static bool s_estopDrawn = false;
    if (sysStatus.emergency_stop_active) {
        int w = 300; int h = 76;
        int x = (320 - w) / 2; int y = (240 - h) / 2;
        if (!s_estopDrawn) {
            tft.fillRect(x - 4, y - 4, w + 8, h + 8, TFT_RED);
            tft.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_BLACK);
            tft.setFreeFont(FONT_FREE_BOLD_9);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("EMERGENCY STOP ACTIVATED!", x + 10, y + 10);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("ALL OUTPUTS SHUT DOWN", x + 10, y + 32);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString("PLEASE RESTART SYSTEM", x + 10, y + 54);
            s_estopDrawn = true;
        }
    } else {
        s_estopDrawn = false;
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

#if ENABLE_PHYSICAL_TFT
    // Ensure TFT chip-select is explicitly de-asserted (HIGH) so other SPI devices can access the bus cleanly
    safeDigitalWrite(PIN_TFT_CS, HIGH);
#endif
}

#if ENABLE_WIFI_WEBSERVER
static void sendCorsHeaders() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

#if ENABLE_APP_REMOTE

static const char* getScreenName(UIScreen_t screen) {
    switch (screen) {
        case SCREEN_HOME: return "HOME";
        case SCREEN_SETTINGS_MENU: return "SETTINGS_MENU";
        case SCREEN_RECIPES_LIST: return "RECIPES_LIST";
        case SCREEN_RECIPE_EDIT: return "RECIPE_EDIT";
        case SCREEN_TIMER_EDIT: return "TIMER_EDIT";
        case SCREEN_TEMP_MANIP: return "TEMP_MANIP";
        case SCREEN_PID_TUNING: return "PID_TUNING";
        case SCREEN_RTC_SET: return "RTC_SET";
        case SCREEN_FACTORY_RESET_PIN: return "FACTORY_RESET_PIN";
        case SCREEN_NAME_EDIT: return "NAME_EDIT";
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
    json += "\"torque_motor\":" + String(sysStatus.torque_motor_running ? "true" : "false") + ",";
    json += "\"emergency_stop\":" + String(sysStatus.emergency_stop_active ? "true" : "false") + ",";
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
        if (currentScreen != SCREEN_HOME) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot start while in settings\"}");
            return;
        }
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
        if (currentScreen != SCREEN_HOME) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot force start while in settings\"}");
            return;
        }
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
    } else if (action.equalsIgnoreCase("emergency_stop")) {
        sysStatus.emergency_stop_active = true;
        safeDigitalWrite(PIN_SSR_1, LOW);
        safeDigitalWrite(PIN_SSR_2, LOW);
        safeDigitalWrite(PIN_PNEUMATIC, LOW);
        safeDigitalWrite(PIN_TORQUE_MOTOR, LOW);
        sysStatus.torque_motor_running = false;
        sysStatus.motor_up_running = false;
        sysStatus.motor_down_running = false;
        sysStatus.remaining_time_sec = 0;
        sysStatus.forceStartActive = false;
        sysStatus.forceStartPending = false;
        currentScreen = SCREEN_HOME;
        transitionToState(STATE_IDLE);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Emergency stop executed\"}");
        return;
    } else if (action.equalsIgnoreCase("reset_alarm") || action.equalsIgnoreCase("stop")) {
        if (currentScreen != SCREEN_HOME) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot stop while in settings\"}");
            return;
        }
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
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot change program while process is running\"}");
            return;
        }
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
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot jog while process is running\"}");
            return;
        }
        if (sysStatus.home_limit_active) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Already at home limit\"}");
            return;
        }
        safeDigitalWrite(PIN_MOTOR_UP, HIGH);
        delay(250);
        safeDigitalWrite(PIN_MOTOR_UP, LOW);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Jog up executed\"}");
        return;
    } else if (action.equalsIgnoreCase("jog_down")) {
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot jog while process is running\"}");
            return;
        }
        if (sysStatus.down_limit_active) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Already at down limit\"}");
            return;
        }
        safeDigitalWrite(PIN_MOTOR_DOWN, HIGH);
        delay(250);
        safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Jog down executed\"}");
        return;
    } else if (action.equalsIgnoreCase("toggle_ssr1")) {
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot toggle SSR while process is running\"}");
            return;
        }
        int st = digitalRead(PIN_SSR_1);
        safeDigitalWrite(PIN_SSR_1, st ? LOW : HIGH);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"ssr1\":" + String(!st ? "true" : "false") + "}");
        return;
    } else if (action.equalsIgnoreCase("toggle_ssr2")) {
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot toggle SSR while process is running\"}");
            return;
        }
        int st = digitalRead(PIN_SSR_2);
        safeDigitalWrite(PIN_SSR_2, st ? LOW : HIGH);
        webServer.send(200, "application/json", "{\"status\":\"ok\",\"ssr2\":" + String(!st ? "true" : "false") + "}");
        return;
    } else if (action.equalsIgnoreCase("set_screen")) {
        bool isProcessRunning = (sysStatus.currentState != STATE_IDLE && 
                                 sysStatus.currentState != STATE_READY && 
                                 sysStatus.currentState != STATE_ALARM_FAULT);
        if (isProcessRunning) {
            webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cannot open settings while process is running\"}");
            return;
        }
        if (webServer.hasArg("screen")) {
            int sc = webServer.arg("screen").toInt();
            if (sc >= 0 && sc <= 9) {
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
#endif