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

void logToSD(const char* logEntry);

// Recent logs in RAM
void pushRecentLog(const char* csvLine);
const char* getRecentLog(uint8_t idx); // 0..LOG_RECENT_COUNT-1, 0 = newest

#endif // STORAGE_H