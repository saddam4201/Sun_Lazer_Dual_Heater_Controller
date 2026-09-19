#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

bool initStorageModules();
void loadRecipesFromNVS();
void saveRecipeToNVS(uint8_t index);
void saveAllRecipesToNVS();

// Start mode persistence
void saveStartModeToNVS(bool autoMode);
bool loadStartModeFromNVS(bool *autoMode);

// Active program persistence
void saveActiveProgramToNVS(uint8_t index);
bool loadActiveProgramFromNVS(uint8_t *index);

// Settings persistence
void saveRelayTypeToNVS(RelayType_t relayType);
bool loadRelayTypeFromNVS(RelayType_t *relayType);

void saveTorqueUnitToNVS(TorqueUnit_t unit);
bool loadTorqueUnitFromNVS(TorqueUnit_t *unit);

void saveTempManipToNVS(float h1_pct, float h2_pct);
bool loadTempManipFromNVS(float *h1_pct, float *h2_pct);

void logToSD(const char* logEntry);
void resetAllToFactoryDefaults();

// Recent logs in RAM
void pushRecentLog(const char* csvLine);
const char* getRecentLog(uint8_t idx); // 0..LOG_RECENT_COUNT-1, 0 = newest

#endif // STORAGE_H