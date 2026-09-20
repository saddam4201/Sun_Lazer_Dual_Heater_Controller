#ifndef CONFIG_H
#define CONFIG_H

#include <Adafruit_MAX31865.h>
#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v3.4"
#endif

// Enable WiFi & WebServer support (set to 0 for bench testing to save ~500 KB
// flash)
#ifndef ENABLE_WIFI_WEBSERVER
#define ENABLE_WIFI_WEBSERVER 0
#endif

#if ENABLE_WIFI_WEBSERVER
#include <WebServer.h>
#endif

// VSPI Pins
#define PIN_VSPI_SCK 18
#define PIN_VSPI_MISO 19
#define PIN_VSPI_MOSI 23

// TFT & SD Chip Selects
#define PIN_TFT_CS 5
#define PIN_TFT_DC 2
#define PIN_TFT_RST 4
#define PIN_SD_CS 13

// Enable SD card support (set to 1 to enable SD logging and file operations)
#ifndef ENABLE_SD_CARD
#define ENABLE_SD_CARD 0
#endif

// Enable RTC support (set to 1 to enable DS3231 RTC features)
#ifndef ENABLE_RTC
#define ENABLE_RTC 0
#endif

// Enable Virtual TFT display over UART Serial (for bench testing with PC
// application)
#ifndef ENABLE_UART_VIRTUAL_TFT
#define ENABLE_UART_VIRTUAL_TFT 1
#endif
#ifndef ENABLE_VIRTUAL_UART_TFT
#define ENABLE_VIRTUAL_UART_TFT ENABLE_UART_VIRTUAL_TFT
#endif

// Enable Physical SPI ILI9341 TFT display (set to 0 for bench testing with only
// ESP)
#ifndef ENABLE_PHYSICAL_TFT
#define ENABLE_PHYSICAL_TFT 1
#endif

// Enable a virtual TFT over Serial (for testing without a display)
#ifndef ENABLE_SERIAL_TFT
#define ENABLE_SERIAL_TFT 0
#endif

// Enable Android / Mobile App Remote Control & Telemetry API
#ifndef ENABLE_APP_REMOTE
#define ENABLE_APP_REMOTE 0
#endif

// Virtual Button bitmasks for Remote App Control
enum AppButtonMask_t {
  APP_BTN_UP_BIT = (1 << 0),
  APP_BTN_DOWN_BIT = (1 << 1),
  APP_BTN_LEFT_BIT = (1 << 2),
  APP_BTN_RIGHT_BIT = (1 << 3),
  APP_BTN_OK_BIT = (1 << 4)
};

// Enable physical push button inputs
#ifndef ENABLE_PHYSICAL_BUTTONS
#define ENABLE_PHYSICAL_BUTTONS 1
#endif

// Enable serial input emulation of buttons (1..5 keys act as buttons
// 1=UP,2=DOWN,3=LEFT,4=RIGHT,5=OK) Set to 1 to enable serial keys in parallel
// with physical buttons
#ifndef INPUT_USE_SERIAL
#define INPUT_USE_SERIAL 1
#endif

// Enable serial simulator mode: when enabled and INPUT_USE_SERIAL is 1,
// terminal commands prefixed with ':' allow simulating inputs such as limit
// switches, temperatures, torque, and relay outputs. Example commands (send as
// a line starting with ':'):
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
#define INPUT_SERIAL_SIMULATOR 0
#endif

// Limit Switch Polarity: 1 = Active LOW (GND when closed), 0 = Active HIGH
#ifndef LIMIT_SWITCH_ACTIVE_LOW
#define LIMIT_SWITCH_ACTIVE_LOW 1
#endif

// Enable Emergency Stop Bench Testing Features (serial bypass commands & virtual overrides)
// Set to 1 for bench testing (allows serial bypass of unwired GPIO 35)
// Set to 0 for physical hardware operation (hardware PIN 35 strictly monitored; serial bypass disabled)
#ifndef ENABLE_ESTOP_BENCH_TESTING
#define ENABLE_ESTOP_BENCH_TESTING 1
#endif

// Virtual limit switch and emergency stop simulator overrides (for bench
// testing via Virtual TFT)
extern bool g_simDownLimit;
extern bool g_simHomeLimit;
#if ENABLE_ESTOP_BENCH_TESTING
extern bool g_simEmergencyStop;
extern bool g_simEstopBypass;
#endif

// Per-device compile-time toggles (set to 0 to disable device and use
// default/simulated values)
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
#define PIN_MAX31865_CS2 14

// Torque Sensor Pins (TQ10 via HX711)
#define PIN_HX711_DOUT 36
#define PIN_HX711_SCK 12

// Limit Switches & Emergency Stop - Active LOW (External 10k pull-ups required
// on GPIO 34/35)
#define PIN_DOWN_LIMIT 34
#define PIN_EMERGENCY_STOP                                                     \
  35 // Dedicated hardware Emergency Stop button (active LOW)
#define PIN_HOME_LIMIT PIN_EMERGENCY_STOP // Compatibility alias

// Push Buttons
#define PIN_BTN_UP 32
#define PIN_BTN_DOWN 33
#define PIN_BTN_RIGHT 25
#define PIN_BTN_OK 26
#define PIN_BTN_LEFT 27

// SSR, Pneumatic Actuator & Torque Motor Outputs
#define PIN_SSR_1 16
#define PIN_SSR_2 17
#define PIN_PNEUMATIC 21 // Solenoid valve output for pneumatic cylinder
#define PIN_MOTOR_DOWN PIN_PNEUMATIC // Compatibility alias
#define PIN_TORQUE_MOTOR                                                       \
  22 // Torque motor relay/driver output (runs during process timer)
#define PIN_MOTOR_UP PIN_TORQUE_MOTOR // Compatibility alias

// PT100 Constants & Wire Mode
#ifndef MAX31865_WIRE_MODE
#define MAX31865_WIRE_MODE                                                     \
  MAX31865_2WIRE // Options: MAX31865_2WIRE, MAX31865_3WIRE, MAX31865_4WIRE
#endif

#ifndef RREF
#define RREF                                                                   \
  430.0f // Reference resistor on MAX31865 breakout (430.0 for PT100, 4300.0 for
         // PT1000)
#endif

#ifndef RNOMINAL
#define RNOMINAL                                                               \
  100.0f // Nominal RTD resistance at 0 deg C (100.0 for PT100, 1000.0 for
         // PT1000)
#endif

// Recipe storage validation
#define RECIPE_MAGIC 0xABCD
#define RECIPE_VERSION 4

// Pneumatic Cylinder Timeouts & Delays
#ifndef PNEUMATIC_DOWN_TIMEOUT_SEC
#define PNEUMATIC_DOWN_TIMEOUT_SEC                                             \
  15 // Timeout for pneumatic cylinder to reach down limit switch
#endif

#ifndef PNEUMATIC_RETRACT_DELAY_MS
#define PNEUMATIC_RETRACT_DELAY_MS                                             \
  1000 // Retraction settling time before saving record
#endif

// 100-Base Process Timer Conversion: 100 units = 1 min (60 seconds)
#define TIMER_UNITS_TO_SECONDS(u) (((uint32_t)(u) * 60UL) / 100UL)
#define SECONDS_TO_TIMER_UNITS(s) (((uint32_t)(s) * 100UL) / 60UL)

#ifndef LIMIT_SWITCH_DOWN_TIMEOUT_SEC
#define LIMIT_SWITCH_DOWN_TIMEOUT_SEC PNEUMATIC_DOWN_TIMEOUT_SEC
#endif

#ifndef LIMIT_SWITCH_HOME_TIMEOUT_SEC
#define LIMIT_SWITCH_HOME_TIMEOUT_SEC 30
#endif

#ifndef LIMIT_SWITCH_DEBOUNCE_MS
#define LIMIT_SWITCH_DEBOUNCE_MS 100 // 100 ms limit switch debounce filter
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
  uint16_t magic;            // validation magic (0xABCD)
  uint8_t version;           // storage struct version (3)
  char name[16];             // e.g. "Program 01"
  float h1_setpoint_c;       // -40.0 to +300.0 C (step 0.5 C)
  float h2_setpoint_c;       // -40.0 to +300.0 C (step 0.5 C)
  uint32_t process_time_sec; // 0 to 9999 s
  float temp_tolerance_c;    // -10.0 to +10.0 C (step 0.5 C)
  float h1_temp_offset_pct;  // -20.0% to +20.0% (step 0.5%)
  float h2_temp_offset_pct;  // -20.0% to +20.0% (step 0.5%)
  uint8_t torque_unit;       // TorqueUnit_t (0: Nm, 1: kg.cm, 2: lb.in)
  uint8_t reserved[3];       // alignment padding

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
  float max_torque_nm; // maximum torque observed during current run
  uint32_t remaining_time_sec;
  bool down_limit_active;
  bool home_limit_active;
  bool motor_down_running;
  bool motor_up_running;
  bool torque_motor_running;
  bool emergency_stop_active;
  uint8_t active_program_idx;
  char alarm_msg[32];

  // Boot health status
  bool boot_ok;
  char boot_msg[64];

  // Force-start confirmation state (when Auto blocks start)
  bool forceStartPending;
  uint32_t forceStartUntilMs;
  bool forceStartActive; // true when process was started via Force-Start
                         // (bypasses temp wait)

  // Start mode (auto/manual)
  bool start_mode_auto;

  // SD card presence
  bool sd_present;

  // Limit switch failure tracking
  uint32_t
      down_limit_fail_count; // number of times down limit failed to activate
  uint32_t
      home_limit_fail_count; // number of times home limit failed to activate
  uint32_t last_down_limit_fail_ms; // timestamp of last down limit failure
  uint32_t last_home_limit_fail_ms; // timestamp of last home limit failure
};

#define LOG_RECENT_COUNT 10 // number of recent logs kept in RAM; changeable

// Relay Type Selection
enum RelayType_t {
  RELAY_TYPE_NORMAL = 0, // Mechanical Relay (10s time window)
  RELAY_TYPE_SSR = 1     // Solid State Relay (1s fast PWM/time window)
};

// Torque Unit Selection (3 options)
enum TorqueUnit_t {
  TORQUE_UNIT_NM = 0,    // Newton-meters
  TORQUE_UNIT_KG_CM = 1, // Kilogram-force centimeters (1 Nm = 10.197 kg.cm)
  TORQUE_UNIT_LB_IN = 2  // Pound-force inches (1 Nm = 8.851 lb.in)
};

// Setpoint Temperature Range Limits (-40 deg C to +300 deg C)
#define MIN_SETPOINT_TEMP_C -40.0f
#define MAX_SETPOINT_TEMP_C 300.0f

// Safety Torque Overload Threshold (Nm)
#define MAX_TORQUE_OVERLOAD_NM 5.0f

// Temperature Tolerance Limits (strictly positive)
#define MIN_TEMP_TOLERANCE_C 0.5f
#define MAX_TEMP_TOLERANCE_C 15.0f

// Compile-time macro to enable/disable temperature percentage manipulation
#ifndef ENABLE_TEMP_MANIPULATION
#define ENABLE_TEMP_MANIPULATION 1
#endif

// Shared Global Variables
#if ENABLE_UART_VIRTUAL_TFT
#include "serial_tft_bridge.h"
extern Dual_TFT_eSPI tft;
#else
extern TFT_eSPI tft;
#endif
extern Adafruit_MAX31865 max1;
extern Adafruit_MAX31865 max2;
extern HX711 torqueScale;
extern Preferences preferences;
#if ENABLE_WIFI_WEBSERVER
extern WebServer webServer;
#endif

extern SemaphoreHandle_t xSemaphoreSPI;
extern QueueHandle_t xLogQueue;

// Bench temperature simulation toggle (false by default: reads real MAX31865
// sensor)
extern bool g_benchTempSimEnabled;

// Active settings
extern RelayType_t g_relayType;
extern TorqueUnit_t g_torqueUnit;
extern float g_h1_temp_manip_pct; // -20.0% to +20.0%
extern float g_h2_temp_manip_pct; // -20.0% to +20.0%

const char *getTorqueUnitName(TorqueUnit_t unit);
float getTorqueConversionFactor(TorqueUnit_t unit);

extern ProgramRecipe_t recipes[10];
extern SystemStatus_t sysStatus;
extern const char *stateNames[];

#endif // CONFIG_H