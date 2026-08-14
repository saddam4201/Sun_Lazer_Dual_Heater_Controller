#include "storage.h"
#include "debug_config.h"
#include "rtc.h"
#if ENABLE_SERIAL_TFT
#include "serial_display.h"
#endif

// Recent logs buffer
static char recentLogs[LOG_RECENT_COUNT][256];
static uint8_t recentLogHead = 0; // next write index
static uint8_t recentLogCount = 0;

#if ENABLE_SD_CARD
#include <SD.h>

bool initStorageModules() {
    if (!SD.begin(PIN_SD_CS)) {
        DEBUG_PRINTLN("[SD] Initialization Failed or Card Not Present!");
        return false;
    } else {
        DEBUG_PRINTLN("[SD] Card Initialized Successfully.");
        // Ensure file has header
        File logFile = SD.open("/process_history.csv", FILE_APPEND);
        if (logFile) {
            // If file is new/empty, write header (best-effort check size)
            if (logFile.size() == 0) {
                logFile.println("timestamp,program,h1_set,h1_act,h2_set,h2_act,process_time_sec,max_torque_nm,result,alarm_code");
            }
            logFile.close();
        }
        return true;
    }
}
#else
// SD disabled: keep in-RAM logging and provide stubs
bool initStorageModules() {
    DEBUG_PRINTLN("[SD] SD support disabled at compile-time (ENABLE_SD_CARD=0)");
    return false;
}
#endif

void loadRecipesFromNVS() {
    // Define old recipe layout for migration
    struct OldProgramRecipe_t {
        char name[16];
        float h1_setpoint_c;
        float h2_setpoint_c;
        uint32_t process_time_sec;
        float torque_limit_nm;
        float temp_tolerance_c;
    };

    bool nvs_read_ok = preferences.begin("sun_lazer", true);
    if (!nvs_read_ok) {
        DEBUG_PRINTF("[NVS] preferences.begin(readOnly) failed - proceeding with defaults\n");
    }

    bool anyLoaded = false;
    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rec_%d", i);
        size_t bytesRead = 0;

        if (nvs_read_ok) {
            bytesRead = preferences.getBytes(key, &recipes[i], sizeof(ProgramRecipe_t));

            if (bytesRead == sizeof(ProgramRecipe_t) && recipes[i].magic == RECIPE_MAGIC && recipes[i].version == RECIPE_VERSION) {
                // valid new-format recipe loaded
                anyLoaded = true;
                continue;
            }

            // Try to detect old-format recipe
            if (bytesRead == sizeof(OldProgramRecipe_t)) {
                OldProgramRecipe_t oldRec;
                // read into temp (getBytes again into oldRec)
                size_t b2 = preferences.getBytes(key, &oldRec, sizeof(OldProgramRecipe_t));
                if (b2 == sizeof(OldProgramRecipe_t)) {
                    // migrate fields
                    memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
                    recipes[i].magic = RECIPE_MAGIC;
                    recipes[i].version = RECIPE_VERSION;
                    strncpy(recipes[i].name, oldRec.name, sizeof(recipes[i].name) - 1);
                    recipes[i].h1_setpoint_c = oldRec.h1_setpoint_c;
                    recipes[i].h2_setpoint_c = oldRec.h2_setpoint_c;
                    recipes[i].process_time_sec = oldRec.process_time_sec;
                    recipes[i].torque_limit_nm = oldRec.torque_limit_nm;
                    recipes[i].temp_tolerance_c = oldRec.temp_tolerance_c;
                    // set default PID tunings for migrated recipes
                    recipes[i].h1_Kp = 20.0f; recipes[i].h1_Ki = 0.5f; recipes[i].h1_Kd = 1.0f;
                    recipes[i].h2_Kp = 20.0f; recipes[i].h2_Ki = 0.5f; recipes[i].h2_Kd = 1.0f;
                    anyLoaded = true;
                    continue;
                }
            }
        }

        // No valid recipe data or NVS not available; populate defaults and set magic/version
        memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
        recipes[i].magic = RECIPE_MAGIC;
        recipes[i].version = RECIPE_VERSION;
        snprintf(recipes[i].name, sizeof(recipes[i].name), "Program %02d", i + 1);
        recipes[i].h1_setpoint_c = 150.0f;
        recipes[i].h2_setpoint_c = 150.0f;
        recipes[i].process_time_sec = 60;
        recipes[i].torque_limit_nm = 18.0f;
        recipes[i].temp_tolerance_c = 5.0f;
        // default PID tunings
        recipes[i].h1_Kp = 20.0f; recipes[i].h1_Ki = 0.5f; recipes[i].h1_Kd = 1.0f;
        recipes[i].h2_Kp = 20.0f; recipes[i].h2_Ki = 0.5f; recipes[i].h2_Kd = 1.0f;
    }

    if (nvs_read_ok) preferences.end();

    // If nothing was loaded from NVS (or NVS was unavailable), ask the user whether to persist defaults
    if (!anyLoaded) {
        DEBUG_PRINTF("[NVS] No saved recipes found in NVS. Offering to persist default recipes.\n");

        // Try to open NVS for writing
        bool nvs_write_ok = preferences.begin("sun_lazer", false);
        if (!nvs_write_ok) {
            DEBUG_PRINTF("[NVS] preferences.begin(write) failed: cannot persist defaults.\n");
#if ENABLE_SERIAL_TFT
            vd_popup("No stored settings found. Connect to WiFi/web UI or press OK to create defaults (NVS unavailable). Please check NVS partition.");
#else
            // fallback serial prompt
            Serial.println("[NVS] No stored settings found and cannot open NVS for write. Defaults loaded into RAM only.");
#endif
            // nothing more to do if NVS can't be opened for write
            return;
        }

#if ENABLE_SERIAL_TFT
        vd_popup("No settings found. Press OK to save defaults to NVS, or wait 10s to skip.");
#else
        Serial.println("[NVS] No settings found. Press OK (button) to save defaults to NVS, or wait 10s to skip.");
#endif

        const uint32_t timeoutMs = 10000;
        uint32_t tstart = millis();
        bool userConfirmed = false;
        while ((millis() - tstart) < timeoutMs) {
            // Button is active-low
            if (safeDigitalRead(PIN_BTN_OK) == LOW) {
                userConfirmed = true;
                break;
            }
            delay(50);
        }

        if (userConfirmed) {
            DEBUG_PRINTF("[NVS] User confirmed - saving default recipes to NVS...\n");
            for (int i = 0; i < 10; i++) {
                char key[16];
                snprintf(key, sizeof(key), "rec_%d", i);
                preferences.putBytes(key, &recipes[i], sizeof(ProgramRecipe_t));
            }
            preferences.end();
#if ENABLE_SERIAL_TFT
            vd_popup("Default recipes saved to NVS.");
#else
            Serial.println("[NVS] Default recipes saved to NVS.");
#endif
        } else {
            DEBUG_PRINTF("[NVS] User did not confirm - defaults remain in RAM only.\n");
            preferences.end();
#if ENABLE_SERIAL_TFT
            vd_popup("Defaults loaded in RAM only. Press OK later in Service->Save to persist.");
#else
            Serial.println("[NVS] Defaults loaded in RAM only. Use UI to save later.");
#endif
        }
    }
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

void saveAllRecipesToNVS() {
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) {
        DEBUG_PRINTF("[NVS] preferences.begin(write) failed: cannot save all recipes\n");
        return;
    }

    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rec_%d", i);
        preferences.putBytes(key, &recipes[i], sizeof(ProgramRecipe_t));
    }

    preferences.end();
    DEBUG_PRINTF("[NVS] All recipes saved to NVS.\n");
}

void saveStartModeToNVS(bool autoMode) {
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) {
        DEBUG_PRINTF("[NVS] preferences.begin(write) failed: cannot save start mode\n");
        return;
    }
    preferences.putUInt("cfg_start_auto", autoMode ? 1U : 0U);
    preferences.end();
    DEBUG_PRINTF("[NVS] start mode saved: %s\n", autoMode ? "AUTO" : "MANUAL");
}

bool loadStartModeFromNVS(bool *autoMode) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (autoMode) *autoMode = true; // default to auto
        return false;
    }
    uint32_t v = preferences.getUInt("cfg_start_auto", 1U);
    if (autoMode) *autoMode = (v != 0U);
    preferences.end();
    return true;
}

void pushRecentLog(const char* csvLine) {
    strncpy(recentLogs[recentLogHead], csvLine, sizeof(recentLogs[0]) - 1);
    recentLogs[recentLogHead][sizeof(recentLogs[0]) - 1] = '\0';
    recentLogHead = (recentLogHead + 1) % LOG_RECENT_COUNT;
    if (recentLogCount < LOG_RECENT_COUNT) recentLogCount++;
}

const char* getRecentLog(uint8_t idx) {
    if (idx >= recentLogCount) return NULL;
    // 0 = newest
    int pos = (int)recentLogHead - 1 - idx;
    while (pos < 0) pos += LOG_RECENT_COUNT;
    return recentLogs[pos % LOG_RECENT_COUNT];
}

void logToSD(const char* logEntry) {
    // keep recent in RAM
    pushRecentLog(logEntry);

#if ENABLE_SD_CARD
    // If SD is enabled, append to file (best-effort)
    File logFile = SD.open("/process_history.csv", FILE_APPEND);
    if (logFile) {
        logFile.println(logEntry);
        logFile.close();
    }
#endif
}