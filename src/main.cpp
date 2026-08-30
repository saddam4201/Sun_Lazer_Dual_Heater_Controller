#include "config.h"
#include "debug_config.h"
#include "storage.h"
#include "display_ui.h"
#include "control_tasks.h"
#include "rtc.h"
#include "gpio_safe.h"
#if ENABLE_SERIAL_TFT
#include "serial_display.h"
#endif


static bool performStartupChecks(char *reason, size_t len) {
    if (len == 0) return false;
    reason[0] = '\0';

    // Basic SPI sensor checks
#if ENABLE_MAX31865
  #if INPUT_SERIAL_SIMULATOR
    // In serial-simulator mode, avoid touching real SPI sensors — assume they will be simulated via terminal.
    // Provide safe default readings so startup checks pass: set to active program setpoints.
    (void)xSemaphoreSPI;
    float t1 = recipes[sysStatus.active_program_idx].h1_setpoint_c;
    float t2 = recipes[sysStatus.active_program_idx].h2_setpoint_c;
    (void)t1; (void)t2; // used only to avoid unused warning if needed
  #else
    if (xSemaphoreTake(xSemaphoreSPI, pdMS_TO_TICKS(500)) == pdTRUE) {
        uint8_t f1 = 0, f2 = 0;
        float t1 = readPT100Temperature(max1, f1);
        float t2 = readPT100Temperature(max2, f2);
        xSemaphoreGive(xSemaphoreSPI);

  #if ENABLE_H1
        if (isnan(t1) || t1 < -10.0f) {
            snprintf(reason, len, "H1 PT100 fault (code 0x%02X, read %.1f C)", f1, isnan(t1) ? 0.0f : t1);
            return false;
        }
  #endif
  #if ENABLE_H2
        if (isnan(t2) || t2 < -10.0f) {
            snprintf(reason, len, "H2 PT100 fault (code 0x%02X, read %.1f C)", f2, isnan(t2) ? 0.0f : t2);
            return false;
        }
  #endif
    } else {
        snprintf(reason, len, "SPI mutex timeout while checking sensors");
        return false;
    }
  #endif
#else
    // MAX31865 disabled: emulate sensors (no failure)
    // Use active program setpoints as emulated sensor values
    (void)xSemaphoreSPI; // quiet unused
#endif

    // Torque sensor
#if ENABLE_HX711
    if (!torqueScale.is_ready()) {
        snprintf(reason, len, "Torque sensor not ready");
        return false;
    }
#else
    // HX711 disabled: simulate torque readiness
    // do not fail boot; note in boot_msg if desired
#endif

    // Validate critical GPIOs
    int criticalPins[] = { PIN_SSR_1, PIN_SSR_2, PIN_MOTOR_DOWN, PIN_MOTOR_UP, PIN_VSPI_SCK };
    for (size_t i = 0; i < sizeof(criticalPins)/sizeof(criticalPins[0]); ++i) {
        if (!gpio_is_valid_number(criticalPins[i])) {
            snprintf(reason, len, "Invalid GPIO mapping: pin %d", criticalPins[i]);
            return false;
        }
    }

    // All checks passed
    reason[0] = '\0';
    return true;
}

// Define Global Objects
TFT_eSPI tft = TFT_eSPI();
Adafruit_MAX31865 max1 = Adafruit_MAX31865(PIN_MAX31865_CS1, &SPI);
Adafruit_MAX31865 max2 = Adafruit_MAX31865(PIN_MAX31865_CS2, &SPI);
HX711 torqueScale;
Preferences preferences;
WebServer webServer(80);

SemaphoreHandle_t xSemaphoreSPI = NULL;
QueueHandle_t xLogQueue = NULL;

ProgramRecipe_t recipes[10];
SystemStatus_t sysStatus;

const char* stateNames[] = {
    "NONE", "IDLE", "SAFETY_CHECK", "MOVE_DOWN", "DOWN_LIMIT",
    "HEAT_TO_SETPOINT", "TEMP_READY", "PROCESS_TIMER", "TIMER_COMPLETE",
    "MOVE_UP", "HOME_LIMIT", "SAVE_RECORD", "PROCESS_COMPLETE", "READY", "ALARM_FAULT"
};

void setup() {
    DEBUG_INIT(115200);
#if ENABLE_SERIAL_TFT || INPUT_USE_SERIAL
    // Ensure Serial is available for virtual display and/or serial input
    Serial.begin(115200);
#endif

    // Deselect all SPI devices initially to prevent bus contention
    safePinMode(PIN_TFT_CS, OUTPUT);
    safeDigitalWrite(PIN_TFT_CS, HIGH);
    safePinMode(PIN_SD_CS, OUTPUT);
    safeDigitalWrite(PIN_SD_CS, HIGH);
    safePinMode(PIN_MAX31865_CS1, OUTPUT);
    safeDigitalWrite(PIN_MAX31865_CS1, HIGH);
    safePinMode(PIN_MAX31865_CS2, OUTPUT);
    safeDigitalWrite(PIN_MAX31865_CS2, HIGH);

    // Actuator Pins (safe-checked)
    safePinMode(PIN_SSR_1, OUTPUT);
    safePinMode(PIN_SSR_2, OUTPUT);
    safePinMode(PIN_MOTOR_DOWN, OUTPUT);
    safePinMode(PIN_MOTOR_UP, OUTPUT);
    safeDigitalWrite(PIN_SSR_1, LOW);
    safeDigitalWrite(PIN_SSR_2, LOW);
    safeDigitalWrite(PIN_MOTOR_DOWN, LOW);
    safeDigitalWrite(PIN_MOTOR_UP, LOW);

    // Sensor & Switch Inputs (safe-checked)
    safePinMode(PIN_DOWN_LIMIT, INPUT_PULLUP);
    safePinMode(PIN_HOME_LIMIT, INPUT_PULLUP);
#if ENABLE_PHYSICAL_BUTTONS
    safePinMode(PIN_BTN_UP, INPUT_PULLUP);
    safePinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    safePinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    safePinMode(PIN_BTN_OK, INPUT_PULLUP);
    safePinMode(PIN_BTN_LEFT, INPUT_PULLUP);
#endif

    // Sync Structures
    xSemaphoreSPI = xSemaphoreCreateMutex();
    xLogQueue = xQueueCreate(10, 256);

    // Hardware SPI & Driver Setup
    SPI.begin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI);
    initDisplayAndWeb();

    // Initialize MAX31865 RTD sensors in Continuous Conversion mode with 50Hz mains filter
    max1.begin(MAX31865_WIRE_MODE);
    max1.enableBias(true);
    max1.autoConvert(true);
    max1.enable50Hz(true);
    max1.clearFault();

    max2.begin(MAX31865_WIRE_MODE);
    max2.enableBias(true);
    max2.autoConvert(true);
    max2.enable50Hz(true);
    max2.clearFault();

    // Settle time for continuous conversions
    delay(100);

    torqueScale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);
    torqueScale.set_scale(42050.0f);
    torqueScale.tare();

    bool sd_ok = initStorageModules();
    loadRecipesFromNVS();
    bool rtc_ok = initRTC();

    // Boot-time diagnostics
    DEBUG_PRINTF("[BOOT] Debug TP: disabled\n");
    DEBUG_PRINTF("[BOOT] SD present: %s\n", sd_ok ? "YES" : "NO");
    DEBUG_PRINTF("[BOOT] RTC present: %s\n", rtc_ok ? "YES" : "NO");

#if ENABLE_SERIAL_TFT || INPUT_USE_SERIAL
    // Print a concise boot summary of enabled/disabled modules to Serial (useful for testing)
    Serial.println("Developer: Saddam Khan");
    Serial.println("[BOOT] Module summary:");
    Serial.printf("  Virtual TFT: %s\n", (ENABLE_SERIAL_TFT ? "ENABLED" : "DISABLED"));
    Serial.printf("  Physical buttons: %s\n", (ENABLE_PHYSICAL_BUTTONS ? "ENABLED" : "DISABLED"));
    Serial.printf("  Serial input (button emulation): %s\n", (INPUT_USE_SERIAL ? "ENABLED" : "DISABLED"));
    Serial.printf("  MAX31865 sensors: %s\n", (ENABLE_MAX31865 ? "ENABLED" : "DISABLED"));
    Serial.printf("  HX711 torque sensor: %s\n", (ENABLE_HX711 ? "ENABLED" : "DISABLED"));
    Serial.printf("  Heater H1 channel: %s\n", (ENABLE_H1 ? "ENABLED" : "DISABLED"));
    Serial.printf("  Heater H2 channel: %s\n", (ENABLE_H2 ? "ENABLED" : "DISABLED"));
    Serial.printf("  SD card support: %s\n", (ENABLE_SD_CARD ? "ENABLED" : "DISABLED"));
    Serial.printf("  RTC support: %s\n", (ENABLE_RTC ? "ENABLED" : "DISABLED"));
    Serial.printf("  App Remote API: %s\n", (ENABLE_APP_REMOTE ? "ENABLED" : "DISABLED"));
#endif

    // Ensure system status structure is zeroed to avoid transient garbage on boot
    memset(&sysStatus, 0, sizeof(sysStatus));

    sysStatus.currentState = STATE_IDLE;
    
    // Load last selected active program from NVS (default: 0)
    uint8_t savedProg = 0;
    if (loadActiveProgramFromNVS(&savedProg) && savedProg < 10) {
        sysStatus.active_program_idx = savedProg;
    } else {
        sysStatus.active_program_idx = 0;
    }
    DEBUG_PRINTF("[BOOT] Active Program: P%02d (%s)\n", sysStatus.active_program_idx + 1, recipes[sysStatus.active_program_idx].name);
    
    // Initialize limit switch failure tracking
    sysStatus.down_limit_fail_count = 0;
    sysStatus.home_limit_fail_count = 0;
    sysStatus.last_down_limit_fail_ms = 0;
    sysStatus.last_home_limit_fail_ms = 0;

    // Load persisted start mode setting (default: auto)
    bool startMode = true;
    loadStartModeFromNVS(&startMode);
    sysStatus.start_mode_auto = startMode;

#if INPUT_SERIAL_SIMULATOR
    // Provide safe default simulated sensor values for terminal-only testing (room temperature 25.0C)
    sysStatus.h1_actual_c = 25.0f;
    sysStatus.h2_actual_c = 25.0f;
    sysStatus.current_torque_nm = 0.0f;
    sysStatus.down_limit_active = false;
    sysStatus.home_limit_active = false;
#endif

    // Perform startup health checks and set boot status/msg for display
    char bootReason[128];
    bool ok = performStartupChecks(bootReason, sizeof(bootReason));
    sysStatus.boot_ok = ok;
    if (!ok) {
        strncpy(sysStatus.boot_msg, bootReason, sizeof(sysStatus.boot_msg) - 1);
        sysStatus.boot_msg[sizeof(sysStatus.boot_msg) - 1] = '\0';
        // also copy into alarm_msg for immediate visibility
        strncpy(sysStatus.alarm_msg, bootReason, sizeof(sysStatus.alarm_msg) - 1);
        sysStatus.alarm_msg[sizeof(sysStatus.alarm_msg) - 1] = '\0';
#if ENABLE_SERIAL_TFT
        vd_popup(sysStatus.boot_msg);
#else
        DEBUG_PRINTF("[BOOT] %s\n", sysStatus.boot_msg);
#endif
    } else {
        sysStatus.boot_msg[0] = '\0';
#if ENABLE_SERIAL_TFT
        vd_popup("System ready. Press OK to start process.");
#else
        DEBUG_PRINTF("[BOOT] System ready.\n");
#endif
    }

    // Core 1 Real-time Tasks
    xTaskCreatePinnedToCore(Task_SafetyAndControl, "SafetyTask", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_TemperaturePID,  "PIDTask",    4096, NULL, 2, NULL, 1);

    // Core 0 Async Tasks
    xTaskCreatePinnedToCore(Task_UIAndWeb,        "UITask",     8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(Task_Logger,          "LogTask",    4096, NULL, 1, NULL, 0);
}


void loop() {
    vTaskDelete(NULL);
}