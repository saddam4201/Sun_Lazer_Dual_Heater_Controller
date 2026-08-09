#include "storage.h"
#include <SD.h>
#include "debug_config.h"

void initStorageModules() {
    if (!SD.begin(PIN_SD_CS)) {
        DEBUG_PRINTLN("[SD] Initialization Failed or Card Not Present!");
    } else {
        DEBUG_PRINTLN("[SD] Card Initialized Successfully.");
    }
}

void loadRecipesFromNVS() {
    preferences.begin("sun_lazer", true);
    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rec_%d", i);
        size_t bytesRead = preferences.getBytes(key, &recipes[i], sizeof(ProgramRecipe_t));
        if (bytesRead != sizeof(ProgramRecipe_t) || recipes[i].magic != RECIPE_MAGIC || recipes[i].version != RECIPE_VERSION) {
            // No valid saved recipe found; populate defaults and set magic/version
            memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
            recipes[i].magic = RECIPE_MAGIC;
            recipes[i].version = RECIPE_VERSION;
            snprintf(recipes[i].name, sizeof(recipes[i].name), "Program %02d", i + 1);
            recipes[i].h1_setpoint_c = 150.0f;
            recipes[i].h2_setpoint_c = 150.0f;
            recipes[i].process_time_sec = 60;
            recipes[i].torque_limit_nm = 18.0f;
            recipes[i].temp_tolerance_c = 5.0f;
        }
    }
    preferences.end();
}

void saveRecipeToNVS(uint8_t index) {
    // Ensure magic/version are present before writing
    recipes[index].magic = RECIPE_MAGIC;
    recipes[index].version = RECIPE_VERSION;

    preferences.begin("sun_lazer", false);
    char key[16];
    snprintf(key, sizeof(key), "rec_%d", index);
    preferences.putBytes(key, &recipes[index], sizeof(ProgramRecipe_t));
    preferences.end();
}

void logToSD(const char* logEntry) {
    File logFile = SD.open("/process_history.txt", FILE_APPEND);
    if (logFile) {
        logFile.println(logEntry);
        logFile.close();
    }
}