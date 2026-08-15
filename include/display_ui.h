#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include "config.h"

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_PROGRAM_SELECT,
    SCREEN_PROGRAM_EDIT,
    SCREEN_TIMER_EDIT,
    SCREEN_PID_TUNING,
    SCREEN_SERVICE,
    SCREEN_RTC_SET
} UIScreen_t;

extern UIScreen_t currentScreen;

void initDisplayAndWeb();
void updateTFTDisplay();
void setupWebServer();
void handleButtonInputs();
void showLimitSwitchWarning(const char* message);

#if ENABLE_APP_REMOTE
void injectAppButton(uint8_t btnMask);
void setupAppRemoteEndpoints();
#endif

#endif // DISPLAY_UI_H