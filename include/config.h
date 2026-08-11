#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Adafruit_MAX31865.h>
#include <HX711.h>
#include <Preferences.h>
#include <WebServer.h>

// VSPI Pins
#define PIN_VSPI_SCK     18
#define PIN_VSPI_MISO    19
#define PIN_VSPI_MOSI    23

// TFT & SD Chip Selects
#define PIN_TFT_CS        5
#define PIN_TFT_DC        2
#define PIN_TFT_RST       4
#define PIN_SD_CS        13

// Enable SD card support (set to 1 to enable SD logging and file operations)
#ifndef ENABLE_SD_CARD
#define ENABLE_SD_CARD 0
#endif

// Enable RTC support (set to 1 to enable DS3231 RTC features)
#ifndef ENABLE_RTC
#define ENABLE_RTC 0
#endif

// Enable a virtual TFT over Serial (for testing without a display)
#ifndef ENABLE_SERIAL_TFT
#define ENABLE_SERIAL_TFT 1
#endif

// Sensor CS Pins
#define PIN_MAX31865_CS1 14
#define PIN_MAX31865_CS2 15

// Torque Sensor Pins (TQ10 via HX711)
#define PIN_HX711_DOUT   36
#define PIN_HX711_SCK    12

// Limit Switches - Active LOW (External 10k pull-ups required on GPIO 34/35)
#define PIN_DOWN_LIMIT   34
#define PIN_HOME_LIMIT   35

// Push Buttons
#define PIN_BTN_UP       32
#define PIN_BTN_DOWN     33
#define PIN_BTN_RIGHT    25
#define PIN_BTN_OK       26
#define PIN_BTN_LEFT     27

// SSR & Motor Actuator Outputs
#define PIN_SSR_1        16
#define PIN_SSR_2        17
#define PIN_MOTOR_DOWN   21
#define PIN_MOTOR_UP     22

// PT100 Constants
#define RREF      430.0f
#define RNOMINAL  100.0f

// Recipe storage validation
#define RECIPE_MAGIC    0xABCD
#define RECIPE_VERSION  1

// 13-State Machine Definition
typedef enum {
    STATE_IDLE = 1,
    STATE_SAFETY_CHECK,
    STATE_MOVE_DOWN,
    STATE_DOWN_LIMIT,
    STATE_HEAT_TO_SETPOINT,
    STATE_TEMPERATURE_READY,
    STATE_PROCESS_TIMER,
    STATE_TIMER_COMPLETE,
    STATE_MOVE_UP,
    STATE_HOME_LIMIT,
    STATE_SAVE_RECORD,
    STATE_PROCESS_COMPLETE,
    STATE_READY,
    STATE_ALARM_FAULT
} ProcessState_t;

// Recipe Format for 10 Programs
struct ProgramRecipe_t {
    uint16_t magic;            // validation magic
    uint8_t  version;          // storage struct version
    char name[16];
    float h1_setpoint_c;
    float h2_setpoint_c;
    uint32_t process_time_sec;
    float torque_limit_nm;
    float temp_tolerance_c;

    // Per-program PID tunings
    float h1_Kp;
    float h1_Ki;
    float h1_Kd;
    float h2_Kp;
    float h2_Ki;
    float h2_Kd;
};

// Global Real-Time System Status Data
struct SystemStatus_t {
    ProcessState_t currentState;
    float h1_actual_c;
    float h2_actual_c;
    float current_torque_nm;
    float max_torque_nm;          // maximum torque observed during current run
    uint32_t remaining_time_sec;
    bool down_limit_active;
    bool home_limit_active;
    bool motor_down_running;
    bool motor_up_running;
    uint8_t active_program_idx;
    char alarm_msg[32];
};

#define LOG_RECENT_COUNT 10 // number of recent logs kept in RAM; changeable

// Shared Global Variables
extern TFT_eSPI tft;
extern Adafruit_MAX31865 max1;
extern Adafruit_MAX31865 max2;
extern HX711 torqueScale;
extern Preferences preferences;
extern WebServer webServer;

extern SemaphoreHandle_t xSemaphoreSPI;
extern QueueHandle_t xLogQueue;

extern ProgramRecipe_t recipes[10];
extern SystemStatus_t sysStatus;
extern const char* stateNames[];

#endif // CONFIG_H