#include "serial_display.h"
#if ENABLE_SERIAL_TFT

#include <Arduino.h>
#include "config.h"
#include "rtc.h"

void vd_init() {
    Serial.println("[VIRT_TFT] Serial virtual display initialized");
}

void vd_drawHomeScreen() {
    Serial.println("[VIRT_TFT] === HOME ===");
    Serial.printf("Program: %s\n", recipes[sysStatus.active_program_idx].name);
    Serial.printf("H1 Set/Act: %.1f / %.1f C\n", recipes[sysStatus.active_program_idx].h1_setpoint_c, sysStatus.h1_actual_c);
    Serial.printf("H2 Set/Act: %.1f / %.1f C\n", recipes[sysStatus.active_program_idx].h2_setpoint_c, sysStatus.h2_actual_c);
    Serial.printf("Torque Now/Max: %.2f / %.2f Nm\n", sysStatus.current_torque_nm, sysStatus.max_torque_nm);
    Serial.printf("Timer (s): %u\n", (unsigned)sysStatus.remaining_time_sec);
    Serial.printf("State: %s\n", stateNames[sysStatus.currentState]);
    if (sysStatus.currentState == STATE_ALARM_FAULT) Serial.printf("ALARM: %s\n", sysStatus.alarm_msg);
    Serial.println("[VIRT_TFT] =============");
}

void vd_drawProgramSelectScreen() {
    Serial.println("[VIRT_TFT] === PROGRAM SELECT ===");
    for (int i=0;i<10;i++) {
        Serial.printf("P%02d: %s - H1 %.1f C\n", i+1, recipes[i].name, recipes[i].h1_setpoint_c);
    }
    Serial.printf("Active program: P%02d\n", sysStatus.active_program_idx+1);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawProgramEditScreen() {
    Serial.println("[VIRT_TFT] === PROGRAM EDIT ===");
    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];
    Serial.printf("Name: %s\n", rec.name);
    Serial.printf("H1 Set: %.1f C\n", rec.h1_setpoint_c);
    Serial.printf("H2 Set: %.1f C\n", rec.h2_setpoint_c);
    Serial.printf("Time (s): %u\n", (unsigned)rec.process_time_sec);
    Serial.printf("Torque limit: %.2f Nm\n", rec.torque_limit_nm);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawTimerEditor() {
    Serial.println("[VIRT_TFT] === TIMER EDIT ===");
    uint32_t t = recipes[sysStatus.active_program_idx].process_time_sec;
    uint32_t h = t/3600;
    uint32_t m = (t%3600)/60;
    uint32_t s = t%60;
    Serial.printf("Program P%02d time: %02u:%02u:%02u (HH:MM:SS)\n", sysStatus.active_program_idx+1, (unsigned)h, (unsigned)m, (unsigned)s);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawServiceScreen() {
    Serial.println("[VIRT_TFT] === SERVICE ===");
    Serial.printf("Down limit: %s, Home limit: %s\n", sysStatus.down_limit_active?"ACTIVE":"OPEN", sysStatus.home_limit_active?"ACTIVE":"OPEN");
    Serial.printf("Torque now/max: %.2f / %.2f Nm\n", sysStatus.current_torque_nm, sysStatus.max_torque_nm);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawPIDTuningScreen() {
    Serial.println("[VIRT_TFT] === PID TUNING ===");
    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];
    Serial.printf("H1 Kp/Ki/Kd: %.2f / %.2f / %.2f\n", rec.h1_Kp, rec.h1_Ki, rec.h1_Kd);
    Serial.printf("H2 Kp/Ki/Kd: %.2f / %.2f / %.2f\n", rec.h2_Kp, rec.h2_Ki, rec.h2_Kd);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawRTCSetScreen() {
    Serial.println("[VIRT_TFT] === RTC SET (virtual) ===");
    // show current system build time as fallback
    char buf[64];
    getTimestampForLog(buf, sizeof(buf));
    Serial.printf("Current time: %s\n", buf);
    Serial.println("[VIRT_TFT] ====================");
}

void vd_popup(const char* msg) {
    Serial.printf("[VIRT_TFT] POPUP: %s\n", msg);
}

#endif // ENABLE_SERIAL_TFT
