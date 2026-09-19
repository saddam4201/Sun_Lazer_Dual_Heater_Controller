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
    // Layouts for backward-compatible migration
    struct OldRecipeV1_t {
        uint16_t magic;
        uint8_t version;
        char name[16];
        float h1_setpoint_c;
        float h2_setpoint_c;
        uint32_t process_time_sec;
        float torque_limit_nm;
        float temp_tolerance_c;
        float h1_Kp, h1_Ki, h1_Kd;
        float h2_Kp, h2_Ki, h2_Kd;
    };

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
    bool needsReSave = false;

    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rec_%d", i);
        size_t bytesRead = 0;

        if (nvs_read_ok) {
            bytesRead = preferences.getBytes(key, &recipes[i], sizeof(ProgramRecipe_t));

            if (bytesRead == sizeof(ProgramRecipe_t) && recipes[i].magic == RECIPE_MAGIC) {
                if (recipes[i].version == RECIPE_VERSION) {
                    // valid current-format recipe loaded
                    anyLoaded = true;
                    continue;
                }
                if (recipes[i].version == 3) {
                    // Migrate from V3 to V4: update default 150.0C to 50.0C
                    recipes[i].version = RECIPE_VERSION;
                    if (recipes[i].h1_setpoint_c == 150.0f) recipes[i].h1_setpoint_c = 50.0f;
                    if (recipes[i].h2_setpoint_c == 150.0f) recipes[i].h2_setpoint_c = 50.0f;
                    anyLoaded = true;
                    needsReSave = true;
                    continue;
                }
            }

            // Try to detect V1 format recipe
            if (bytesRead == sizeof(OldRecipeV1_t)) {
                OldRecipeV1_t v1;
                preferences.getBytes(key, &v1, sizeof(OldRecipeV1_t));
                if (v1.magic == RECIPE_MAGIC) {
                    memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
                    recipes[i].magic = RECIPE_MAGIC;
                    recipes[i].version = RECIPE_VERSION;
                    strncpy(recipes[i].name, v1.name, sizeof(recipes[i].name) - 1);
                    recipes[i].h1_setpoint_c = (v1.h1_setpoint_c == 150.0f) ? 50.0f : v1.h1_setpoint_c;
                    recipes[i].h2_setpoint_c = (v1.h2_setpoint_c == 150.0f) ? 50.0f : v1.h2_setpoint_c;
                    recipes[i].process_time_sec = v1.process_time_sec;
                    recipes[i].temp_tolerance_c = v1.temp_tolerance_c;
                    recipes[i].h1_temp_offset_pct = 0.0f;
                    recipes[i].h2_temp_offset_pct = 0.0f;
                    recipes[i].torque_unit = (uint8_t)TORQUE_UNIT_NM;
                    recipes[i].h1_Kp = v1.h1_Kp; recipes[i].h1_Ki = v1.h1_Ki; recipes[i].h1_Kd = v1.h1_Kd;
                    recipes[i].h2_Kp = v1.h2_Kp; recipes[i].h2_Ki = v1.h2_Ki; recipes[i].h2_Kd = v1.h2_Kd;
                    anyLoaded = true;
                    needsReSave = true;
                    continue;
                }
            }

            // Try to detect legacy unversioned recipe
            if (bytesRead == sizeof(OldProgramRecipe_t)) {
                OldProgramRecipe_t oldRec;
                preferences.getBytes(key, &oldRec, sizeof(OldProgramRecipe_t));
                memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
                recipes[i].magic = RECIPE_MAGIC;
                recipes[i].version = RECIPE_VERSION;
                strncpy(recipes[i].name, oldRec.name, sizeof(recipes[i].name) - 1);
                recipes[i].h1_setpoint_c = (oldRec.h1_setpoint_c == 150.0f) ? 50.0f : oldRec.h1_setpoint_c;
                recipes[i].h2_setpoint_c = (oldRec.h2_setpoint_c == 150.0f) ? 50.0f : oldRec.h2_setpoint_c;
                recipes[i].process_time_sec = oldRec.process_time_sec;
                recipes[i].temp_tolerance_c = oldRec.temp_tolerance_c;
                recipes[i].h1_temp_offset_pct = 0.0f;
                recipes[i].h2_temp_offset_pct = 0.0f;
                recipes[i].torque_unit = (uint8_t)TORQUE_UNIT_NM;
                recipes[i].h1_Kp = 20.0f; recipes[i].h1_Ki = 0.5f; recipes[i].h1_Kd = 1.0f;
                recipes[i].h2_Kp = 20.0f; recipes[i].h2_Ki = 0.5f; recipes[i].h2_Kd = 1.0f;
                anyLoaded = true;
                needsReSave = true;
                continue;
            }
        }

        // No valid recipe data or NVS not available: populate clean defaults (50.0 C)
        memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
        recipes[i].magic = RECIPE_MAGIC;
        recipes[i].version = RECIPE_VERSION;
        snprintf(recipes[i].name, sizeof(recipes[i].name), "Program %02d", i + 1);
        recipes[i].h1_setpoint_c = 50.0f;
        recipes[i].h2_setpoint_c = 50.0f;
        recipes[i].process_time_sec = 60;
        recipes[i].temp_tolerance_c = 5.0f;
        recipes[i].h1_temp_offset_pct = 0.0f;
        recipes[i].h2_temp_offset_pct = 0.0f;
        recipes[i].torque_unit = (uint8_t)TORQUE_UNIT_NM;
        recipes[i].h1_Kp = 20.0f; recipes[i].h1_Ki = 0.5f; recipes[i].h1_Kd = 1.0f;
        recipes[i].h2_Kp = 20.0f; recipes[i].h2_Ki = 0.5f; recipes[i].h2_Kd = 1.0f;
        needsReSave = true;
    }

    if (nvs_read_ok) preferences.end();

    // Auto-persist: if NVS was blank or required migration, save all recipes immediately
    // so data is guaranteed to retain in memory across all future power cycles
    if (!anyLoaded || needsReSave) {
        DEBUG_PRINTF("[NVS] Persisting recipes to NVS for reliable retention...\n");
        saveAllRecipesToNVS();
    }
}


void saveActiveProgramToNVS(uint8_t index) {
    if (index >= 10) return;
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) {
        DEBUG_PRINTF("[NVS] preferences.begin(write) failed: cannot save active program\n");
        return;
    }
    preferences.putUChar("cfg_act_prog", index);
    preferences.end();
    DEBUG_PRINTF("[NVS] Active program index saved: %u\n", index);
}

bool loadActiveProgramFromNVS(uint8_t *index) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (index) *index = 0;
        return false;
    }
    uint8_t v = preferences.getUChar("cfg_act_prog", 0);
    if (v >= 10) v = 0;
    if (index) *index = v;
    preferences.end();
    return true;
}

void saveRecipeToNVS(uint8_t index) {
    if (index >= 10) return;
    // Ensure magic/version are present before writing
    recipes[index].magic = RECIPE_MAGIC;
    recipes[index].version = RECIPE_VERSION;

    preferences.begin("sun_lazer", false);
    char key[16];
    snprintf(key, sizeof(key), "rec_%d", index);
    preferences.putBytes(key, &recipes[index], sizeof(ProgramRecipe_t));
    preferences.putUChar("cfg_act_prog", index);
    preferences.end();
    DEBUG_PRINTF("[NVS] Recipe %d and active program index saved.\n", index);
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
    preferences.putBool("cfg_start_auto", autoMode);
    preferences.end();
    DEBUG_PRINTF("[NVS] start mode saved: %s\n", autoMode ? "AUTO" : "MANUAL");
}

bool loadStartModeFromNVS(bool *autoMode) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (autoMode) *autoMode = true; // default to auto
        return false;
    }
    bool v = preferences.getBool("cfg_start_auto", true);
    if (autoMode) *autoMode = v;
    preferences.end();
    return true;
}

void saveRelayTypeToNVS(RelayType_t relayType) {
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) return;
    preferences.putUChar("cfg_relay_type", (uint8_t)relayType);
    preferences.end();
    DEBUG_PRINTF("[NVS] Relay type saved: %u\n", (unsigned)relayType);
}

bool loadRelayTypeFromNVS(RelayType_t *relayType) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (relayType) *relayType = RELAY_TYPE_SSR; // default SSR
        return false;
    }
    uint8_t v = preferences.getUChar("cfg_relay_type", (uint8_t)RELAY_TYPE_SSR);
    if (v > 1) v = (uint8_t)RELAY_TYPE_SSR;
    if (relayType) *relayType = (RelayType_t)v;
    preferences.end();
    return true;
}

void saveTorqueUnitToNVS(TorqueUnit_t unit) {
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) return;
    preferences.putUChar("cfg_torque_unit", (uint8_t)unit);
    preferences.end();
    DEBUG_PRINTF("[NVS] Torque unit saved: %u\n", (unsigned)unit);
}

bool loadTorqueUnitFromNVS(TorqueUnit_t *unit) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (unit) *unit = TORQUE_UNIT_NM; // default Nm
        return false;
    }
    uint8_t v = preferences.getUChar("cfg_torque_unit", (uint8_t)TORQUE_UNIT_NM);
    if (v > 2) v = (uint8_t)TORQUE_UNIT_NM;
    if (unit) *unit = (TorqueUnit_t)v;
    preferences.end();
    return true;
}

void saveTempManipToNVS(float h1_pct, float h2_pct) {
    bool ok = preferences.begin("sun_lazer", false);
    if (!ok) return;
    preferences.putFloat("cfg_h1_manip", h1_pct);
    preferences.putFloat("cfg_h2_manip", h2_pct);
    preferences.end();
    DEBUG_PRINTF("[NVS] Temp manip saved: H1=%.1f%%, H2=%.1f%%\n", h1_pct, h2_pct);
}

bool loadTempManipFromNVS(float *h1_pct, float *h2_pct) {
    bool ok = preferences.begin("sun_lazer", true);
    if (!ok) {
        if (h1_pct) *h1_pct = 0.0f;
        if (h2_pct) *h2_pct = 0.0f;
        return false;
    }
    float v1 = preferences.getFloat("cfg_h1_manip", 0.0f);
    float v2 = preferences.getFloat("cfg_h2_manip", 0.0f);
    if (v1 < -20.0f) v1 = -20.0f;
    if (v1 > 20.0f) v1 = 20.0f;
    if (v2 < -20.0f) v2 = -20.0f;
    if (v2 > 20.0f) v2 = 20.0f;
    if (h1_pct) *h1_pct = v1;
    if (h2_pct) *h2_pct = v2;
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

void resetAllToFactoryDefaults() {
    DEBUG_PRINTF("[NVS] Resetting all settings and recipes to factory defaults...\n");

    // 1. Reset all 10 recipes
    for (int i = 0; i < 10; i++) {
        memset(&recipes[i], 0, sizeof(ProgramRecipe_t));
        recipes[i].magic = RECIPE_MAGIC;
        recipes[i].version = RECIPE_VERSION;
        snprintf(recipes[i].name, sizeof(recipes[i].name), "Program %02d", i + 1);
        recipes[i].h1_setpoint_c = 50.0f;
        recipes[i].h2_setpoint_c = 50.0f;
        recipes[i].process_time_sec = 60;
        recipes[i].temp_tolerance_c = 2.0f;
        recipes[i].h1_temp_offset_pct = 0.0f;
        recipes[i].h2_temp_offset_pct = 0.0f;
        recipes[i].torque_unit = (uint8_t)TORQUE_UNIT_NM;
        recipes[i].h1_Kp = 20.0f; recipes[i].h1_Ki = 0.5f; recipes[i].h1_Kd = 1.0f;
        recipes[i].h2_Kp = 20.0f; recipes[i].h2_Ki = 0.5f; recipes[i].h2_Kd = 1.0f;
        saveRecipeToNVS(i);
    }

    // 2. Reset system settings
    sysStatus.start_mode_auto = true;
    saveStartModeToNVS(true);

    g_relayType = RELAY_TYPE_SSR;
    saveRelayTypeToNVS(RELAY_TYPE_SSR);

    sysStatus.active_program_idx = 0;
    saveActiveProgramToNVS(0);

    g_torqueUnit = TORQUE_UNIT_NM;
    saveTorqueUnitToNVS(TORQUE_UNIT_NM);

    g_h1_temp_manip_pct = 0.0f;
    g_h2_temp_manip_pct = 0.0f;
    saveTempManipToNVS(0.0f, 0.0f);

    sysStatus.down_limit_fail_count = 0;
    sysStatus.home_limit_fail_count = 0;
    sysStatus.last_down_limit_fail_ms = 0;
    sysStatus.last_home_limit_fail_ms = 0;

    DEBUG_PRINTF("[NVS] Factory reset complete.\n");
}