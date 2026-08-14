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

// Enable serial input emulation of buttons (1..5 keys act as buttons 1=UP,2=DOWN,3=LEFT,4=RIGHT,5=OK)
// Set to 1 to use serial keys instead of physical buttons
#ifndef INPUT_USE_SERIAL
#define INPUT_USE_SERIAL 1
#endif

// Enable serial simulator mode: when enabled and INPUT_USE_SERIAL is 1, terminal commands
// prefixed with ':' allow simulating inputs such as limit switches, temperatures, torque, and relay outputs.
// Example commands (send as a line starting with ':'):
//   :down on|off       -- set Down limit switch state
//   :home on|off       -- set Home limit switch state
//   :h1 <float>        -- set H1 actual temperature (C)
//   :h2 <float>        -- set H2 actual temperature (C)
//   :torque <float>    -- set current torque (Nm)
//   :ssr1 on|off       -- set SSR1 output
//   :ssr2 on|off       -- set SSR2 output
//   :motor_down on|off -- set motor down output
//   :motor_up on|off   -- set motor up output
//   :show              -- print current simulated status
#ifndef INPUT_SERIAL_SIMULATOR
#define INPUT_SERIAL_SIMULATOR 1
#endif

// Per-device compile-time toggles (set to 0 to disable device and use default/simulated values)
#ifndef ENABLE_MAX31865
#define ENABLE_MAX31865 1
#endif

#ifndef ENABLE_HX711
#define ENABLE_HX711 1
#endif

// Per-heater compile-time toggles (disable individual heater channel)
#ifndef ENABLE_H1
#define ENABLE_H1 1
#endif

#ifndef ENABLE_H2
#define ENABLE_H2 1
#endif

// Note: ENABLE_SD_CARD and ENABLE_RTC already present above

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

// Limit switch timeouts (seconds). When moving up/down, if the corresponding limit switch
// is not detected within this timeout the controller will stop the motor and proceed
// to the next logical state (instead of triggering a hard safety shutdown).
// Separate timeouts for down and home limit switches for better control.
#ifndef LIMIT_SWITCH_DOWN_TIMEOUT_SEC
#define LIMIT_SWITCH_DOWN_TIMEOUT_SEC 30
#endif

#ifndef LIMIT_SWITCH_HOME_TIMEOUT_SEC
#define LIMIT_SWITCH_HOME_TIMEOUT_SEC 30
#endif

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

    // Boot health status
    bool boot_ok;
    char boot_msg[64];

    // Force-start confirmation state (when Auto blocks start)
    bool forceStartPending;
    uint32_t forceStartUntilMs;

    // Start mode (auto/manual)
    bool start_mode_auto;

    // Limit switch failure tracking
    uint32_t down_limit_fail_count;     // number of times down limit failed to activate
    uint32_t home_limit_fail_count;     // number of times home limit failed to activate
    uint32_t last_down_limit_fail_ms;   // timestamp of last down limit failure
    uint32_t last_home_limit_fail_ms;   // timestamp of last home limit failure
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