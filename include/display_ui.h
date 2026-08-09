#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include "config.h"

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_PROGRAM_SELECT,
    SCREEN_PROGRAM_EDIT,
    SCREEN_SERVICE
} UIScreen_t;

extern UIScreen_t currentScreen;

void initDisplayAndWeb();
void updateTFTDisplay();
void setupWebServer();
void handleButtonInputs();

#endif // DISPLAY_UI_H