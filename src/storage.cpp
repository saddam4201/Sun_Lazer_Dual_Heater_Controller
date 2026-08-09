#include "storage.h"
#include <SD.h>
#include "debug_config.h"
#include "rtc.h"

// Recent logs buffer
static char recentLogs[LOG_RECENT_COUNT][256];
static uint8_t recentLogHead = 0; // next write index
static uint8_t recentLogCount = 0;

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

    preferences.begin("sun_lazer", true);
    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rec_%d", i);
        size_t bytesRead = preferences.getBytes(key, &recipes[i], sizeof(ProgramRecipe_t));

        if (bytesRead == sizeof(ProgramRecipe_t) && recipes[i].magic == RECIPE_MAGIC && recipes[i].version == RECIPE_VERSION) {
            // valid new-format recipe loaded
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
                continue;
            }
        }

        // No valid recipe data; populate defaults and set magic/version
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
    // logEntry is expected to be a CSV line without newline
    File logFile = SD.open("/process_history.csv", FILE_APPEND);
    if (logFile) {
        logFile.println(logEntry);
        logFile.close();
    }
    // keep recent in RAM
    pushRecentLog(logEntry);
}