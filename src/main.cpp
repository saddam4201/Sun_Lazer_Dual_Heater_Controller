#include "config.h"
#include "debug_config.h"
#include "storage.h"
#include "display_ui.h"
#include "control_tasks.h"
#include "rtc.h"

// Define Global Objects
TFT_eSPI tft = TFT_eSPI();
Adafruit_MAX31865 max1 = Adafruit_MAX31865(PIN_MAX31865_CS1, PIN_VSPI_MOSI, PIN_VSPI_MISO, PIN_VSPI_SCK);
Adafruit_MAX31865 max2 = Adafruit_MAX31865(PIN_MAX31865_CS2, PIN_VSPI_MOSI, PIN_VSPI_MISO, PIN_VSPI_SCK);
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
    DEBUG_TP_INIT();

    // Actuator Pins
    pinMode(PIN_SSR_1, OUTPUT);
    pinMode(PIN_SSR_2, OUTPUT);
    pinMode(PIN_MOTOR_DOWN, OUTPUT);
    pinMode(PIN_MOTOR_UP, OUTPUT);
    digitalWrite(PIN_SSR_1, LOW);
    digitalWrite(PIN_SSR_2, LOW);
    digitalWrite(PIN_MOTOR_DOWN, LOW);
    digitalWrite(PIN_MOTOR_UP, LOW);

    // Sensor & Switch Inputs
    pinMode(PIN_DOWN_LIMIT, INPUT);
    pinMode(PIN_HOME_LIMIT, INPUT);
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    pinMode(PIN_BTN_OK, INPUT_PULLUP);
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);

    // Sync Structures
    xSemaphoreSPI = xSemaphoreCreateMutex();
    xLogQueue = xQueueCreate(10, 256);

    // Hardware SPI & Driver Setup
    SPI.begin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI);
    initDisplayAndWeb();

    max1.begin(MAX31865_3WIRE);
    max2.begin(MAX31865_3WIRE);

    torqueScale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);
    torqueScale.set_scale(42050.0f);
    torqueScale.tare();

    bool sd_ok = initStorageModules();
    loadRecipesFromNVS();
    bool rtc_ok = initRTC();

    // Boot-time diagnostics
    DEBUG_PRINTF("[BOOT] Debug TP GPIO: %d\n", DEBUG_TP_GPIO);
    DEBUG_PRINTF("[BOOT] SD present: %s\n", sd_ok ? "YES" : "NO");
    DEBUG_PRINTF("[BOOT] RTC present: %s\n", rtc_ok ? "YES" : "NO");

    // Ensure system status structure is zeroed to avoid transient garbage on boot
    memset(&sysStatus, 0, sizeof(sysStatus));

    sysStatus.currentState = STATE_IDLE;
    sysStatus.active_program_idx = 0;

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