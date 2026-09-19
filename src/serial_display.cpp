#include "serial_display.h"
#if ENABLE_SERIAL_TFT

#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "rtc.h"
#include "display_ui.h"

void vd_init() {
    Serial.println("[VIRT_TFT] Serial virtual display initialized");
    Serial.println("[VIRT_TFT] Button Mapping: 1=UP, 2=DOWN, 3=LEFT (<-), 4=RIGHT (->), 5=START/STOP");
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
    static UIScreen_t last_screen = (UIScreen_t)0xFF;

    bool changed = false;

    if (currentScreen != last_screen) changed = true;

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
    Serial.println("[VIRT_TFT] === HOME SCREEN ===");
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
    Serial.printf("State: %s | Mode: %s\n", stateNames[sysStatus.currentState], sysStatus.start_mode_auto ? "AUTO" : "MANUAL");
    Serial.printf("Switches: Down Sw: %s | Home Sw: %s\n",
                  sysStatus.down_limit_active ? "CLOSED" : "OPEN",
                  sysStatus.home_limit_active ? "CLOSED" : "OPEN");
    if (sysStatus.currentState == STATE_ALARM_FAULT) Serial.printf("ALARM: %s\n", sysStatus.alarm_msg);

    // Boot status
    if (!sysStatus.boot_ok) {
        Serial.printf("BOOT ERROR: %s\n", sysStatus.boot_msg);
    }

    // Force-start pending
    if (sysStatus.forceStartPending) {
        Serial.printf("[VIRT_TFT] FORCE START PENDING (%us): Press [5/START/STOP] to Force Start or [3/<-] to Cancel\n", (unsigned)force_remaining);
    } else {
        Serial.println("[VIRT_TFT] Nav: [5/START/STOP]: Start/Stop Process | [4/->]: Settings Menu");
    }

    Serial.println("[VIRT_TFT] ====================");

    // Update last snapshot
    last_screen = currentScreen;
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
    static int last_prog_idx = -1;
    static UIScreen_t last_screen = (UIScreen_t)0xFF;
    if (currentScreen == last_screen && sysStatus.active_program_idx == last_prog_idx) return;
    last_prog_idx = sysStatus.active_program_idx;
    last_screen = currentScreen;

    Serial.println("[VIRT_TFT] === PROGRAM SELECT ===");
    for (int i = 0; i < 10; i++) {
        const char *cursor = (sysStatus.active_program_idx == i) ? "-> " : "   ";
        Serial.printf("%s%-16s | H1: %.1f C | H2: %.1f C | Time: %us\n",
                      cursor, recipes[i].name, recipes[i].h1_setpoint_c, recipes[i].h2_setpoint_c, (unsigned)recipes[i].process_time_sec);
    }
    Serial.printf("Active program: %s\n", recipes[sysStatus.active_program_idx].name);
    Serial.println("[VIRT_TFT] Nav: [1/UP, 2/DN]: Select Recipe | [4/->]: Edit Recipe | [3/<-]: Back to Home");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawProgramEditScreen() {
    static int last_prog_idx = -1;
    static float last_h1 = NAN, last_h2 = NAN, last_tol = NAN, last_o1 = NAN, last_o2 = NAN;
    static uint32_t last_time = 0xFFFFFFFF;
    static uint8_t last_unit = 0xFF;
    static UIScreen_t last_screen = (UIScreen_t)0xFF;

    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];
    bool changed = (currentScreen != last_screen) ||
                   (sysStatus.active_program_idx != last_prog_idx) ||
                   (rec.h1_setpoint_c != last_h1) ||
                   (rec.h2_setpoint_c != last_h2) ||
                   (rec.process_time_sec != last_time) ||
                   (rec.temp_tolerance_c != last_tol) ||
                   (rec.h1_temp_offset_pct != last_o1) ||
                   (rec.h2_temp_offset_pct != last_o2) ||
                   (rec.torque_unit != last_unit);

    if (!changed) return;
    last_screen = currentScreen;
    last_prog_idx = sysStatus.active_program_idx;
    last_h1 = rec.h1_setpoint_c;
    last_h2 = rec.h2_setpoint_c;
    last_time = rec.process_time_sec;
    last_tol = rec.temp_tolerance_c;
    last_o1 = rec.h1_temp_offset_pct;
    last_o2 = rec.h2_temp_offset_pct;
    last_unit = rec.torque_unit;

    Serial.printf("[VIRT_TFT] === PROGRAM EDIT (%s) ===\n", rec.name);
    Serial.printf("  1. H1 Target Temp : %.1f C\n", rec.h1_setpoint_c);
    Serial.printf("  2. H2 Target Temp : %.1f C\n", rec.h2_setpoint_c);
    Serial.printf("  3. Process Time   : %u s\n", (unsigned)rec.process_time_sec);
    Serial.printf("  4. Temp Tolerance : %+.1f C\n", rec.temp_tolerance_c);
    Serial.printf("  5. H1 Offset %%    : %+.1f %%\n", rec.h1_temp_offset_pct);
    Serial.printf("  6. H2 Offset %%    : %+.1f %%\n", rec.h2_temp_offset_pct);
    Serial.printf("  7. Torque Unit    : %s\n", getTorqueUnitName((TorqueUnit_t)rec.torque_unit));
    Serial.println("[VIRT_TFT] Nav: [1/UP, 2/DN]: Move | [4/->]: Edit Field | [3/<-]: Save & Exit");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawTimerEditor() {
    static uint32_t last_time = 0xFFFFFFFF;
    static int last_prog = -1;
    static UIScreen_t last_screen = (UIScreen_t)0xFF;

    uint32_t t = recipes[sysStatus.active_program_idx].process_time_sec;
    if (currentScreen == last_screen && t == last_time && sysStatus.active_program_idx == last_prog) return;
    last_screen = currentScreen;
    last_time = t;
    last_prog = sysStatus.active_program_idx;

    uint32_t h = t / 3600;
    uint32_t m = (t % 3600) / 60;
    uint32_t s = t % 60;
    Serial.printf("[VIRT_TFT] === TIMER EDIT (P%02d: %s) ===\n", sysStatus.active_program_idx + 1, recipes[sysStatus.active_program_idx].name);
    Serial.printf("  Process Time: %02u:%02u:%02u (HH:MM:SS) [%u total seconds]\n", (unsigned)h, (unsigned)m, (unsigned)s, (unsigned)t);
    Serial.println("[VIRT_TFT] Nav: [1/UP, 2/DN]: Change | [4/->]: Next (HH->MM->SS) | [5/OK]: Save | [3/<-]: Cancel");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawServiceScreen() {
    static bool last_down = false, last_home = false, last_auto = false;
    static uint32_t last_dn_fail = 0xFFFFFFFF, last_hm_fail = 0xFFFFFFFF;
    static float last_torque = NAN, last_max_torque = NAN;
    static UIScreen_t last_screen = (UIScreen_t)0xFF;

    bool changed = (currentScreen != last_screen) ||
                   (sysStatus.down_limit_active != last_down) ||
                   (sysStatus.home_limit_active != last_home) ||
                   (sysStatus.start_mode_auto != last_auto) ||
                   (sysStatus.down_limit_fail_count != last_dn_fail) ||
                   (sysStatus.home_limit_fail_count != last_hm_fail) ||
                   (fabsf(sysStatus.current_torque_nm - last_torque) > 0.05f) ||
                   (fabsf(sysStatus.max_torque_nm - last_max_torque) > 0.05f);

    if (!changed) return;
    last_screen = currentScreen;
    last_down = sysStatus.down_limit_active;
    last_home = sysStatus.home_limit_active;
    last_auto = sysStatus.start_mode_auto;
    last_dn_fail = sysStatus.down_limit_fail_count;
    last_hm_fail = sysStatus.home_limit_fail_count;
    last_torque = sysStatus.current_torque_nm;
    last_max_torque = sysStatus.max_torque_nm;

    Serial.println("[VIRT_TFT] === SERVICE DIAGNOSTICS ===");
    Serial.printf("  Down Limit Sw : %s | Home Limit Sw : %s\n",
                  sysStatus.down_limit_active ? "ACTIVE" : "OPEN",
                  sysStatus.home_limit_active ? "ACTIVE" : "OPEN");
    Serial.printf("  Limit Failures: DN Fail: %u | HM Fail: %u\n",
                  (unsigned)sysStatus.down_limit_fail_count, (unsigned)sysStatus.home_limit_fail_count);
    Serial.printf("  Torque Sensor : Now: %.2f Nm | Max: %.2f Nm\n",
                  sysStatus.current_torque_nm, sysStatus.max_torque_nm);
    Serial.printf("  Start Mode    : %s\n", sysStatus.start_mode_auto ? "AUTO" : "MANUAL");
    Serial.println("[VIRT_TFT] Actions: [5/OK]: Heater Test | [1/UP]: Motor Jog UP | [4/->]: Motor Jog DN | [2/DN]: Toggle Mode");
    Serial.println("[VIRT_TFT] Combos : [4+5/->+OK]: PID Tuning | [2+5/DN+OK]: Set RTC | [2+4/DN+->]: Reset Fails | [3+5/<-+OK]: Save Defaults");
    Serial.println("[VIRT_TFT] Nav    : [3/<-]: Back to Home");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawPIDTuningScreen() {
    static int last_prog = -1;
    static float last_h1_kp = NAN, last_h1_ki = NAN, last_h1_kd = NAN;
    static float last_h2_kp = NAN, last_h2_ki = NAN, last_h2_kd = NAN;
    static UIScreen_t last_screen = (UIScreen_t)0xFF;

    ProgramRecipe_t &rec = recipes[sysStatus.active_program_idx];
    bool changed = (currentScreen != last_screen) ||
                   (sysStatus.active_program_idx != last_prog) ||
                   (rec.h1_Kp != last_h1_kp) || (rec.h1_Ki != last_h1_ki) || (rec.h1_Kd != last_h1_kd) ||
                   (rec.h2_Kp != last_h2_kp) || (rec.h2_Ki != last_h2_ki) || (rec.h2_Kd != last_h2_kd);

    if (!changed) return;
    last_screen = currentScreen;
    last_prog = sysStatus.active_program_idx;
    last_h1_kp = rec.h1_Kp; last_h1_ki = rec.h1_Ki; last_h1_kd = rec.h1_Kd;
    last_h2_kp = rec.h2_Kp; last_h2_ki = rec.h2_Ki; last_h2_kd = rec.h2_Kd;

    Serial.printf("[VIRT_TFT] === PID TUNING (P%02d: %s) ===\n", sysStatus.active_program_idx + 1, rec.name);
    Serial.printf("  H1 PID: Kp=%.2f, Ki=%.3f, Kd=%.2f\n", rec.h1_Kp, rec.h1_Ki, rec.h1_Kd);
    Serial.printf("  H2 PID: Kp=%.2f, Ki=%.3f, Kd=%.2f\n", rec.h2_Kp, rec.h2_Ki, rec.h2_Kd);
    Serial.println("[VIRT_TFT] Nav: [1/UP, 2/DN]: Change Value | [4/->]: Next Field | [5/OK]: Save | [3/<-]: Back to Service");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_drawRTCSetScreen() {
    static UIScreen_t last_screen = (UIScreen_t)0xFF;
    if (currentScreen == last_screen) return;
    last_screen = currentScreen;

    char buf[64];
    getTimestampForLog(buf, sizeof(buf));
    Serial.println("[VIRT_TFT] === RTC SETTINGS ===");
    Serial.printf("  Current System Time: %s\n", buf);
    Serial.println("[VIRT_TFT] Nav: [1/UP, 2/DN]: Change Value | [4/->]: Next Field | [5/OK]: Save RTC | [3/<-]: Cancel");
    Serial.println("[VIRT_TFT] ====================");
}

void vd_popup(const char* msg) {
    Serial.printf("[VIRT_TFT] POPUP: %s\n", msg);
}

#endif // ENABLE_SERIAL_TFT
