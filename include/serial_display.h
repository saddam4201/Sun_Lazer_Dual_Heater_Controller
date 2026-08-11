#ifndef SERIAL_DISPLAY_H
#define SERIAL_DISPLAY_H

#include "config.h"

#if ENABLE_SERIAL_TFT

void vd_init();
void vd_drawHomeScreen();
void vd_drawProgramSelectScreen();
void vd_drawProgramEditScreen();
void vd_drawTimerEditor();
void vd_drawServiceScreen();
void vd_drawPIDTuningScreen();
void vd_drawRTCSetScreen();
void vd_popup(const char* msg);

#else

static inline void vd_init() {}
static inline void vd_drawHomeScreen() {}
static inline void vd_drawProgramSelectScreen() {}
static inline void vd_drawProgramEditScreen() {}
static inline void vd_drawTimerEditor() {}
static inline void vd_drawServiceScreen() {}
static inline void vd_drawPIDTuningScreen() {}
static inline void vd_drawRTCSetScreen() {}
static inline void vd_popup(const char* msg) {}

#endif // ENABLE_SERIAL_TFT

#endif // SERIAL_DISPLAY_H