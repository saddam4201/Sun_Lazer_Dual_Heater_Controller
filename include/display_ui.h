#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include "config.h"

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_SETTINGS_MENU,
    SCREEN_RECIPES_LIST,
    SCREEN_RECIPE_EDIT,
    SCREEN_TIMER_EDIT,
    SCREEN_TEMP_MANIP,
    SCREEN_PID_TUNING,
    SCREEN_RTC_SET,
    SCREEN_FACTORY_RESET_PIN,
    SCREEN_NAME_EDIT
} UIScreen_t;

extern UIScreen_t currentScreen;

void initDisplayAndWeb();
void updateTFTDisplay();
#if ENABLE_WIFI_WEBSERVER
void setupWebServer();
#endif
void handleButtonInputs();
void showLimitSwitchWarning(const char* message);

#if ENABLE_APP_REMOTE
void injectAppButton(uint8_t btnMask);
void setupAppRemoteEndpoints();
#endif

#endif // DISPLAY_UI_H