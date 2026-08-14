#include "serial_display.h"
#if ENABLE_SERIAL_TFT

#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "rtc.h"

void vd_init() {
    Serial.println("[VIRT_TFT] Serial virtual display initialized");
}

void vd_drawHomeScreen() {
    // Draw only when content on the virtual TFT changes to avoid flooding Serial.
    static int last_prog_idx = -1;
    static float last_h1_set = NAN, last_h1_act = NAN;
    static float last_h2_set = NAN, last_h2_act = NAN;
    static float last_torque = NAN, last_max_torque = NAN;
    static uint32_t last_remaining = 0xFFFFFFFF;
    static ProcessState_t lastState = (ProcessState_t)0xFF;
    static bool last_boot_ok = true;
    static char last_boot_msg[128] = "";
    static bool last_force_pending = false;
    static uint32_t last_force_remaining = 0xFFFFFFFF;

    bool changed = false;

    // Program change
    if (sysStatus.active_program_idx != last_prog_idx) {
        changed = true;
    }

#if ENABLE_H1
    // H1 changes (compare to 0.1C resolution)
    if (isnan(last_h1_set) || fabsf(recipes[sysStatus.active_program_idx].h1_setpoint_c - last_h1_set) > 0.05f) changed = true;
    if (isnan(last_h1_act) || fabsf(sysStatus.h1_actual_c - last_h1_act) > 0.05f) changed = true;
#else
    // If disabled, print when we transition into/out of disabled state
    if (!ENABLE_H1 && !isnan(last_h1_set)) changed = true;
#endif

#if ENABLE_H2
    if (isnan(last_h2_set) || fabsf(recipes[sysStatus.active_program_idx].h2_setpoint_c - last_h2_set) > 0.05f) changed = true;
    if (isnan(last_h2_act) || fabsf(sysStatus.h2_actual_c - last_h2_act) > 0.05f) changed = true;
#else
    if (!ENABLE_H2 && !isnan(last_h2_set)) changed = true;
#endif

    // Torque changes (small epsilon)
    if (isnan(last_torque) || fabsf(sysStatus.current_torque_nm - last_torque) > 0.01f) changed = true;
    if (isnan(last_max_torque) || fabsf(sysStatus.max_torque_nm - last_max_torque) > 0.01f) changed = true;

    // Timer change
    if (sysStatus.remaining_time_sec != last_remaining) changed = true;

    // State change
    if (sysStatus.currentState != lastState) changed = true;

    // Boot status change
    if (sysStatus.boot_ok != last_boot_ok) changed = true;
    if (!sysStatus.boot_ok && strcmp(sysStatus.boot_msg, last_boot_msg) != 0) changed = true;

    // Force-start pending or its countdown changed
    uint32_t force_remaining = 0;
    if (sysStatus.forceStartPending) {
        force_remaining = (sysStatus.forceStartUntilMs > millis()) ? (sysStatus.forceStartUntilMs - millis())/1000 : 0;
    }
    if (sysStatus.forceStartPending != last_force_pending) changed = true;
    if (sysStatus.forceStartPending && force_remaining != last_force_remaining) changed = true;

    if (!changed) return; // nothing changed since last draw

    // Print full snapshot when anything changed
    Serial.println("[VIRT_TFT] === HOME SNAPSHOT ===");
    Serial.printf("Program: %s\n", recipes[sysStatus.active_program_idx].name);
#if ENABLE_H1
    Serial.printf("H1 Set/Act: %.1f / %.1f C\n", recipes[sysStatus.active_program_idx].h1_setpoint_c, sysStatus.h1_actual_c);
#else
    Serial.println("H1: DISABLED");
#endif
#if ENABLE_H2
    Serial.printf("H2 Set/Act: %.1f / %.1f C\n", recipes[sysStatus.active_program_idx].h2_setpoint_c, sysStatus.h2_actual_c);
#else
    Serial.println("H2: DISABLED");
#endif
    Serial.printf("Torque Now/Max: %.2f / %.2f Nm\n", sysStatus.current_torque_nm, sysStatus.max_torque_nm);

    // Timer display
    if (sysStatus.remaining_time_sec > 0) {
        uint32_t mm = sysStatus.remaining_time_sec / 60;
        uint32_t ss = sysStatus.remaining_time_sec % 60;
        Serial.printf("Timer: %02u:%02u (s=%u)\n", (unsigned)mm, (unsigned)ss, (unsigned)sysStatus.remaining_time_sec);
    } else {
        Serial.println("Timer: --:--");
    }

    // Status
    Serial.printf("State: %s\n", stateNames[sysStatus.currentState]);
    if (sysStatus.currentState == STATE_ALARM_FAULT) Serial.printf("ALARM: %s\n", sysStatus.alarm_msg);

    // Boot status
    if (!sysStatus.boot_ok) {
        Serial.printf("BOOT ERROR: %s\n", sysStatus.boot_msg);
    }

    // Force-start pending
    if (sysStatus.forceStartPending) {
        Serial.printf("FORCE START PENDING: confirm with OK or cancel with LEFT (%us)\n", (unsigned)force_remaining);
        Serial.printf("[VIRT_TFT] FORCE START PENDING: Press 5 (OK) to confirm or 3 (LEFT) to cancel (%us)\n", (unsigned)force_remaining);
    }

    Serial.println("[VIRT_TFT] ====================");

    // Update last snapshot
    last_prog_idx = sysStatus.active_program_idx;
#if ENABLE_H1
    last_h1_set = recipes[sysStatus.active_program_idx].h1_setpoint_c;
    last_h1_act = sysStatus.h1_actual_c;
#else
    last_h1_set = NAN;
    last_h1_act = NAN;
#endif
#if ENABLE_H2
    last_h2_set = recipes[sysStatus.active_program_idx].h2_setpoint_c;
    last_h2_act = sysStatus.h2_actual_c;
#else
    last_h2_set = NAN;
    last_h2_act = NAN;
#endif
    last_torque = sysStatus.current_torque_nm;
    last_max_torque = sysStatus.max_torque_nm;
    last_remaining = sysStatus.remaining_time_sec;
    lastState = sysStatus.currentState;
    last_boot_ok = sysStatus.boot_ok;
    strncpy(last_boot_msg, sysStatus.boot_msg, sizeof(last_boot_msg)-1);
    last_boot_msg[sizeof(last_boot_msg)-1] = '\0';
    last_force_pending = sysStatus.forceStartPending;
    last_force_remaining = force_remaining;
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
    Serial.printf("DN Failures: %u, HOME Failures: %u\n", (unsigned)sysStatus.down_limit_fail_count, (unsigned)sysStatus.home_limit_fail_count);
    Serial.printf("Torque now/max: %.2f / %.2f Nm\n", sysStatus.current_torque_nm, sysStatus.max_torque_nm);
    Serial.println("[VIRT_TFT] Press 4+2 (DN+RIGHT) to reset failure counters");
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
