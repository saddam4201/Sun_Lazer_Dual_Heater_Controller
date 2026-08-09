#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

void initStorageModules();
void loadRecipesFromNVS();
void saveRecipeToNVS(uint8_t index);
void logToSD(const char* logEntry);

#endif // STORAGE_H