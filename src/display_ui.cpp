#include "display_ui.h"
#include "control_tasks.h"
#include "storage.h"
#include "debug_config.h"
#include <WiFi.h>

UIScreen_t currentScreen = SCREEN_HOME;
uint8_t selectedEditField = 0; // 0: H1 Temp, 1: H2 Temp, 2: Process Time, 3: Torque Limit

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
                else if (selectedEditField == 2) recipes[sysStatus.active_program_idx].process_time_sec += 10;
                else if (selectedEditField == 3) recipes[sysStatus.active_program_idx].torque_limit_nm += 0.5f;
            } else if (btnDown) {
                if (selectedEditField == 0 && recipes[sysStatus.active_program_idx].h1_setpoint_c >= 5.0f)
                    recipes[sysStatus.active_program_idx].h1_setpoint_c -= 5.0f;
                else if (selectedEditField == 1 && recipes[sysStatus.active_program_idx].h2_setpoint_c >= 5.0f)
                    recipes[sysStatus.active_program_idx].h2_setpoint_c -= 5.0f;
                else if (selectedEditField == 2 && recipes[sysStatus.active_program_idx].process_time_sec >= 10)
                    recipes[sysStatus.active_program_idx].process_time_sec -= 10;
                else if (selectedEditField == 3 && recipes[sysStatus.active_program_idx].torque_limit_nm >= 1.0f)
                    recipes[sysStatus.active_program_idx].torque_limit_nm -= 0.5f;
            } else if (btnRight) {
                selectedEditField = (selectedEditField + 1) % 4;
            } else if (btnOk) {
                saveRecipeToNVS(sysStatus.active_program_idx);
                currentScreen = SCREEN_PROGRAM_SELECT;
            } else if (btnLeft) {
                currentScreen = SCREEN_PROGRAM_SELECT;
            }
            break;

        case SCREEN_SERVICE:
            if (btnLeft) {
                currentScreen = SCREEN_HOME;
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

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("[<-]: Back to Home Screen", 10, 215, 2);
}

void updateTFTDisplay() {
    tft.fillScreen(TFT_BLACK);
    switch (currentScreen) {
        case SCREEN_HOME: drawHomeScreen(); break;
        case SCREEN_PROGRAM_SELECT: drawProgramSelectScreen(); break;
        case SCREEN_PROGRAM_EDIT: drawProgramEditScreen(); break;
        case SCREEN_SERVICE: drawServiceScreen(); break;
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
    webServer.begin();
}