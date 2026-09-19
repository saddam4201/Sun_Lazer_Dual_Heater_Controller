#include "display_renderer.h"
#include "ds1307_rtc.h"
#include "milk_bubble_remover.h"
#include "serial_tft_bridge.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <SPI.h>

#if ENABLE_WIFI_TELEMETRY
#include <WiFi.h>
#endif
#if ENABLE_BLUETOOTH_SERIAL
#include "bluetooth_manager.h"
#endif

DisplayRenderer Renderer;

// Theme color definitions
const ThemeColors THEME_PALETTES[THEME_COUNT] = {
    {"Classic Green", 0x9E66, 0x0000, 0x18E3},
    {"Blue Backlight", 0x0013, 0xFFFF, 0x0008},
    {"Amber Industrial", 0x0000, 0xFD20, 0x3186},
    {"Modern OLED", 0x0000, 0xFFFF, 0x2104},
    {"Cyan Dark", 0x0862, 0x07FF, 0x0010},
    {"Inverted Mono", 0xFFFF, 0x0000, 0x7BEF}};

// Dual Display Driver (Physical SPI TFT + Virtual UART TFT)
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
static Dual_ILI9341 s_tft = Dual_ILI9341(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
#endif

// Precomputed coordinate mapping tables for fast 320x240 scaling
static uint8_t s_x_map_byte[320];
static uint8_t s_x_map_mask[320];

DisplayRenderer::DisplayRenderer()
    : m_current_theme(DEFAULT_THEME), m_scaling_mode(DEFAULT_SCALING_MODE),
      m_cursor_mode(DEFAULT_CURSOR_MODE), m_tft_available(false),
      m_last_telemetry_time(0), m_last_status_bar_time(0), m_battery_pct(95),
      m_battery_charging(true), m_clock_offset_seconds(12 * 3600),
      m_last_cursor_x(-1), m_last_cursor_y(-1), m_last_cursor_w(0),
      m_last_cursor_h(0), m_last_cursor_row(-1), m_last_cursor_col(-1),
      m_last_cursor_visible(false), m_last_cursor_blink_toggle(0),
      m_cursor_blink_state(true), m_frame_count(0), m_last_fps_time(0),
      m_fps(0.0f), m_last_screen_type(0), m_last_countdown(-1),
      m_last_temp(-999.0f), m_last_fat(-1.0f), m_last_snf(-1.0f),
      m_last_density(-1.0f), m_last_protein(-1.0f), m_last_lactose(-1.0f),
      m_last_water(-1.0f), m_last_p2_temp(-999.0f), m_last_p2_fzp(-999.0f),
      m_last_p2_solubility(-999.0f), m_boot_phase(BOOT_PHASE_WELCOME),
      m_boot_phase_start_ms(0), m_last_degas_remaining(0xFFFF),
      m_degasser_started(false), m_splash_card_drawn(false) {
  memset(m_shadow_framebuffer, 0xFF, sizeof(m_shadow_framebuffer));
  memset(m_shadow_text_buffer, 0, sizeof(m_shadow_text_buffer));
  memset(m_last_sample, 0, sizeof(m_last_sample));
  memset(m_last_results_sample, 0, sizeof(m_last_results_sample));
  memset(m_last_splash_vers, 0, sizeof(m_last_splash_vers));
  memset(m_last_splash_date, 0, sizeof(m_last_splash_date));
  memset(m_last_splash_ser, 0, sizeof(m_last_splash_ser));
}

const char *DisplayRenderer::getEsp32SerialNumber() {
  static char s_esp_serial[16] = {0};
  if (s_esp_serial[0] == '\0') {
#ifdef CUSTOM_DEVICE_SERIAL
    if (strlen(CUSTOM_DEVICE_SERIAL) > 0) {
      strncpy(s_esp_serial, CUSTOM_DEVICE_SERIAL, sizeof(s_esp_serial) - 1);
      return s_esp_serial;
    }
#endif
    uint64_t mac = ESP.getEfuseMac();
    uint16_t id = (uint16_t)(mac & 0xFFFF);
    if (id == 0) {
      id = (uint16_t)((mac >> 16) & 0xFFFF);
    }
    snprintf(s_esp_serial, sizeof(s_esp_serial), "%04X", id);
  }
  return s_esp_serial;
}

bool DisplayRenderer::begin() {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  m_boot_phase = BOOT_PHASE_WELCOME; // Always show custom startup splash card
                                     // first on power-on
  m_boot_phase_start_ms = millis();
  m_last_degas_remaining = 0xFFFF;
  m_degasser_started = false;
  m_splash_card_drawn = false;

  if (TFT_PIN_BL >= 0) {
    pinMode(TFT_PIN_BL, OUTPUT);
    digitalWrite(TFT_PIN_BL, HIGH); // Turn on backlight
  }

  // Precompute horizontal mapping tables for 320-pixel stretch (128 -> 320)
  for (int x = 0; x < 320; x++) {
    int src_x = (x * 128) / 320;             // 0..127
    s_x_map_byte[x] = (uint8_t)(src_x >> 3); // Byte offset in row (0..15)
    s_x_map_mask[x] = (uint8_t)(0x80 >> (src_x & 7)); // Bit mask (MSB first)
  }

  // Initialize SPI for TFT display
  SPI.begin(TFT_PIN_SCK, TFT_PIN_MISO, TFT_PIN_MOSI, TFT_PIN_CS);

  s_tft.setPhysicalEnabled(ENABLE_PHYSICAL_TFT);
  s_tft.setUartEnabled(ENABLE_UART_VIRTUAL_TFT);
  s_tft.begin(27000000); // 27 MHz SPI
  s_tft.setRotation(TFT_ROTATION);
  m_tft_available = true;

  // Draw initial screen layout
  drawBezelAndBackground();
#else
  m_tft_available = false;
#endif
  return true;
}

const char *DisplayRenderer::getScalingModeName() const {
  switch (m_scaling_mode) {
  case SCALE_MODE_MODERN_DASHBOARD:
    return "Modern Smart Instrument Dashboard";
  case SCALE_MODE_CLASSIC_CENTERED:
    return "Classic 2x Centered (256x128)";
  case SCALE_MODE_FULL_STRETCH:
    return "Full Edge-to-Edge (320x220)";
  case SCALE_MODE_PROPORTIONAL_WIDE:
    return "Proportional Wide (320x160)";
  case SCALE_MODE_LARGE_TEXT:
    return "Large Industrial Text Layout";
  default:
    return "Unknown";
  }
}

void DisplayRenderer::setTheme(LCDTheme theme) {
  if (theme >= THEME_COUNT)
    theme = THEME_BLUE_BACKLIGHT;
  m_current_theme = theme;
  memset(m_shadow_framebuffer, 0xFF, sizeof(m_shadow_framebuffer));
  memset(m_shadow_text_buffer, 0, sizeof(m_shadow_text_buffer));
  m_last_screen_type = 0;
  m_last_status_bar_time = 0;
  m_last_temp = -999.0f;
  m_last_countdown = -1;
  m_last_sample[0] = '\0';
  m_last_results_sample[0] = '\0';
  m_last_fat = -1.0f;
  m_last_snf = -1.0f;
  m_last_density = -1.0f;
  m_last_protein = -1.0f;
  m_last_lactose = -1.0f;
  m_last_water = -1.0f;
  if (m_tft_available) {
    drawBezelAndBackground();
  }
}

void DisplayRenderer::nextTheme() {
  uint8_t next = ((uint8_t)m_current_theme + 1) % THEME_COUNT;
  setTheme((LCDTheme)next);
}

void DisplayRenderer::setScalingMode(DisplayScalingMode mode) {
  if (mode >= SCALE_MODE_COUNT)
    mode = SCALE_MODE_CLASSIC_CENTERED;
  m_scaling_mode = mode;
  memset(m_shadow_framebuffer, 0xFF, sizeof(m_shadow_framebuffer));
  memset(m_shadow_text_buffer, 0, sizeof(m_shadow_text_buffer));
  m_last_screen_type = 0;
  m_last_status_bar_time = 0;
  m_last_temp = -999.0f;
  m_last_countdown = -1;
  m_last_sample[0] = '\0';
  m_last_results_sample[0] = '\0';
  m_last_fat = -1.0f;
  m_last_snf = -1.0f;
  m_last_density = -1.0f;
  m_last_protein = -1.0f;
  m_last_lactose = -1.0f;
  m_last_water = -1.0f;
  if (m_tft_available) {
    drawBezelAndBackground();
  }
}

void DisplayRenderer::nextScalingMode() {
  // Directly toggle between Classic Centered (Original ST7920 Screen) and
  // Modern Dashboard
  if (m_scaling_mode == SCALE_MODE_CLASSIC_CENTERED) {
    setScalingMode(SCALE_MODE_MODERN_DASHBOARD);
  } else {
    setScalingMode(SCALE_MODE_CLASSIC_CENTERED);
  }
}

const char *DisplayRenderer::getCursorModeName() const {
  switch (m_cursor_mode) {
  case CURSOR_MODE_AUTO:
    return "Auto (Follows ST7920 Cmds)";
  case CURSOR_MODE_ALWAYS_ON:
    return "Always ON (Blinking)";
  case CURSOR_MODE_OFF:
    return "Disabled (Hidden)";
  default:
    return "Unknown";
  }
}

void DisplayRenderer::setCursorMode(CursorDisplayMode mode) {
  if (mode >= CURSOR_MODE_COUNT)
    mode = CURSOR_MODE_AUTO;
  m_cursor_mode = mode;
  m_last_cursor_x = -1; // Force clean redraw
  m_last_cursor_row = -1;
  m_last_cursor_col = -1;
}

void DisplayRenderer::nextCursorMode() {
  uint8_t next = ((uint8_t)m_cursor_mode + 1) % CURSOR_MODE_COUNT;
  setCursorMode((CursorDisplayMode)next);
}

void DisplayRenderer::setClock(uint8_t hours, uint8_t minutes,
                               uint8_t seconds) {
  uint32_t target_sec =
      (hours % 24) * 3600 + (minutes % 60) * 60 + (seconds % 60);
  uint32_t uptime_sec = millis() / 1000;
  m_clock_offset_seconds = (target_sec >= uptime_sec)
                               ? (target_sec - uptime_sec)
                               : (target_sec + 86400 - uptime_sec);
  m_last_status_bar_time = 0; // Force immediate refresh
}

void DisplayRenderer::setBatteryLevel(uint8_t pct, bool charging) {
  if (pct > 100)
    pct = 100;
  m_battery_pct = pct;
  m_battery_charging = charging;
  m_last_status_bar_time = 0; // Force immediate refresh
}

void DisplayRenderer::renderTopStatusBar(ST7920Emulator &emulator,
                                         bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  uint32_t now = millis();
  // Refresh at 1 Hz unless forced
  if (!force_redraw && (now - m_last_status_bar_time < 1000)) {
    return;
  }
  m_last_status_bar_time = now;

  const uint16_t BAR_BG = 0x10A2;      // Deep slate dark background
  const uint16_t BAR_LINE = 0x39E7;    // Separator border at Y = 39
  const uint16_t TEXT_COLOR = 0xFFFF;  // Bright white text
  const uint16_t BRAND_COLOR = 0x07FF; // Electric cyan accent

  if (force_redraw) {
    s_tft.fillRect(0, 0, 320, TFT_STATUS_BAR_HEIGHT - 1, BAR_BG);
    s_tft.drawFastHLine(0, TFT_STATUS_BAR_HEIGHT - 1, 320, BAR_LINE);
  }

  // 1. Left: Live Bus Status Bullet & Brand ("SUN SMART" in modern bold sans)
  bool bus_active =
      (emulator.getStats().valid_packets > 0) && emulator.isDirty();
  uint16_t dot_color = bus_active ? 0x07E0 : 0x7BEF;

  static uint16_t s_last_dot_color = 0;
  static bool s_left_drawn = false;
#if ENABLE_BLUETOOTH_SERIAL
  static bool s_last_bt_conn = false;
  static uint8_t s_last_bt_sec = 0;
  bool cur_bt_conn = BtManager.isConnected();
  uint8_t cur_bt_sec = BubbleRemover.getBtWaitRemainingSeconds();
  if (cur_bt_conn != s_last_bt_conn || cur_bt_sec != s_last_bt_sec) {
    s_left_drawn = false;
    s_last_bt_conn = cur_bt_conn;
    s_last_bt_sec = cur_bt_sec;
  }
#endif

  if (force_redraw || !s_left_drawn) {
    s_tft.fillRect(0, 0, 128, TFT_STATUS_BAR_HEIGHT - 1, BAR_BG);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(BRAND_COLOR, BAR_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(20, 20);
    s_tft.print("SUN SMART");
    s_tft.setFont(NULL);

#if ENABLE_WIFI_TELEMETRY
    char net_str[24];
    bool is_sta = (WiFi.status() == WL_CONNECTED);
    uint16_t ip_color = is_sta ? 0x07E0 : 0xFD20;

    if (is_sta) {
      snprintf(net_str, sizeof(net_str), "IP : %s",
               WiFi.localIP().toString().c_str());
    } else {
      snprintf(net_str, sizeof(net_str), "AP : 192.168.4.1");
    }

    s_tft.fillRect(20, 26, 105, 10, BAR_BG);
    s_tft.setTextColor(ip_color, BAR_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(20, 27);
    s_tft.print(net_str);
#elif ENABLE_BLUETOOTH_SERIAL
    char bt_str[24];
    bool is_waiting = BubbleRemover.isWaitingForBtResponse();
    uint16_t bt_color = is_waiting ? 0xFD20 : (cur_bt_conn ? 0x07FF : 0x7BEF);

    if (is_waiting) {
      snprintf(bt_str, sizeof(bt_str), "BT: WAIT %ds", (int)cur_bt_sec);
    } else if (cur_bt_conn) {
      snprintf(bt_str, sizeof(bt_str), "BT: CONNECTED");
    } else {
      snprintf(bt_str, sizeof(bt_str), "BT: READY");
    }

    s_tft.fillRect(20, 26, 105, 10, BAR_BG);
    s_tft.setTextColor(bt_color, BAR_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(20, 27);
    s_tft.print(bt_str);
#endif
    s_left_drawn = true;
    s_last_dot_color = dot_color;
    s_tft.fillCircle(10, 15, 4, dot_color);
  } else if (dot_color != s_last_dot_color) {
    s_last_dot_color = dot_color;
    s_tft.fillCircle(10, 15, 4, dot_color);
  }


  // 2. Middle: Always Digital Clock (Live Date & Smooth Live Time)
  // Note: Degas countdown timer has been removed from top header per user
  // request.
  const char *date_str = Rtc.getDateStr();
  const char *time_str = Rtc.getTimeStr();

  static char s_last_date[16] = "";
  static char s_last_time[16] = "";

  // 2.1 Live Date on top line (clean 1x font) - redrawn only if changed or
  // forced
  if (force_redraw || strcmp(date_str, s_last_date) != 0) {
    strncpy(s_last_date, date_str, sizeof(s_last_date));
    int16_t dw = strlen(date_str) * 6;
    int16_t cur_date_x = 128 + ((98 - dw) / 2);
    if (cur_date_x < 130)
      cur_date_x = 130;

    if (!force_redraw) {
      s_tft.fillRectPhysicalOnly(128, 2, 98, 12, BAR_BG);
    }
    s_tft.setFont(NULL);
    s_tft.setTextSize(1);
    s_tft.setTextColor(0x07FF, BAR_BG); // Cyan
    s_tft.setCursor(cur_date_x, 4);
    s_tft.print(date_str);
  }

  // 2.2 Live Time on bottom line (FreeSans9pt7b) - smooth flicker-free update
  if (force_redraw || strcmp(time_str, s_last_time) != 0) {
    strncpy(s_last_time, time_str, sizeof(s_last_time));

    // Fixed anchor centered in middle box (128..226) so digits/colons never
    // jitter horizontally
    const int16_t TIME_X = 140;
    const int16_t TIME_Y = 32;

    // Erase ONLY the compact time bounding rectangle on physical display
    // without wiping the date or sending disruptive full RECT packets over UART
    s_tft.fillRectPhysicalOnly(138, 18, 78, 17, BAR_BG);

    s_tft.setFont(&FreeSans9pt7b);
    s_tft.setTextColor(TEXT_COLOR, BAR_BG);
    s_tft.setCursor(TIME_X, TIME_Y);
    s_tft.print(time_str);
    s_tft.setFont(NULL);
    s_tft.setTextSize(1);
  }

  // 3. Right: Battery text in modern bold sans & Mobile Battery Icon (W=28,
  // H=16)
  static uint8_t s_last_rendered_pct = 0xFF;
  static bool s_last_rendered_chg = false;

  if (force_redraw || m_battery_pct != s_last_rendered_pct ||
      m_battery_charging != s_last_rendered_chg) {
    s_last_rendered_pct = m_battery_pct;
    s_last_rendered_chg = m_battery_charging;

    char batt_str[8];
    snprintf(batt_str, sizeof(batt_str), "%d%%", m_battery_pct);

    s_tft.fillRect(225, 0, 95, TFT_STATUS_BAR_HEIGHT - 1, BAR_BG);

    const int16_t BATT_X = 284;
    const int16_t BATT_Y = 12;
    const int16_t BATT_W = 28;
    const int16_t BATT_H = 16;
    const int16_t BATT_GAP =
        8; // Clean visible space between battery percentage and battery shell

    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(TEXT_COLOR, BAR_BG);
    s_tft.setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    s_tft.getTextBounds(batt_str, 0, 0, &bx, &by, &bw, &bh);
    int16_t text_x = BATT_X - BATT_GAP - bw;
    s_tft.setCursor(text_x, 25);
    s_tft.print(batt_str);
    s_tft.setFont(NULL);

    // Large Mobile Battery Shell
    s_tft.drawRoundRect(BATT_X, BATT_Y, BATT_W, BATT_H, 3,
                        0xD6BA); // Silver rounded shell
    s_tft.fillRect(BATT_X + BATT_W, BATT_Y + 4, 3, 8, 0xD6BA); // Terminal pip

    int16_t fill_w = (m_battery_pct * (BATT_W - 4)) / 100;
    if (fill_w < 2 && m_battery_pct > 0)
      fill_w = 2;

    uint16_t fill_color = 0x07E0; // Green (>40%)
    if (m_battery_pct <= 20) {
      fill_color = 0xF800; // Red (<20%)
    } else if (m_battery_pct <= 40) {
      fill_color = 0xFD20; // Amber (20-40%)
    }

    s_tft.fillRect(BATT_X + 2, BATT_Y + 2, fill_w, BATT_H - 4, fill_color);
  }
#endif
}

void DisplayRenderer::drawBezelAndBackground() {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  const ThemeColors &pal = THEME_PALETTES[m_current_theme];

  // Clear and draw top mobile status bar
  s_tft.fillScreen(pal.tft_bezel);
  m_last_status_bar_time = 0; // Force immediate status bar render

  switch (m_scaling_mode) {
  case SCALE_MODE_MODERN_DASHBOARD: {
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0841);
    m_last_screen_type = 0; // Force full repaint
    break;
  }
  case SCALE_MODE_CLASSIC_CENTERED: {
    int lcd_w = ST7920_WIDTH * TFT_SCALE_FACTOR;
    int lcd_h = ST7920_HEIGHT * TFT_SCALE_FACTOR;
    s_tft.drawRect(TFT_OFFSET_X - 2, TFT_OFFSET_Y - 2, lcd_w + 4, lcd_h + 4,
                   0x0000);
    s_tft.fillRect(TFT_OFFSET_X, TFT_OFFSET_Y, lcd_w, lcd_h, pal.tft_bg);
    s_tft.setTextColor(0xFFFF, pal.tft_bezel);
    s_tft.setTextSize(1);
    s_tft.setCursor(TFT_OFFSET_X, TFT_OFFSET_Y - 12);
    s_tft.print("ORIGINAL DISPLAY (128x64)");
    break;
  }
  case SCALE_MODE_FULL_STRETCH: {
    // Edge-to-edge active canvas below status bar (320x220)
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT,
                   pal.tft_bg);
    break;
  }
  case SCALE_MODE_PROPORTIONAL_WIDE: {
    // Active viewing area: Y = 40 to 199 (160 px high, 2.5x square scale)
    s_tft.drawRect(0, TFT_STATUS_BAR_HEIGHT - 1, 320, 162, 0x0000);
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, 160, pal.tft_bg);

    // Bottom Diagnostics Bar (Y = 200 to 239)
    s_tft.fillRect(0, 200, 320, 40, pal.tft_bezel);
    s_tft.drawFastHLine(0, 200, 320, 0x0000);
    break;
  }
  case SCALE_MODE_LARGE_TEXT: {
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT,
                   pal.tft_bezel);
    break;
  }
  default:
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT,
                   pal.tft_bg);
    break;
  }
#endif
}

void DisplayRenderer::renderClassicCentered(const uint8_t *current_fb,
                                            bool force_full_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
#if ENABLE_PHYSICAL_TFT
  uint16_t scanline[ST7920_WIDTH * TFT_SCALE_FACTOR];
#endif

  for (uint8_t y = 0; y < ST7920_HEIGHT; y++) {
    uint16_t row_offset = (uint16_t)y * 16;
    if (!force_full_redraw &&
        memcmp(&current_fb[row_offset], &m_shadow_framebuffer[row_offset],
               16) == 0) {
      continue;
    }
    memcpy(&m_shadow_framebuffer[row_offset], &current_fb[row_offset], 16);

#if ENABLE_UART_VIRTUAL_TFT
    s_tft.sendST7920Row(y, &current_fb[row_offset], 16, pal.tft_fg, pal.tft_bg);
#endif

#if ENABLE_PHYSICAL_TFT
    uint16_t pixel_idx = 0;
    for (uint8_t b = 0; b < 16; b++) {
      uint8_t byte_val = current_fb[row_offset + b];
      for (int8_t bit = 7; bit >= 0; bit--) {
        uint16_t color = (byte_val & (1 << bit)) ? pal.tft_fg : pal.tft_bg;
        scanline[pixel_idx++] = color;
        scanline[pixel_idx++] = color;
      }
    }

    int screen_y = TFT_OFFSET_Y + (y * TFT_SCALE_FACTOR);
    s_tft.startWrite();
    s_tft.setAddrWindow(TFT_OFFSET_X, screen_y, ST7920_WIDTH * TFT_SCALE_FACTOR,
                        TFT_SCALE_FACTOR);
    s_tft.writePixels(scanline, ST7920_WIDTH * TFT_SCALE_FACTOR);
    s_tft.writePixels(scanline, ST7920_WIDTH * TFT_SCALE_FACTOR);
    s_tft.endWrite();
#endif
  }
#endif
}

void DisplayRenderer::renderFullStretch(const uint8_t *current_fb,
                                        bool force_full_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
#if ENABLE_PHYSICAL_TFT
  uint16_t scanline320[320];
#endif

  for (uint8_t src_y = 0; src_y < ST7920_HEIGHT; src_y++) {
    uint16_t row_offset = (uint16_t)src_y * 16;
    if (!force_full_redraw &&
        memcmp(&current_fb[row_offset], &m_shadow_framebuffer[row_offset],
               16) == 0) {
      continue;
    }
    memcpy(&m_shadow_framebuffer[row_offset], &current_fb[row_offset], 16);

#if ENABLE_UART_VIRTUAL_TFT
    s_tft.sendST7920Row(src_y, &current_fb[row_offset], 16, pal.tft_fg,
                        pal.tft_bg);
#endif

#if ENABLE_PHYSICAL_TFT
    // Build 320-pixel scanline using fast precomputed mapping
    for (int x = 0; x < 320; x++) {
      uint8_t b = current_fb[row_offset + s_x_map_byte[x]];
      scanline320[x] = (b & s_x_map_mask[x]) ? pal.tft_fg : pal.tft_bg;
    }

    // Calculate Y span on 320x200 active region below status bar
    uint16_t y_start = TFT_STATUS_BAR_HEIGHT + (src_y * TFT_ACTIVE_HEIGHT) / 64;
    uint16_t y_end =
        TFT_STATUS_BAR_HEIGHT + ((src_y + 1) * TFT_ACTIVE_HEIGHT) / 64;
    uint16_t h = y_end - y_start;

    s_tft.startWrite();
    s_tft.setAddrWindow(0, y_start, 320, h);
    for (uint16_t i = 0; i < h; i++) {
      s_tft.writePixels(scanline320, 320);
    }
    s_tft.endWrite();
#endif
  }
#endif
}

void DisplayRenderer::renderProportionalWide(const uint8_t *current_fb,
                                             ST7920Emulator &emulator,
                                             bool force_full_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
#if ENABLE_PHYSICAL_TFT
  uint16_t scanline320[320];
#endif

  for (uint8_t src_y = 0; src_y < ST7920_HEIGHT; src_y++) {
    uint16_t row_offset = (uint16_t)src_y * 16;
    if (!force_full_redraw &&
        memcmp(&current_fb[row_offset], &m_shadow_framebuffer[row_offset],
               16) == 0) {
      continue;
    }
    memcpy(&m_shadow_framebuffer[row_offset], &current_fb[row_offset], 16);

#if ENABLE_UART_VIRTUAL_TFT
    s_tft.sendST7920Row(src_y, &current_fb[row_offset], 16, pal.tft_fg,
                        pal.tft_bg);
#endif

#if ENABLE_PHYSICAL_TFT
    for (int x = 0; x < 320; x++) {
      uint8_t b = current_fb[row_offset + s_x_map_byte[x]];
      scanline320[x] = (b & s_x_map_mask[x]) ? pal.tft_fg : pal.tft_bg;
    }

    // Active viewing area: Y = 40 to 199 (160 px high, 2.5x square scale)
    uint16_t y_start = TFT_STATUS_BAR_HEIGHT + (src_y * 160) / 64;
    uint16_t y_end = TFT_STATUS_BAR_HEIGHT + ((src_y + 1) * 160) / 64;
    uint16_t h = y_end - y_start;

    s_tft.startWrite();
    s_tft.setAddrWindow(0, y_start, 320, h);
    for (uint16_t i = 0; i < h; i++) {
      s_tft.writePixels(scanline320, 320);
    }
    s_tft.endWrite();
#endif
  }

  updateTelemetryBar(emulator);
#endif
}

void DisplayRenderer::updateTelemetryBar(ST7920Emulator &emulator) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  uint32_t now = millis();
  if (now - m_last_telemetry_time < 500)
    return; // Update twice per second
  m_last_telemetry_time = now;

  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
  ST7920Stats stats = emulator.getStats();

  s_tft.fillRect(0, 200, 320, 40, pal.tft_bezel);
  s_tft.drawFastHLine(0, 200, 320, 0x0000);
  s_tft.setTextColor(0xFFFF, pal.tft_bezel);
  s_tft.setTextSize(1);
  s_tft.setCursor(8, 204);
  s_tft.printf("FPS: %.1f | Pkts: %lu | Chars: %lu", m_fps, stats.valid_packets,
               stats.ddram_chars_written);
  s_tft.setCursor(8, 216);
  s_tft.printf("THEME: %s | SCALING: 2.5x WIDE", pal.name);
  s_tft.setCursor(8, 228);
  s_tft.printf("SYSTEM: 240MHz DUAL-CORE ENGINE READY");
#endif
}

void DisplayRenderer::renderLargeText(ST7920Emulator &emulator,
                                      bool force_full_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  // If controller is running in Graphic Mode (GDRAM bitmap), fall back to full
  // stretch
  if (emulator.isGraphicMode()) {
    uint8_t current_fb[ST7920_BUFFER_SIZE];
    emulator.copyFramebuffer(current_fb);
    renderFullStretch(current_fb, force_full_redraw);
    return;
  }

  const ThemeColors &pal = THEME_PALETTES[m_current_theme];

  // Card styling parameters for 4 lines on 320x200 active region below 40px
  // header
  const int card_x = 8;
  const int card_w = 304;
  const int card_h = 42;
  const int card_y_positions[4] = {46, 94, 142, 190};

  char line_buf[17];

  for (uint8_t l = 0; l < 4; l++) {
    emulator.getTextLine(l, line_buf, sizeof(line_buf));

    // Dirty check: only redraw line if text actually changed!
    if (!force_full_redraw &&
        memcmp(&m_shadow_text_buffer[l * 16], line_buf, 16) == 0) {
      continue;
    }
    memcpy(&m_shadow_text_buffer[l * 16], line_buf, 16);

    int y = card_y_positions[l];

    // Draw card container panel
    s_tft.fillRoundRect(card_x, y, card_w, card_h, 6, pal.tft_bg);
    s_tft.drawRoundRect(card_x, y, card_w, card_h, 6, pal.tft_bezel);

    // Render line in large, crisp size 3 font (18x24 px per character,
    // centered)
    s_tft.setTextColor(pal.tft_fg, pal.tft_bg);
    s_tft.setTextSize(3);
    s_tft.setCursor(16, y + 9);
    s_tft.print(line_buf);
  }
#endif
}

// ==============================================================================
// NEXT-GEN MODERN SMART INSTRUMENT DASHBOARD ENGINE
// ==============================================================================

#define MOD_BG 0x0841          // Deep Void Navy
#define MOD_CARD_BG 0x10C3     // Dark Slate Glass Card
#define MOD_CARD_BORDER 0x2187 // Subtle Slate Border
#define MOD_CARD_HI 0x3A4E     // Card Highlight Line
#define MOD_CYAN 0x07FF        // Electric Cyan (FAT)
#define MOD_AMBER 0xFDC0       // Golden Amber (SNF)
#define MOD_VIOLET 0x9BFF      // Neon Violet (DENSITY)
#define MOD_CORAL 0xFBAE       // Rose Coral (PROTEIN)
#define MOD_EMERALD 0x07E0     // Emerald Mint (LACTOSE / SUCCESS)
#define MOD_ICE_BLUE 0x5DFE    // Sky Ice Blue (WATER)
#define MOD_WHITE 0xFFFF       // Pure Crisp White
#define MOD_MUTED 0x9CD3       // Cool Muted Silver

static bool containsIgnoreCase(const char *haystack, const char *needle) {
  if (!haystack || !needle)
    return false;
  size_t h_len = strlen(haystack);
  size_t n_len = strlen(needle);
  if (n_len > h_len)
    return false;
  for (size_t i = 0; i <= h_len - n_len; i++) {
    bool match = true;
    for (size_t j = 0; j < n_len; j++) {
      char ch1 = haystack[i + j];
      char ch2 = needle[j];
      if (ch1 >= 'A' && ch1 <= 'Z')
        ch1 += 32;
      if (ch2 >= 'A' && ch2 <= 'Z')
        ch2 += 32;
      if (ch1 != ch2) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

static const char *detectMilkSampleType(const char *text) {
  if (!text || text[0] == '\0')
    return nullptr;

  // 1. Direct explicit keyword checks (highest confidence)
  if (containsIgnoreCase(text, "mix") || containsIgnoreCase(text, "mixed")) {
    return "Mix Milk";
  }
  if (containsIgnoreCase(text, "buf") || containsIgnoreCase(text, "buff") ||
      containsIgnoreCase(text, "duff") || containsIgnoreCase(text, "buffalo")) {
    return "Buffalo Milk";
  }
  if (containsIgnoreCase(text, "cow")) {
    return "Cow Milk";
  }

  // 2. Multi-word abbreviations commonly used on 16-character analyzer screens
  if (containsIgnoreCase(text, "c milk") ||
      containsIgnoreCase(text, "c.milk") ||
      containsIgnoreCase(text, "c-milk") ||
      containsIgnoreCase(text, "c_milk") ||
      containsIgnoreCase(text, "meas: c") ||
      containsIgnoreCase(text, "meas:c") ||
      containsIgnoreCase(text, "meas : c") ||
      containsIgnoreCase(text, "1 c milk") || containsIgnoreCase(text, "1 c")) {
    return "Cow Milk";
  }

  if (containsIgnoreCase(text, "b milk") ||
      containsIgnoreCase(text, "b.milk") ||
      containsIgnoreCase(text, "b-milk") ||
      containsIgnoreCase(text, "b_milk") ||
      containsIgnoreCase(text, "meas: b") ||
      containsIgnoreCase(text, "meas:b") ||
      containsIgnoreCase(text, "meas : b") ||
      containsIgnoreCase(text, "2 b milk") || containsIgnoreCase(text, "2 b")) {
    return "Buffalo Milk";
  }

  if (containsIgnoreCase(text, "m mix") || containsIgnoreCase(text, "m.mix") ||
      containsIgnoreCase(text, "m-mix") || containsIgnoreCase(text, "m_mix") ||
      containsIgnoreCase(text, "m milk") ||
      containsIgnoreCase(text, "m.milk") ||
      containsIgnoreCase(text, "m-milk") ||
      containsIgnoreCase(text, "m_milk") ||
      containsIgnoreCase(text, "meas: m") ||
      containsIgnoreCase(text, "meas:m") ||
      containsIgnoreCase(text, "meas : m") ||
      containsIgnoreCase(text, "3 m mix") || containsIgnoreCase(text, "3 m")) {
    return "Mix Milk";
  }

  // 3. Exact or standalone channel numbers / single-letter abbreviations
  const char *t = text;
  while (*t == ' ')
    t++;
  if (strcmp(t, "3") == 0 || strcmp(t, "3 Milk") == 0 ||
      strcmp(t, "3 milk") == 0 || strcmp(t, "M") == 0 || strcmp(t, "m") == 0 ||
      strcmp(t, "M Mix") == 0 || strcmp(t, "m mix") == 0 ||
      strcmp(t, "M MIx") == 0 || strcmp(t, "M Milk") == 0 ||
      strcmp(t, "m milk") == 0) {
    return "Mix Milk";
  }
  if (strcmp(t, "2") == 0 || strcmp(t, "2 Milk") == 0 ||
      strcmp(t, "2 milk") == 0 || strcmp(t, "B") == 0 || strcmp(t, "b") == 0 ||
      strcmp(t, "B Milk") == 0 || strcmp(t, "b milk") == 0) {
    return "Buffalo Milk";
  }
  if (strcmp(t, "1") == 0 || strcmp(t, "1 Milk") == 0 ||
      strcmp(t, "1 milk") == 0 || strcmp(t, "C") == 0 || strcmp(t, "c") == 0 ||
      strcmp(t, "C Milk") == 0 || strcmp(t, "c milk") == 0) {
    return "Cow Milk";
  }

  // 4. Channel indicator patterns: channel 3 = Mix, channel 2 = Buffalo,
  // channel 1 = Cow
  if (containsIgnoreCase(text, "cal3") || containsIgnoreCase(text, "cal 3") ||
      containsIgnoreCase(text, "cal:3") ||
      containsIgnoreCase(text, "cal : 3") ||
      containsIgnoreCase(text, "meas: 3") ||
      containsIgnoreCase(text, "meas:3") ||
      containsIgnoreCase(text, "meas : 3") ||
      containsIgnoreCase(text, "meas 3") ||
      containsIgnoreCase(text, "results: 3") ||
      containsIgnoreCase(text, "results:3") ||
      containsIgnoreCase(text, "results 3") ||
      containsIgnoreCase(text, "resul 3") ||
      containsIgnoreCase(text, "resul: 3") ||
      containsIgnoreCase(text, "resul:3") ||
      containsIgnoreCase(text, "channel 3") ||
      containsIgnoreCase(text, "ch: 3") || containsIgnoreCase(text, "ch 3") ||
      containsIgnoreCase(text, "ch:3")) {
    return "Mix Milk";
  }

  if (containsIgnoreCase(text, "cal2") || containsIgnoreCase(text, "cal 2") ||
      containsIgnoreCase(text, "cal:2") ||
      containsIgnoreCase(text, "cal : 2") ||
      containsIgnoreCase(text, "meas: 2") ||
      containsIgnoreCase(text, "meas:2") ||
      containsIgnoreCase(text, "meas : 2") ||
      containsIgnoreCase(text, "meas 2") ||
      containsIgnoreCase(text, "results: 2") ||
      containsIgnoreCase(text, "results:2") ||
      containsIgnoreCase(text, "results 2") ||
      containsIgnoreCase(text, "resul 2") ||
      containsIgnoreCase(text, "resul: 2") ||
      containsIgnoreCase(text, "resul:2") ||
      containsIgnoreCase(text, "channel 2") ||
      containsIgnoreCase(text, "ch: 2") || containsIgnoreCase(text, "ch 2") ||
      containsIgnoreCase(text, "ch:2")) {
    return "Buffalo Milk";
  }

  if (containsIgnoreCase(text, "cal1") || containsIgnoreCase(text, "cal 1") ||
      containsIgnoreCase(text, "cal:1") ||
      containsIgnoreCase(text, "cal : 1") ||
      containsIgnoreCase(text, "meas: 1") ||
      containsIgnoreCase(text, "meas:1") ||
      containsIgnoreCase(text, "meas : 1") ||
      containsIgnoreCase(text, "meas 1") ||
      containsIgnoreCase(text, "results: 1") ||
      containsIgnoreCase(text, "results:1") ||
      containsIgnoreCase(text, "results 1") ||
      containsIgnoreCase(text, "resul 1") ||
      containsIgnoreCase(text, "resul: 1") ||
      containsIgnoreCase(text, "resul:1") ||
      containsIgnoreCase(text, "channel 1") ||
      containsIgnoreCase(text, "ch: 1") || containsIgnoreCase(text, "ch 1") ||
      containsIgnoreCase(text, "ch:1")) {
    return "Cow Milk";
  }

  return nullptr;
}

void DisplayRenderer::renderDegasserSetupCard(bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

#if ENABLE_MILK_BUBBLE_REMOVER
  static uint16_t last_duration = 0xFFFF;
  static uint32_t last_freq = 0xFFFFFFFF;
  static int8_t last_nuv_auto = -1;
  static uint8_t last_countdown = 0xFF;
  static bool last_saved = false;
  static bool setup_card_drawn = false;

  if (!BubbleRemover.isSetupMode() ||
      BubbleRemover.getSetupPage() != SETUP_PAGE_DEGASSER) {
    setup_card_drawn = false;
    return;
  }

  if (force_redraw) {
    setup_card_drawn = false;
  }

  uint16_t cur_duration = BubbleRemover.getDuration();
  uint32_t cur_freq = BubbleRemover.getFrequency();
  bool cur_nuv_auto = BubbleRemover.isAutoStartNuvoton();
  uint8_t cur_countdown = BubbleRemover.getSaveCountdown();
  bool cur_saved = BubbleRemover.justSaved();

  const uint16_t CARD_BG = 0x0841;     // Deep midnight slate
  const uint16_t CARD_BORDER = 0x07FF; // Cyan outline
  const uint16_t HEADER_BG = 0x18C3;   // Header bar
  const uint16_t TEXT_WHITE = 0xFFFF;
  const uint16_t TEXT_CYAN = 0x07FF;
  const uint16_t TEXT_AMBER = 0xFD20;
  const uint16_t TEXT_GREEN = 0x07E0;
  const uint16_t BOX_BG = 0x10A2;

  // 1. Draw Card Frame (Full redraw once or if forced)
  if (!setup_card_drawn) {
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT,
                   0x0000); // Clear background
    s_tft.fillRoundRect(10, 44, 300, 188, 8, CARD_BG);
    s_tft.drawRoundRect(10, 44, 300, 188, 8, CARD_BORDER);

    // Header Pill
    s_tft.fillRoundRect(12, 46, 296, 28, 6, HEADER_BG);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(TEXT_CYAN);
    s_tft.setTextSize(1);
    s_tft.setCursor(22, 65);
    s_tft.print("DEGASSING TIMER SETUP");
    s_tft.setFont(NULL);
    s_tft.setTextColor(0xBDD7);
    s_tft.setCursor(244, 56);
    s_tft.print("[1/2]");

    last_duration = 0xFFFF;
    last_freq = 0xFFFFFFFF;
    last_nuv_auto = -1;
    last_countdown = 0xFF;
    last_saved = false;
    setup_card_drawn = true;
  }

  // 2. Duration Row
  if (cur_duration != last_duration) {
    last_duration = cur_duration;

    // Value Box
    s_tft.fillRoundRect(20, 78, 140, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 78, 140, 34, 5, 0x39E7);

    char dur_str[16];
    snprintf(dur_str, sizeof(dur_str), "%d SEC", cur_duration);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(TEXT_WHITE);
    s_tft.setTextSize(1);
    s_tft.setCursor(30, 101);
    s_tft.print(dur_str);

    // Label & Help
    s_tft.setFont(NULL);
    s_tft.fillRect(170, 78, 130, 34, CARD_BG);
    s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 82);
    s_tft.print("DURATION TIME");
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(172, 98);
    s_tft.print("BTN 1: +1s (1-45s)");
  }

  // 3. Frequency Row (Display mapped number: 18..138, default 110)
  if (cur_freq != last_freq) {
    last_freq = cur_freq;

    s_tft.fillRoundRect(20, 116, 140, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 116, 140, 34, 5, 0x39E7);

    uint16_t freq_num = BubbleRemover.freqToNumber(cur_freq);
    char freq_str[16];
    snprintf(freq_str, sizeof(freq_str), "%u", freq_num);
    s_tft.setFont(&FreeSansBold12pt7b);
    s_tft.setTextColor(TEXT_AMBER);
    s_tft.setTextSize(1);
    s_tft.setCursor(freq_num >= 100 ? 46 : 58, 140);
    s_tft.print(freq_str);

    s_tft.setFont(NULL);
    s_tft.fillRect(170, 116, 130, 34, CARD_BG);
    s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 120);
    s_tft.print("FREQUENCY");
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(172, 136);
    s_tft.print("BTN 2: -  BTN 3: +");
  }

  // 4. Nuvoton Start Mode Row
  if ((int8_t)cur_nuv_auto != last_nuv_auto) {
    last_nuv_auto = (int8_t)cur_nuv_auto;

    s_tft.fillRoundRect(20, 154, 140, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 154, 140, 34, 5,
                        cur_nuv_auto ? TEXT_GREEN : TEXT_AMBER);

    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(cur_nuv_auto ? TEXT_GREEN : TEXT_AMBER);
    s_tft.setTextSize(1);
    s_tft.setCursor(cur_nuv_auto ? 22 : 22, 177);
    s_tft.print(cur_nuv_auto ? "AUTO START" : "MANUAL");

    s_tft.setFont(NULL);
    s_tft.fillRect(170, 154, 130, 34, CARD_BG);
    s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 158);
    s_tft.print("TRIGGER");
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(172, 174);
    s_tft.print("BTN 4: Auto/Manual");
  }

  // 5. Status Bottom Banner
  if (cur_countdown != last_countdown || cur_saved != last_saved) {
    last_countdown = cur_countdown;
    last_saved = cur_saved;

    s_tft.setFont(NULL);
    s_tft.fillRect(20, 194, 280, 26, CARD_BG);

    if (cur_saved) {
      s_tft.fillRoundRect(20, 194, 280, 24, 4, 0x02E0); // Green
      s_tft.drawRoundRect(20, 194, 280, 24, 4, TEXT_GREEN);
      s_tft.setTextColor(0xFFFF);
      s_tft.setTextSize(1);
      s_tft.setCursor(46, 202);
      s_tft.print("SETTINGS SAVED TO FLASH (NVS) !");
    } else {
      s_tft.fillRoundRect(20, 194, 280, 24, 4, 0x2124); // Dark amber
      s_tft.drawRoundRect(20, 194, 280, 24, 4, TEXT_AMBER);
      char save_str[36];
      snprintf(save_str, sizeof(save_str), "AUTO-SAVING TO FLASH IN %d s...",
               cur_countdown);
      s_tft.setTextColor(TEXT_AMBER);
      s_tft.setTextSize(1);
      s_tft.setCursor(44, 202);
      s_tft.print(save_str);
    }
  }
#endif
#endif
}

void DisplayRenderer::renderRtcSetupCard(bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

#if ENABLE_MILK_BUBBLE_REMOVER
  static uint16_t last_year = 0xFFFF;
  static uint8_t last_month = 0xFF;
  static uint8_t last_day = 0xFF;
  static uint8_t last_hour = 0xFF;
  static uint8_t last_min = 0xFF;
  static RtcEditField last_field = (RtcEditField)0xFF;
  static uint8_t last_countdown = 0xFF;
  static bool last_saved = false;
  static bool rtc_card_drawn = false;

  if (!BubbleRemover.isSetupMode() ||
      BubbleRemover.getSetupPage() != SETUP_PAGE_RTC) {
    rtc_card_drawn = false;
    return;
  }

  if (force_redraw) {
    rtc_card_drawn = false;
  }

  uint16_t cur_year;
  uint8_t cur_month, cur_day, cur_hour, cur_min;
  BubbleRemover.getRtcEditValues(cur_year, cur_month, cur_day, cur_hour,
                                 cur_min);
  RtcEditField cur_field = BubbleRemover.getRtcSelectedField();
  uint8_t cur_countdown = BubbleRemover.getSaveCountdown();
  bool cur_saved = BubbleRemover.justSaved();

  const uint16_t CARD_BG = 0x0841;     // Deep midnight slate
  const uint16_t CARD_BORDER = 0x07FF; // Cyan outline
  const uint16_t HEADER_BG = 0x18C3;   // Header bar
  const uint16_t TEXT_WHITE = 0xFFFF;
  const uint16_t TEXT_CYAN = 0x07FF;
  const uint16_t TEXT_AMBER = 0xFD20;
  const uint16_t TEXT_GREEN = 0x07E0;
  const uint16_t BOX_BG = 0x10A2;
  const uint16_t BOX_ACTIVE_BORDER = 0xFD20; // Amber highlight for active field
  const uint16_t BOX_INACTIVE_BORDER = 0x39E7;

  // 1. Draw Card Frame
  if (!rtc_card_drawn) {
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
    s_tft.fillRoundRect(10, 44, 300, 188, 8, CARD_BG);
    s_tft.drawRoundRect(10, 44, 300, 188, 8, CARD_BORDER);

    // Header Pill
    s_tft.fillRoundRect(12, 46, 296, 28, 6, HEADER_BG);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(TEXT_CYAN);
    s_tft.setTextSize(1);
    s_tft.setCursor(22, 65);
    s_tft.print("RTC DATE & TIME SETUP");
    s_tft.setFont(NULL);
    s_tft.setTextColor(0xBDD7);
    s_tft.setCursor(244, 56);
    s_tft.print("[2/2]");

    last_year = 0xFFFF;
    last_month = 0xFF;
    last_day = 0xFF;
    last_hour = 0xFF;
    last_min = 0xFF;
    last_field = (RtcEditField)0xFF;
    last_countdown = 0xFF;
    last_saved = false;
    rtc_card_drawn = true;
  }

  // 2. Date Row (Y = 78..112)
  bool date_changed = (cur_day != last_day) || (cur_month != last_month) ||
                      (cur_year != last_year) || (cur_field != last_field);
  if (date_changed) {
    last_day = cur_day;
    last_month = cur_month;
    last_year = cur_year;

    // Day Box
    bool day_active = (cur_field == RTC_EDIT_DAY);
    s_tft.fillRoundRect(20, 78, 38, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 78, 38, 34, 5,
                        day_active ? BOX_ACTIVE_BORDER : BOX_INACTIVE_BORDER);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(day_active ? TEXT_AMBER : TEXT_WHITE);
    s_tft.setCursor(26, 101);
    s_tft.printf("%02d", cur_day);

    // Separator 1
    s_tft.setFont(NULL);
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(61, 91);
    s_tft.print("-");

    // Month Box
    bool mon_active = (cur_field == RTC_EDIT_MONTH);
    s_tft.fillRoundRect(68, 78, 38, 34, 5, BOX_BG);
    s_tft.drawRoundRect(68, 78, 38, 34, 5,
                        mon_active ? BOX_ACTIVE_BORDER : BOX_INACTIVE_BORDER);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(mon_active ? TEXT_AMBER : TEXT_WHITE);
    s_tft.setCursor(74, 101);
    s_tft.printf("%02d", cur_month);

    // Separator 2
    s_tft.setFont(NULL);
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(109, 91);
    s_tft.print("-");

    // Year Box
    bool yr_active = (cur_field == RTC_EDIT_YEAR);
    s_tft.fillRoundRect(116, 78, 48, 34, 5, BOX_BG);
    s_tft.drawRoundRect(116, 78, 48, 34, 5,
                        yr_active ? BOX_ACTIVE_BORDER : BOX_INACTIVE_BORDER);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(yr_active ? TEXT_AMBER : TEXT_WHITE);
    s_tft.setCursor(119, 101);
    s_tft.printf("%04d", cur_year);

    // Right Label & Guide
    s_tft.setFont(NULL);
    s_tft.fillRect(170, 78, 130, 34, CARD_BG);
    s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 82);
    s_tft.print("DATE SETTING");
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(172, 98);
    s_tft.print("BTN 1: Next Field");
  }

  // 3. Time Row (Y = 116..150)
  bool time_changed = (cur_hour != last_hour) || (cur_min != last_min) ||
                      (cur_field != last_field);
  if (time_changed) {
    last_hour = cur_hour;
    last_min = cur_min;

    // Hour Box
    bool hr_active = (cur_field == RTC_EDIT_HOUR);
    s_tft.fillRoundRect(20, 116, 44, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 116, 44, 34, 5,
                        hr_active ? BOX_ACTIVE_BORDER : BOX_INACTIVE_BORDER);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(hr_active ? TEXT_AMBER : TEXT_WHITE);
    s_tft.setCursor(29, 139);
    s_tft.printf("%02d", cur_hour);

    // Colon Separator
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(0xBDD7);
    s_tft.setCursor(69, 137);
    s_tft.print(":");

    // Minute Box
    bool min_active = (cur_field == RTC_EDIT_MIN);
    s_tft.fillRoundRect(78, 116, 44, 34, 5, BOX_BG);
    s_tft.drawRoundRect(78, 116, 44, 34, 5,
                        min_active ? BOX_ACTIVE_BORDER : BOX_INACTIVE_BORDER);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(min_active ? TEXT_AMBER : TEXT_WHITE);
    s_tft.setCursor(87, 139);
    s_tft.printf("%02d", cur_min);

    // Right Label & Guide
    s_tft.setFont(NULL);
    s_tft.fillRect(170, 116, 130, 34, CARD_BG);
    s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 120);
    s_tft.print("TIME (24-HOUR)");
    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(172, 136);
    s_tft.print("BTN 2: -   BTN 3: +");
  }

  last_field = cur_field;

  // 4. Action Guide Row (Y = 154..188)
  if (!rtc_card_drawn || force_redraw) {
    s_tft.fillRoundRect(20, 154, 280, 34, 5, BOX_BG);
    s_tft.drawRoundRect(20, 154, 280, 34, 5, 0x39E7);
    s_tft.setFont(NULL);
    s_tft.setTextColor(TEXT_CYAN, BOX_BG);
    s_tft.setCursor(28, 160);
    s_tft.print("BTN 4: Save to RTC");
    s_tft.setTextColor(0xBDD7, BOX_BG);
    s_tft.setCursor(28, 174);
    s_tft.print("HOLD BTN 1: Switch to Degas Timer Setup");
  }

  // 5. Status Bottom Banner (Y = 194..220)
  if (!rtc_card_drawn || cur_saved != last_saved || force_redraw) {
    last_saved = cur_saved;

    s_tft.setFont(NULL);
    s_tft.fillRect(20, 194, 280, 26, CARD_BG);

    if (cur_saved) {
      s_tft.fillRoundRect(20, 194, 280, 24, 4, 0x02E0); // Green
      s_tft.drawRoundRect(20, 194, 280, 24, 4, TEXT_GREEN);
      s_tft.setTextColor(0xFFFF);
      s_tft.setTextSize(1);
      s_tft.setCursor(38, 202);
      s_tft.print("DATE & TIME SAVED TO RTC (DS1307)!");
    } else {
      s_tft.fillRoundRect(20, 194, 280, 24, 4, 0x2124); // Dark amber
      s_tft.drawRoundRect(20, 194, 280, 24, 4, TEXT_AMBER);
      s_tft.setTextColor(TEXT_AMBER);
      s_tft.setTextSize(1);
      s_tft.setCursor(48, 202);
      s_tft.print("BTN 4: CLICK TO SAVE TO RTC");
    }
  }
#endif
#endif
}

void DisplayRenderer::renderModernDegasserScreen(uint16_t remainingSec,
                                                 uint16_t totalDuration,
                                                 uint32_t freqHz,
                                                 bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  const uint16_t CARD_BG = 0x0841;     // Deep midnight slate
  const uint16_t CARD_BORDER = 0x07FF; // Cyan accent
  const uint16_t BANNER_BG = 0x1082;   // Banner background
  const uint16_t TEXT_WHITE = 0xFFFF;
  const uint16_t TEXT_CYAN = 0x07FF;
  const uint16_t TEXT_AMBER = 0xFD20;
  const uint16_t TEXT_GREEN = 0x07E0;
  const uint16_t BAR_TRACK = 0x1904; // Progress bar background
  const uint16_t BAR_FILL = 0x07FF;  // Progress bar fill cyan

  static bool s_degasser_card_drawn = false;
  static uint16_t s_last_drawn_rem = 0xFFFF;
  static uint32_t s_last_drawn_freq = 0;

  if (force_redraw) {
    s_degasser_card_drawn = false;
  }

  if (!s_degasser_card_drawn) {
    // 1. Clear active region below status bar
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);

    // 2. Outer Card Shell (X=10, Y=46, W=300, H=186)
    s_tft.fillRoundRect(10, 46, 300, 186, 8, CARD_BG);
    s_tft.drawRoundRect(10, 46, 300, 186, 8, CARD_BORDER);

    // 3. Top Banner
    s_tft.fillRoundRect(12, 48, 296, 36, 6, BANNER_BG);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(TEXT_CYAN);
    s_tft.setTextSize(1);
    s_tft.setCursor(22, 68);
    s_tft.print("ULTRASONIC MILK DEGASSING");
    s_tft.setFont(NULL);

    s_tft.setTextColor(0xBDD7, BANNER_BG);
    s_tft.setTextSize(1);
    s_tft.setCursor(22, 72);
    if (!BubbleRemover.isRunning()) {
      // s_tft.print("SET: Start | Hold SET: Setup | -/+: Freq");
    } else {
      s_tft.print("Clearing micro-bubbles before measurement");
    }

    // 4. Progress bar track (X=25, Y=146, W=270, H=12)
    s_tft.fillRoundRect(25, 146, 270, 12, 4, BAR_TRACK);
    s_tft.drawRoundRect(25, 146, 270, 12, 4, 0x39E7);

    // 5. Diagnostics Footer (Y=168..224)
    s_tft.drawFastHLine(20, 168, 280, 0x2104);

    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "FREQ: %-3u",
             BubbleRemover.freqToNumber(freqHz));
    s_tft.setTextColor(TEXT_AMBER, CARD_BG);
    s_tft.setTextSize(2);
    s_tft.setCursor(24, 190);
    s_tft.print(freq_str);

    s_tft.setTextSize(1);

    s_tft.setTextColor(0xBDD7, CARD_BG);
    s_tft.setCursor(170, 176);
#if ENABLE_MOSFET
    // s_tft.print(BubbleRemover.isRunning() ? "PWM: 50% Push-Pull" : "PWM:
    // Standby (0V)");
#else
    // s_tft.print("PWM: Inhibited (0V)");
#endif

    s_tft.setCursor(24, 194);
#if ENABLE_MOSFET
    // s_tft.setTextColor(TEXT_CYAN, CARD_BG);
    // s_tft.print("MOSFETS: Dual Half-Bridge");
#else
    // s_tft.setTextColor(0x94B2, CARD_BG);
    // s_tft.print("MOSFETS: Disabled (Macro)");
#endif

    s_tft.setTextColor(BubbleRemover.isAutoStartNuvoton() ? TEXT_GREEN : 0xFD20,
                       CARD_BG);
    // s_tft.setCursor(170, 194);
    s_tft.setTextSize(2);
    s_tft.setCursor(185, 190);
    s_tft.print(BubbleRemover.isAutoStartNuvoton() ? "AUTO-START" : "MANUAL");
    s_tft.setTextSize(1);
    s_tft.setCursor(24, 212);
#if ENABLE_MOSFET
    // s_tft.setTextColor(BubbleRemover.isRunning() ? TEXT_GREEN : 0xBDD7,
    // CARD_BG); s_tft.print(BubbleRemover.isRunning() ? "TRANSDUCER: Ultrasonic
    // Active" : "TRANSDUCER: Standby");
#else
    // s_tft.setTextColor(0x94B2, CARD_BG);
    // s_tft.print("TRANSDUCER: Inactive (Demo)");
#endif

    s_degasser_card_drawn = true;
    s_last_drawn_rem = 0xFFFF;
    s_last_drawn_freq = freqHz;
  }

  // 6. Live Frequency Update (when tuned via BTN 2 or BTN 3 while running,
  // without restarting timer!)
  if (freqHz != s_last_drawn_freq && s_degasser_card_drawn) {
    s_last_drawn_freq = freqHz;
    s_tft.fillRect(20, 186, 160, 24, CARD_BG);
    char live_freq_str[32];
    snprintf(live_freq_str, sizeof(live_freq_str), "FREQ: %-3u",
             BubbleRemover.freqToNumber(freqHz));
    s_tft.setFont(NULL);
    s_tft.setTextColor(TEXT_AMBER, CARD_BG);
    s_tft.setTextSize(2);
    s_tft.setCursor(24, 190);
    s_tft.print(live_freq_str);
    s_tft.setTextSize(1);
  }

  // 7. Incremental Countdown & Animated Progress Bar Update
  if (remainingSec != s_last_drawn_rem || force_redraw) {
    s_last_drawn_rem = remainingSec;

    // Center Timer Box (X=30, Y=90, W=260, H=48)
    s_tft.fillRect(30, 90, 260, 48, CARD_BG);

    char timer_str[24];
    snprintf(timer_str, sizeof(timer_str), "%d SEC", remainingSec);

    s_tft.setFont(&FreeSansBold18pt7b);
    s_tft.setTextColor(TEXT_WHITE);
    s_tft.setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    s_tft.getTextBounds(timer_str, 0, 0, &bx, &by, &bw, &bh);
    int16_t tx = 160 - (bw / 2);
    s_tft.setCursor(tx, 124);
    s_tft.print(timer_str);
    s_tft.setFont(NULL);

    if (!BubbleRemover.isRunning()) {
      s_tft.setTextColor(TEXT_AMBER, CARD_BG);
      s_tft.setTextSize(1);
      s_tft.setCursor(160 - 54, 130);
      s_tft.print("PRESS SET TO START");
    } else {
      s_tft.setTextColor(TEXT_CYAN, CARD_BG);
      s_tft.setTextSize(1);
      s_tft.setCursor(160 - 27, 130);
      s_tft.print("REMAINING");
    }

    // Progress Bar Fill
    if (totalDuration == 0)
      totalDuration = 15;
    int progress_pct = ((totalDuration - remainingSec) * 100) / totalDuration;
    if (progress_pct < 0)
      progress_pct = 0;
    if (progress_pct > 100)
      progress_pct = 100;
    int16_t fill_w = (progress_pct * (270 - 4)) / 100;

    s_tft.fillRect(27, 148, 270 - 4, 8, BAR_TRACK);
    if (fill_w > 0) {
      s_tft.fillRoundRect(27, 148, fill_w, 8, 3, BAR_FILL);
    }
  }

  if (remainingSec == 0) {
    s_degasser_card_drawn = false;
    s_last_drawn_freq = 0;
  }
#endif
}

static void formatWithSpacedColon(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0)
    return;
  size_t d = 0;
  for (size_t s = 0; src[s] != '\0' && d + 2 < dst_size; s++) {
    if (src[s] == ':') {
      if (d > 0 && dst[d - 1] != ' ') {
        dst[d++] = ' ';
      }
      dst[d++] = ':';
      if (src[s + 1] != '\0' && src[s + 1] != ' ' && d + 1 < dst_size) {
        dst[d++] = ' ';
      }
    } else {
      dst[d++] = src[s];
    }
  }
  dst[d] = '\0';
}

void DisplayRenderer::renderModernMeasuringScreen(const char *sample,
                                                  float temp, int countdown,
                                                  bool force_redraw) {
#if ENABLE_MILK_BUBBLE_REMOVER
  BubbleRemover.notifyMeasurementStarted();
#endif

  // Invalidate and purge previous test results so stale parameters can NEVER leak into the new test!
  m_last_fat = -1.0f;
  m_last_snf = -1.0f;
  m_last_density = -1.0f;
  m_last_protein = -1.0f;
  m_last_lactose = -1.0f;
  m_last_water = -1.0f;

#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  bool sample_changed = strcmp(sample, m_last_sample) != 0;
  bool temp_changed = (abs(temp - m_last_temp) > 0.05f);
  bool count_changed = (countdown != m_last_countdown);

  if (!force_redraw && !sample_changed && !temp_changed && !count_changed) {
    return;
  }

  // 1. Top Left Card: SAMPLE TYPE (X=8, Y=44, W=148, H=52)
  if (force_redraw || sample_changed) {
    strncpy(m_last_sample, sample, sizeof(m_last_sample));
    s_tft.fillRoundRect(8, 44, 148, 52, 6, MOD_CARD_BG);
    s_tft.drawRoundRect(8, 44, 148, 52, 6, MOD_CARD_BORDER);

    s_tft.setFont(&FreeSans9pt7b);
    s_tft.setTextColor(MOD_MUTED);
    s_tft.setTextSize(1);
    s_tft.setCursor(16, 62);
    s_tft.print("SAMPLE TYPE");

    s_tft.setFont(&FreeSansBold12pt7b);
    int16_t bx, by;
    uint16_t bw, bh;
    s_tft.getTextBounds(sample, 0, 0, &bx, &by, &bw, &bh);
    if (bw > (148 - 24)) {
      s_tft.setFont(&FreeSansBold9pt7b);
    }
    s_tft.setTextColor(MOD_WHITE);
    s_tft.setTextSize(1);
    s_tft.setCursor(16, 84);
    s_tft.print(sample);
    s_tft.setFont(NULL);
  }

  // 2. Top Right Card: TEMPERATURE (X=164, Y=44, W=148, H=52)
  if (force_redraw || temp_changed) {
    m_last_temp = temp;
    s_tft.fillRoundRect(164, 44, 148, 52, 6, MOD_CARD_BG);
    s_tft.drawRoundRect(164, 44, 148, 52, 6, MOD_CARD_BORDER);

    s_tft.setFont(&FreeSans9pt7b);
    s_tft.setTextColor(MOD_MUTED);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 62);
    s_tft.print("TEMPERATURE");

    s_tft.setFont(&FreeSansBold12pt7b);
    s_tft.setTextColor(MOD_AMBER);
    s_tft.setTextSize(1);
    s_tft.setCursor(172, 84);
    char t_buf[16];
    snprintf(t_buf, sizeof(t_buf), "%.1f", temp);
    s_tft.print(t_buf);
    int16_t bx, by;
    uint16_t bw, bh;
    s_tft.getTextBounds(t_buf, 172, 84, &bx, &by, &bw, &bh);
    s_tft.drawCircle(bx + bw + 4, 72, 2, MOD_AMBER);
    s_tft.setCursor(bx + bw + 10, 84);
    s_tft.print("C");
    s_tft.setFont(NULL);
  }

  // 3. Main Center Card: MEASURING PROGRESS (X=8, Y=102, W=304, H=132)
  if (force_redraw) {
    s_tft.fillRoundRect(8, 102, 304, 132, 8, MOD_CARD_BG);
    s_tft.drawRoundRect(8, 102, 304, 132, 8, MOD_CARD_BORDER);
    s_tft.drawFastHLine(18, 102, 284, MOD_CARD_HI);

    // Status badge pill (auto-measured width so it never overflows, centered
    // horizontally at X=160)
    s_tft.setFont(&FreeSansBold9pt7b);
    int16_t mx, my;
    uint16_t mw, mh;
    s_tft.getTextBounds("ANALYSIS IN PROGRESS", 0, 0, &mx, &my, &mw, &mh);
    int m_pill_w = mw + 16;
    if (m_pill_w > 280)
      m_pill_w = 280;
    int pill_x = 160 - (m_pill_w / 2);
    s_tft.fillRoundRect(pill_x, 108, m_pill_w, 22, 11,
                        0x0186); // Cyan dark tint
    s_tft.drawRoundRect(pill_x, 108, m_pill_w, 22, 11, MOD_CYAN);
    s_tft.setTextColor(MOD_CYAN);
    s_tft.setTextSize(1);
    s_tft.setCursor(160 - (mw / 2), 124);
    s_tft.print("ANALYSIS IN PROGRESS");

    // Center "SECONDS REMAINING" horizontally at X=160
    s_tft.setFont(&FreeSans9pt7b);
    s_tft.setTextColor(MOD_MUTED);
    int16_t sx, sy;
    uint16_t sw, sh;
    s_tft.getTextBounds("SECONDS REMAINING", 0, 0, &sx, &sy, &sw, &sh);
    s_tft.setCursor(160 - (sw / 2), 176);
    s_tft.print("SECONDS REMAINING");
    s_tft.setFont(NULL);
  }

  if (force_redraw || count_changed) {
    m_last_countdown = countdown;

    // Erase and redraw countdown number with FreeSansBold18pt7b centered
    // horizontally at X=160
    s_tft.fillRect(100, 130, 120, 32, MOD_CARD_BG);
    s_tft.setFont(&FreeSansBold18pt7b);
    s_tft.setTextColor(MOD_WHITE);
    s_tft.setTextSize(1);
    char cnt_buf[8];
    snprintf(cnt_buf, sizeof(cnt_buf), "%02d", countdown);
    int16_t cx, cy;
    uint16_t cw, ch;
    s_tft.getTextBounds(cnt_buf, 0, 0, &cx, &cy, &cw, &ch);
    s_tft.setCursor(160 - (cw / 2), 156);
    s_tft.print(cnt_buf);
    s_tft.setFont(NULL);

    // High-Precision Rounded Progress Bar (X=20, Y=190, W=280, H=14)
    const int bar_x = 20;
    const int bar_y = 190;
    const int bar_w = 280;
    const int bar_h = 14;

    s_tft.drawRoundRect(bar_x, bar_y, bar_w, bar_h, 7, MOD_CARD_BORDER);
    s_tft.fillRoundRect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 6,
                        0x0841); // Track

    // Calculate progress (30 sec down to 0 sec -> 0% to 100%)
    float pct = (30.0f - (float)countdown) / 30.0f;
    if (pct < 0.0f)
      pct = 0.0f;
    if (pct > 1.0f)
      pct = 1.0f;
    int fill_w = (int)(pct * (bar_w - 4));
    if (fill_w > 0) {
      uint16_t bar_color = (countdown <= 3) ? MOD_EMERALD : MOD_CYAN;
      s_tft.fillRoundRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 5,
                          bar_color);
      if (fill_w > 6) {
        s_tft.drawFastVLine(bar_x + 2 + fill_w - 2, bar_y + 3, bar_h - 6,
                            0xFFFF);
      }
    }
  }
  s_tft.setFont(NULL);
  s_tft.setTextSize(1);
#endif
}

void DisplayRenderer::renderModernResultsScreen(const char *sample, float fat,
                                                float snf, float density,
                                                float protein, float lactose,
                                                float water,
                                                bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  bool sample_changed = (strcmp(sample, m_last_results_sample) != 0);
  bool values_changed = (abs(fat - m_last_fat) > 0.05f) ||
                        (abs(snf - m_last_snf) > 0.05f) ||
                        (abs(density - m_last_density) > 0.05f) ||
                        (abs(protein - m_last_protein) > 0.05f) ||
                        (abs(lactose - m_last_lactose) > 0.05f) ||
                        (abs(water - m_last_water) > 0.05f) || sample_changed;

  if (!force_redraw && !values_changed) {
    return;
  }

  if (fat > 0.001f && snf > 0.001f) {
    m_last_fat = fat;
    m_last_snf = snf;
    if (density > 0.001f || m_last_density <= 0.001f)
      m_last_density = density;
    if (protein > 0.001f || m_last_protein <= 0.001f)
      m_last_protein = protein;
    if (lactose > 0.001f || m_last_lactose <= 0.001f)
      m_last_lactose = lactose;
    m_last_water = water;
  }
  strncpy(m_last_results_sample, sample, sizeof(m_last_results_sample) - 1);
  m_last_results_sample[sizeof(m_last_results_sample) - 1] = '\0';

#if ENABLE_MILK_BUBBLE_REMOVER
  if (fat > 0.001f && snf > 0.001f) {
    BubbleRemover.notifyResultsScreenDetected(sample, fat, snf, density, protein,
                                              lactose, water, m_last_temp);
  }
#endif

  // 1. Top Header Banner (Y = 41..66)
  s_tft.fillRect(0, 41, 320, 26, MOD_BG);

  // Left Status Pill: "Analysis Complete"
  const char *status_text = "Analysis Complete";
  uint16_t pill_bg = 0x0320;
  uint16_t pill_border = MOD_EMERALD;

  s_tft.setFont(&FreeSansBold9pt7b);
  int16_t p_bx, p_by;
  uint16_t p_bw, p_bh;
  s_tft.getTextBounds(status_text, 0, 0, &p_bx, &p_by, &p_bw, &p_bh);

  const int pill_x = 6;
  const int pill_y = 43;
  const int pill_w = p_bw + 14;
  const int pill_h = 22;

  s_tft.fillRoundRect(pill_x, pill_y, pill_w, pill_h, 6, pill_bg);
  s_tft.drawRoundRect(pill_x, pill_y, pill_w, pill_h, 6, pill_border);

  s_tft.setTextColor(0xFFFF);
  s_tft.setTextSize(1);
  s_tft.setCursor(pill_x + 7, pill_y + 16);
  s_tft.print(status_text);
  s_tft.setFont(NULL);

  // Right: Generic Milk Type (e.g. "Buffalo Milk", "Cow Milk") - Right-aligned
  // with collision prevention
  int max_sample_w = 314 - (pill_x + pill_w + 10);
  s_tft.setFont(&FreeSansBold12pt7b);
  int16_t s_bx, s_by;
  uint16_t s_bw, s_bh;
  s_tft.getTextBounds(sample, 0, 0, &s_bx, &s_by, &s_bw, &s_bh);

  if (s_bw > max_sample_w) {
    // If 12pt is too wide, automatically use Bold 9pt
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.getTextBounds(sample, 0, 0, &s_bx, &s_by, &s_bw, &s_bh);
  }

  int sample_x = 314 - s_bw;
  if (sample_x < pill_x + pill_w + 8) {
    sample_x = pill_x + pill_w + 8;
  }

  s_tft.setTextColor(MOD_WHITE);
  s_tft.setTextSize(1);
  s_tft.setCursor(sample_x, 60);
  s_tft.print(sample);
  s_tft.setFont(NULL);

  // Tile 1: FAT (X=8, Y=68, W=96, H=80)
  s_tft.fillRoundRect(8, 68, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(8, 68, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_CYAN);
  s_tft.setTextSize(1);
  s_tft.setCursor(16, 89);
  s_tft.print("FAT");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(14, 122);
  s_tft.printf("%04.1f", fat);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(82, 138);
  s_tft.print("%");
  s_tft.setFont(NULL);

  // Tile 2: SNF (X=112, Y=68, W=96, H=80)
  s_tft.fillRoundRect(112, 68, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(112, 68, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_AMBER);
  s_tft.setTextSize(1);
  s_tft.setCursor(120, 89);
  s_tft.print("SNF");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(118, 122);
  s_tft.printf("%04.1f", snf);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(186, 138);
  s_tft.print("%");
  s_tft.setFont(NULL);

  // Tile 3: DENSITY (X=216, Y=68, W=96, H=80)
  s_tft.fillRoundRect(216, 68, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(216, 68, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_VIOLET);
  s_tft.setTextSize(1);
  s_tft.setCursor(222, 89);
  s_tft.print("DENSITY");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(220, 122);
  s_tft.printf("%04.1f", density);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(260, 138);
  s_tft.print("g/cm3");
  s_tft.setFont(NULL);

  // Tile 4: PROTEIN (X=8, Y=154, W=96, H=80)
  s_tft.fillRoundRect(8, 154, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(8, 154, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_CORAL);
  s_tft.setTextSize(1);
  s_tft.setCursor(14, 175);
  s_tft.print("PROTEIN");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(14, 208);
  s_tft.printf("%04.1f", protein);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(82, 224);
  s_tft.print("%");
  s_tft.setFont(NULL);

  // Tile 5: LACTOSE (X=112, Y=154, W=96, H=80)
  s_tft.fillRoundRect(112, 154, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(112, 154, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_EMERALD);
  s_tft.setTextSize(1);
  s_tft.setCursor(118, 175);
  s_tft.print("LACTOSE");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(118, 208);
  s_tft.printf("%04.1f", lactose);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(186, 224);
  s_tft.print("%");
  s_tft.setFont(NULL);

  // Tile 6: WATER (X=216, Y=154, W=96, H=80)
  s_tft.fillRoundRect(216, 154, 96, 80, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(216, 154, 96, 80, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_ICE_BLUE);
  s_tft.setTextSize(1);
  s_tft.setCursor(224, 175);
  s_tft.print("WATER");

  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(220, 208);
  s_tft.printf("%04.1f", water);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(290, 224);
  s_tft.print("%");

  s_tft.setFont(NULL);
  s_tft.setTextSize(1);

#if ESP32_FORCE_RESTART
  ESP.restart();
#endif

#endif
}

void DisplayRenderer::renderModernResultsPage2Screen(const char *sample,
                                                     float temp, float fzp,
                                                     float solubility,
                                                     bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  bool values_changed = (abs(temp - m_last_p2_temp) > 0.05f) ||
                        (abs(fzp - m_last_p2_fzp) > 0.005f) ||
                        (abs(solubility - m_last_p2_solubility) > 0.05f) ||
                        (strcmp(sample, m_last_results_sample) != 0);

  if (!force_redraw && !values_changed) {
    return;
  }

  m_last_p2_temp = temp;
  m_last_p2_fzp = fzp;
  m_last_p2_solubility = solubility;
  strncpy(m_last_results_sample, sample, sizeof(m_last_results_sample) - 1);
  m_last_results_sample[sizeof(m_last_results_sample) - 1] = '\0';

  // 1. Top Header Banner (Y = 41..66)
  s_tft.fillRect(0, 41, 320, 26, MOD_BG);

  // Left Status Pill: "Page 2 Results" (auto-measured width)
  const char *p2_status = "Page 2 Results";
  s_tft.setFont(&FreeSansBold9pt7b);
  int16_t p2_bx, p2_by;
  uint16_t p2_bw, p2_bh;
  s_tft.getTextBounds(p2_status, 0, 0, &p2_bx, &p2_by, &p2_bw, &p2_bh);

  const int pill_x = 6;
  const int pill_y = 43;
  const int pill_w = p2_bw + 14;
  const int pill_h = 22;

  s_tft.fillRoundRect(pill_x, pill_y, pill_w, pill_h, 6,
                      0x2129); // Deep violet/indigo dark tint
  s_tft.drawRoundRect(pill_x, pill_y, pill_w, pill_h, 6,
                      MOD_VIOLET); // Neon violet

  s_tft.setTextColor(0xFFFF);
  s_tft.setTextSize(1);
  s_tft.setCursor(pill_x + 7, pill_y + 16);
  s_tft.print(p2_status);
  s_tft.setFont(NULL);

  // Right: Generic Milk Type (e.g. "Buffalo Milk", "Cow Milk") - Right-aligned
  // with collision prevention
  int max_sample_w = 314 - (pill_x + pill_w + 10);
  s_tft.setFont(&FreeSansBold12pt7b);
  int16_t s_bx, s_by;
  uint16_t s_bw, s_bh;
  s_tft.getTextBounds(sample, 0, 0, &s_bx, &s_by, &s_bw, &s_bh);

  if (s_bw > max_sample_w) {
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.getTextBounds(sample, 0, 0, &s_bx, &s_by, &s_bw, &s_bh);
  }

  int sample_x = 314 - s_bw;
  if (sample_x < pill_x + pill_w + 8) {
    sample_x = pill_x + pill_w + 8;
  }

  s_tft.setTextColor(MOD_WHITE);
  s_tft.setTextSize(1);
  s_tft.setCursor(sample_x, 60);
  s_tft.print(sample);
  s_tft.setFont(NULL);

  // Row 1: Two cards side-by-side (Y = 70, H = 76)
  // Card 1: TEMPERATURE (X=8, Y=70, W=148, H=76)
  s_tft.fillRoundRect(8, 70, 148, 76, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(8, 70, 148, 76, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_AMBER);
  s_tft.setTextSize(1);
  s_tft.setCursor(16, 91);
  s_tft.print("TEMPERATURE");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_AMBER);
  s_tft.setCursor(16, 126);
  char t_str[16];
  snprintf(t_str, sizeof(t_str), "%04.1f", temp);
  s_tft.print(t_str);
  int16_t t_bx, t_by;
  uint16_t t_bw, t_bh;
  s_tft.getTextBounds(t_str, 16, 126, &t_bx, &t_by, &t_bw, &t_bh);
  s_tft.drawCircle(t_bx + t_bw + 6, 110, 2, MOD_AMBER); // Degree symbol
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(t_bx + t_bw + 12, 126);
  s_tft.print("C");
  s_tft.setFont(NULL);

  // Card 2: FREEZING POINT (X=164, Y=70, W=148, H=76)
  s_tft.fillRoundRect(164, 70, 148, 76, 6, MOD_CARD_BG);
  s_tft.drawRoundRect(164, 70, 148, 76, 6, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_CYAN);
  s_tft.setTextSize(1);
  s_tft.setCursor(172, 91);
  s_tft.print("FREEZE");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_CYAN);
  s_tft.setCursor(168, 126);
  char fzp_str[16];
  snprintf(fzp_str, sizeof(fzp_str), "%+06.3f", fzp);
  s_tft.print(fzp_str);
  int16_t f_bx, f_by;
  uint16_t f_bw, f_bh;
  s_tft.getTextBounds(fzp_str, 168, 126, &f_bx, &f_by, &f_bw, &f_bh);
  s_tft.drawCircle(f_bx + f_bw + 4, 110, 2, MOD_CYAN); // Degree symbol
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(f_bx + f_bw + 10, 126);
  s_tft.print("C");
  s_tft.setFont(NULL);

  // Row 2: Centered Card - SOLUBILITY (X=60, Y=154, W=200, H=76)
  s_tft.fillRoundRect(60, 154, 200, 76, 8, MOD_CARD_BG);
  s_tft.drawRoundRect(60, 154, 200, 76, 8, MOD_CARD_BORDER);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_EMERALD);
  s_tft.setTextSize(1);
  int16_t lbx, lby;
  uint16_t lbw, lbh;
  s_tft.getTextBounds("SOLUBILITY", 0, 0, &lbx, &lby, &lbw, &lbh);
  s_tft.setCursor(60 + (200 - lbw) / 2, 175);
  s_tft.print("SOLUBILITY");
  s_tft.setFont(&FreeSansBold18pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(114, 212);
  char sol_str[16];
  snprintf(sol_str, sizeof(sol_str), "%04.1f", solubility);
  s_tft.print(sol_str);
  int16_t s_bx2, s_by2;
  uint16_t s_bw2, s_bh2;
  s_tft.getTextBounds(sol_str, 114, 212, &s_bx2, &s_by2, &s_bw2, &s_bh2);
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  s_tft.setCursor(s_bx2 + s_bw2 + 8, 212);
  s_tft.print("%");
  s_tft.setFont(NULL);
  s_tft.setTextSize(1);
#endif
}

void DisplayRenderer::renderModernSplashScreen(const char *vers,
                                               const char *date,
                                               const char *ser_num,
                                               bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  bool info_changed = (strcmp(vers, m_last_splash_vers) != 0) ||
                      (strcmp(date, m_last_splash_date) != 0) ||
                      (strcmp(ser_num, m_last_splash_ser) != 0);

  if (!m_splash_card_drawn) {
    force_redraw = true; // Always draw complete outer card, titles, and pill on
                         // first render!
  }

  if (!force_redraw && !info_changed)
    return;

  strncpy(m_last_splash_vers, vers, sizeof(m_last_splash_vers) - 1);
  m_last_splash_vers[sizeof(m_last_splash_vers) - 1] = '\0';
  strncpy(m_last_splash_date, date, sizeof(m_last_splash_date) - 1);
  m_last_splash_date[sizeof(m_last_splash_date) - 1] = '\0';
  strncpy(m_last_splash_ser, ser_num, sizeof(m_last_splash_ser) - 1);
  m_last_splash_ser[sizeof(m_last_splash_ser) - 1] = '\0';

  // Incremental detail lines update ONLY if full outer card was already drawn
  if (!force_redraw && info_changed && m_splash_card_drawn) {
    s_tft.fillRect(20, 102, 280, 74, MOD_CARD_BG);
    s_tft.setFont(&FreeSansBold9pt7b);
    s_tft.setTextColor(MOD_WHITE);
    s_tft.setCursor(30, 120);
    s_tft.printf("Vers :   %s", vers);
    s_tft.setCursor(30, 142);
    s_tft.printf("Date :   %s", date);
    s_tft.setCursor(30, 164);
    if (ser_num[0] == '#') {
      s_tft.printf("Serial : %s", ser_num);
    } else {
      s_tft.printf("Serial : #%s", ser_num);
    }
    s_tft.setFont(NULL);
    s_tft.setTextSize(1);
    return;
  }

  // Outer Card (Y = 44 to 232)
  s_tft.fillRoundRect(12, 44, 296, 188, 8, MOD_CARD_BG);
  s_tft.drawRoundRect(12, 44, 296, 188, 8, MOD_CARD_BORDER);
  s_tft.fillRect(24, 46, 272, 3, MOD_CYAN);

  // Brand Title - FreeSansBold12pt7b (Centered)
  s_tft.setFont(&FreeSansBold12pt7b);
  s_tft.setTextColor(MOD_CYAN);
  s_tft.setTextSize(1);
  int16_t b_bx, b_by;
  uint16_t b_bw, b_bh;
  s_tft.getTextBounds("SUN SMART", 0, 0, &b_bx, &b_by, &b_bw, &b_bh);
  s_tft.setCursor(160 - (b_bw / 2), 72);
  s_tft.print("SUN SMART");

  // Subtitle - FreeSans9pt7b (Centered directly below SUN SMART)
  s_tft.setFont(&FreeSans9pt7b);
  s_tft.setTextColor(MOD_MUTED);
  int16_t m_bx, m_by;
  uint16_t m_bw, m_bh;
  s_tft.getTextBounds("MILK ANALYZER", 0, 0, &m_bx, &m_by, &m_bw, &m_bh);
  s_tft.setCursor(160 - (m_bw / 2), 90);
  s_tft.print("MILK ANALYZER");

  s_tft.drawFastHLine(24, 98, 272, MOD_CARD_BORDER);

  // Detail lines - FreeSansBold9pt7b
  s_tft.setFont(&FreeSansBold9pt7b);
  s_tft.setTextColor(MOD_WHITE);
  s_tft.setCursor(30, 120);
  s_tft.printf("Vers :   %s", vers);
  s_tft.setCursor(30, 142);
  s_tft.printf("Date :   %s", date);
  s_tft.setCursor(30, 164);
  if (ser_num[0] == '#') {
    s_tft.printf("Serial : %s", ser_num);
  } else {
    s_tft.printf("Serial : #%s", ser_num);
  }

  // Ready Status Pill - Emerald pill with FreeSansBold12pt7b (Centered)
  s_tft.fillRoundRect(24, 180, 272, 36, 18, 0x0280); // Emerald pill
  s_tft.drawRoundRect(24, 180, 272, 36, 18, MOD_EMERALD);
  s_tft.setFont(&FreeSansBold12pt7b);
  s_tft.setTextColor(0xFFFF);
  int16_t r_bx, r_by;
  uint16_t r_bw, r_bh;
  s_tft.getTextBounds("SYSTEM READY", 0, 0, &r_bx, &r_by, &r_bw, &r_bh);
  s_tft.setCursor(160 - (r_bw / 2), 205);
  s_tft.print("SYSTEM READY");

  s_tft.setFont(NULL);
  s_tft.setTextSize(1);

  m_splash_card_drawn = true;
#endif
}

void DisplayRenderer::renderModernGenericCards(ST7920Emulator &emulator,
                                               bool force_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  const int card_x = 8;
  const int card_w = 304;
  const int card_h = 42;
  const int card_y_positions[4] = {46, 94, 142, 190};
  const uint16_t card_accents[4] = {MOD_CYAN, MOD_AMBER, MOD_VIOLET,
                                    MOD_EMERALD};

  char line_buf[17];

  for (uint8_t l = 0; l < 4; l++) {
    emulator.getTextLine(l, line_buf, sizeof(line_buf));

    // Sanitize unpadded Nuvoton menu overwrites (e.g. "Cal1-Cowfalo" ->
    // "Cal1-Cow", "Cal2-Duffmilk" -> "Cal2-Buffalo")
    if (strncmp(line_buf, "Cal1-Cow", 8) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal1-Cow", 8);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cal:1 Cow", 9) == 0 ||
               strncmp(line_buf, "Cal : 1 Cow", 11) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal : 1 Cow", 11);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cal2-Buf", 8) == 0 ||
               strncmp(line_buf, "Cal2-Duff", 9) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal2-Buffalo", 12);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cal:2 Buf", 9) == 0 ||
               strncmp(line_buf, "Cal : 2 Buf", 11) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal : 2 Buffalo", 15);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cal3-Mix", 8) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal3-Mix Milk", 13);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cal:3 Mix", 9) == 0 ||
               strncmp(line_buf, "Cal : 3 Mix", 11) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Cal : 3 Mix Milk", 16);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Cleaning", 8) == 0 &&
               strncmp(line_buf, "Cleaning Mode", 13) != 0) {
      char *col_ptr = strchr(line_buf, ':');
      if (col_ptr) {
        // Cleaning screen count: extract any number/text after colon cleanly
        char cycle_str[10] = {0};
        int c_idx = 0;
        char *p = col_ptr + 1;
        while (*p == ' ')
          p++;
        while (*p && *p != ' ' && c_idx < 8) {
          cycle_str[c_idx++] = *p++;
        }
        cycle_str[c_idx] = '\0';
        memset(line_buf, ' ', 16);
        if (c_idx > 0) {
          snprintf(line_buf, 17, "Cleaning : %s", cycle_str);
        } else {
          memcpy(line_buf, "Cleaning :", 10);
        }
        int slen = strlen(line_buf);
        while (slen < 16)
          line_buf[slen++] = ' ';
        line_buf[16] = '\0';
      } else {
        // Menu item "Cleaning"
        memset(line_buf, ' ', 16);
        memcpy(line_buf, "Cleaning", 8);
        line_buf[16] = '\0';
      }
    } else if (strncmp(line_buf, "Printing", 8) == 0 &&
               strncmp(line_buf, "Printing...", 11) != 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Printing", 8);
      line_buf[16] = '\0';
    } else if (containsIgnoreCase(line_buf, "Mode select") ||
               containsIgnoreCase(line_buf, "elector")) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Mode selector", 13);
      line_buf[16] = '\0';
    } else if (containsIgnoreCase(line_buf, "Flushing")) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Flushing Sensor", 15);
      line_buf[16] = '\0';
    } else if (strncmp(line_buf, "Pump: ACTIVE", 12) == 0 ||
               strncmp(line_buf, "Pump : ACTIVE", 13) == 0) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Pump : ACTIVE", 13);
      line_buf[16] = '\0';
    } else if (containsIgnoreCase(line_buf, "Please wait")) {
      memset(line_buf, ' ', 16);
      memcpy(line_buf, "Please wait...", 14);
      line_buf[16] = '\0';
    }

    static bool s_shadow_card_cursor[4] = {false, false, false, false};
    if (force_redraw) {
      memset(s_shadow_card_cursor, 0xFF, sizeof(s_shadow_card_cursor));
    }

    uint8_t cur_row = 0, cur_col = 0;
    emulator.getCursorPosition(cur_row, cur_col);
    bool is_cursor_row =
        (cur_row == l) && (emulator.isCursorOn() || emulator.isBlinkOn());

    // Track active/selected milk mode when cursor or indicator is on menu line
    if (is_cursor_row || line_buf[0] == '>' || line_buf[1] == '>' ||
        line_buf[0] == '*' || strstr(line_buf, "->") != NULL) {
      const char *menu_sample = detectMilkSampleType(line_buf);
      if (menu_sample) {
        strncpy(m_last_sample, menu_sample, sizeof(m_last_sample) - 1);
        m_last_sample[sizeof(m_last_sample) - 1] = '\0';
      }
    }

    bool text_changed =
        (memcmp(&m_shadow_text_buffer[l * 16], line_buf, 16) != 0);
    bool cursor_changed = (is_cursor_row != s_shadow_card_cursor[l]);

    if (!force_redraw && !text_changed && !cursor_changed) {
      continue;
    }
    memcpy(&m_shadow_text_buffer[l * 16], line_buf, 16);
    s_shadow_card_cursor[l] = is_cursor_row;

    int y = card_y_positions[l];

    s_tft.fillRoundRect(card_x, y, card_w, card_h, 6, MOD_CARD_BG);
    s_tft.drawRoundRect(card_x, y, card_w, card_h, 6,
                        is_cursor_row ? MOD_CYAN : MOD_CARD_BORDER);
    s_tft.fillRoundRect(card_x + 3, y + 5, 4, card_h - 10, 2,
                        is_cursor_row ? MOD_CYAN : card_accents[l]);

    // Format any text with colon to ensure clear spacing (e.g. "Cleaning : 1",
    // "PCB : 42")
    char card_text[32];
    formatWithSpacedColon(line_buf, card_text, sizeof(card_text));

    // Trim trailing spaces for clean vector typography
    int len = strlen(card_text);
    while (len > 0 && card_text[len - 1] == ' ') {
      card_text[--len] = '\0';
    }

    if (len > 0) {
      s_tft.setFont(&FreeSansBold12pt7b);
      int16_t bx, by;
      uint16_t bw, bh;
      s_tft.getTextBounds(card_text, 0, 0, &bx, &by, &bw, &bh);

      // If text exceeds card text width, gracefully scale to 9pt bold
      if (bw > (card_w - 36)) {
        s_tft.setFont(&FreeSansBold9pt7b);
      }

      s_tft.setTextColor(MOD_WHITE);
      s_tft.setTextSize(1);
      s_tft.setCursor(24, y + 27);
      s_tft.print(card_text);
    }
  }
  s_tft.setFont(NULL);
  s_tft.setTextSize(1);
#endif
}

void DisplayRenderer::renderModernDashboard(ST7920Emulator &emulator,
                                            bool force_full_redraw) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  if (!emulator.hasTextData()) {
    // DDRAM is blank / cleared (e.g. during 0x01 Clear Screen command).
    // Hold current display frame to prevent black flash / blank card flickers!
    return;
  }

  char l0[17], l1[17], l2[17], l3[17];
  emulator.getTextLine(0, l0, sizeof(l0));
  emulator.getTextLine(1, l1, sizeof(l1));
  emulator.getTextLine(2, l2, sizeof(l2));
  emulator.getTextLine(3, l3, sizeof(l3));

  bool is_settings =
      containsIgnoreCase(l0, "Setting") || containsIgnoreCase(l1, "Setting") ||
      containsIgnoreCase(l0, "Settings") ||
      containsIgnoreCase(l1, "Settings") || containsIgnoreCase(l0, "Baud") ||
      containsIgnoreCase(l1, "Baud") || containsIgnoreCase(l2, "Baud") ||
      containsIgnoreCase(l3, "Baud") || containsIgnoreCase(l0, "Format") ||
      containsIgnoreCase(l1, "Format");

  bool has_milk_fat = (strstr(l1, "F=") != NULL || strstr(l1, "f=") != NULL ||
                       strstr(l1, "F =") != NULL);
  bool has_milk_params =
      (strstr(l1, "S=") != NULL || strstr(l1, "s=") != NULL ||
       strstr(l2, "D=") != NULL || strstr(l2, "d=") != NULL ||
       strstr(l2, "P=") != NULL || strstr(l2, "p=") != NULL ||
       strstr(l3, "L=") != NULL || strstr(l3, "l=") != NULL ||
       strstr(l3, "W=") != NULL || strstr(l3, "w=") != NULL);

  uint8_t screen_type = 4; // Default: Generic
  if (strstr(l0, "SUN") != NULL || strstr(l1, "Vers:") != NULL ||
      strstr(l1, "Vers :") != NULL || containsIgnoreCase(l0, "Nuline") ||
      containsIgnoreCase(l0, "SL30")) {
    screen_type = 1; // Splash / Boot
  } else if (!is_settings &&
             (containsIgnoreCase(l0, "Page2") ||
              containsIgnoreCase(l0, "age2") || strstr(l2, "FrzP") != NULL ||
              strstr(l2, "F zP") != NULL || strstr(l2, "FzP") != NULL ||
              strstr(l2, "zP=") != NULL ||
              (strstr(l3, "S=") != NULL &&
               (strstr(l1, "Temp") != NULL || strstr(l2, "Temp") != NULL)))) {
    screen_type = 5; // Results Page 2 Screen
  } else if (!is_settings && has_milk_fat &&
             ((containsIgnoreCase(l0, "Results") &&
               !containsIgnoreCase(l0, "Page2")) ||
              has_milk_params)) {
    screen_type = 3; // Results Screen (Page 1)
  } else if (strstr(l0, "Meas:") != NULL || strstr(l0, "Meas :") != NULL ||
             (strstr(l1, "Temp") != NULL && !has_milk_fat && !is_settings)) {
    screen_type = 2; // Measuring / Countdown
  }

  // Transition Debounce: Prevent momentary drop to Generic Cards (screen_type =
  // 4) during in-flight screen clearing or line-by-line serial transmissions
  static uint8_t s_pending_screen_type = 0;
  static uint32_t s_pending_type_start_ms = 0;

  if (m_last_screen_type != 0 && m_last_screen_type != 4 && screen_type == 4) {
    uint32_t now_ms = millis();
    if (s_pending_screen_type != 4) {
      s_pending_screen_type = 4;
      s_pending_type_start_ms = now_ms;
      // Hold previous specialized screen while transition settles
      screen_type = m_last_screen_type;
    } else if (now_ms - s_pending_type_start_ms < 150) {
      // Still within 150ms settling window - hold previous screen!
      screen_type = m_last_screen_type;
    } else {
      // Settled on generic cards for >= 150ms: genuine menu transition!
      s_pending_screen_type = 0;
    }
  } else {
    s_pending_screen_type = 0;
  }

#if ENABLE_MILK_BUBBLE_REMOVER
  if (m_boot_phase == BOOT_PHASE_SUPPRESS_SPLASH && screen_type != 1) {
    m_boot_phase = BOOT_PHASE_RUNNING;
    m_last_screen_type =
        0xFF; // Force fresh redraw of the new operational screen
  }
#endif

  bool screen_type_changed =
      (screen_type != m_last_screen_type) || force_full_redraw;
  if (screen_type_changed) {
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, MOD_BG);
    m_last_screen_type = screen_type;
    m_splash_card_drawn = false;
    // Invalidate cursor tracking on screen switch so old cursor characters
    // never leak onto new screens
    m_last_cursor_visible = false;
    m_last_cursor_x = -1;
    m_last_cursor_y = -1;
    m_last_cursor_w = 0;
    m_last_cursor_h = 0;
    m_last_cursor_row = -1;
    m_last_cursor_col = -1;
  }

  switch (screen_type) {
  case 1: { // Splash / Boot Screen from Nuvoton
            // Skip / Suppress Nuvoton's received splash screen!
    // Nuvoton's incoming "Vers: 42", "Date: 07-04-15", "Serial: #0826" is
    // ignored. Our custom splash screen was displayed on startup.
#if ENABLE_MILK_BUBBLE_REMOVER
    if (m_boot_phase == BOOT_PHASE_SUPPRESS_SPLASH) {
      // Degassing finished, Nuvoton booting: show sleek transition status card
      static uint32_t s_last_trans_draw = 0;
      if (screen_type_changed || (millis() - s_last_trans_draw > 500)) {
        s_last_trans_draw = millis();
        s_tft.fillRoundRect(12, 54, 296, 172, 8, MOD_CARD_BG);
        s_tft.drawRoundRect(12, 54, 296, 172, 8, MOD_CYAN);
        s_tft.setFont(&FreeSansBold9pt7b);
        s_tft.setTextColor(0x07E0); // Green
        s_tft.setCursor(30, 95);
        s_tft.print("MILK SAMPLE DEGASSED");
        s_tft.setFont(NULL);
        s_tft.setTextColor(MOD_WHITE);
        s_tft.setTextSize(2);
        // s_tft.setCursor(30, 118);
        // s_tft.print("Starting measurement engine...");
        s_tft.setTextColor(MOD_CYAN);
        s_tft.setCursor(30, 142);
        s_tft.print("Press Any Button ");
        s_tft.setCursor(30, 165);
        s_tft.print("to Continue...");
        s_tft.setTextSize(1);
      }
      break;
    }
#endif

    // Render custom splash screen (never Nuvoton's received values)
    const char *esp_ser = getEsp32SerialNumber();
    renderModernSplashScreen(FIRMWARE_VERSION, STARTUP_SPLASH_DATE, esp_ser,
                             screen_type_changed);
    break;
  }

  case 2: { // Measuring Screen
    char formatted_sample[24] = {0};

    // 1. Detect sample from Line 0 or Line 1 using intelligent pattern matching
    const char *detected = detectMilkSampleType(l0);
    if (!detected) {
      detected = detectMilkSampleType(l1);
    }

    // Check full string after colon if colon exists (e.g. "Meas: C Milk",
    // "Meas: M MIx", "Meas: Goat")
    char *colon_ptr = strchr(l0, ':');
    if (!detected && colon_ptr) {
      detected = detectMilkSampleType(colon_ptr + 1);
    }

    if (detected) {
      strncpy(formatted_sample, detected, sizeof(formatted_sample) - 1);
    } else if (colon_ptr) {
      char temp_sample[16] = {0};
      sscanf(colon_ptr + 1, "%15s", temp_sample);
      const char *d = detectMilkSampleType(temp_sample);
      if (d) {
        strncpy(formatted_sample, d, sizeof(formatted_sample) - 1);
      } else if (strlen(temp_sample) > 0 &&
                 !containsIgnoreCase(temp_sample, "milk")) {
        snprintf(formatted_sample, sizeof(formatted_sample), "%s Milk",
                 temp_sample);
      } else if (strlen(temp_sample) > 0) {
        snprintf(formatted_sample, sizeof(formatted_sample), "%s", temp_sample);
      }
    }

    // 2. Fall back to sample tracked from menu or previous screen
    if (formatted_sample[0] == '\0') {
      const char *prev_d = detectMilkSampleType(m_last_sample);
      if (prev_d) {
        strncpy(formatted_sample, prev_d, sizeof(formatted_sample) - 1);
      } else if (strlen(m_last_sample) > 0 &&
                 !containsIgnoreCase(m_last_sample, "resul") &&
                 !containsIgnoreCase(m_last_sample, "page")) {
        strncpy(formatted_sample, m_last_sample, sizeof(formatted_sample) - 1);
      } else {
        strncpy(formatted_sample, "Cow Milk", sizeof(formatted_sample) - 1);
      }
    }
    if (formatted_sample[0] >= 'a' && formatted_sample[0] <= 'z') {
      formatted_sample[0] -= 32;
    }

    float temp = 34.7f;
    int countdown = 30;
    // Find temperature reading (matches "Temp =34.7", "Tep =34.7", "emp =34.7",
    // "mp =34.7")
    char *t_ptr = strstr(l1, "Temp");
    if (!t_ptr)
      t_ptr = strstr(l1, "Tep");
    if (!t_ptr)
      t_ptr = strstr(l1, "emp");
    if (!t_ptr)
      t_ptr = strstr(l1, "mp");
    if (!t_ptr)
      t_ptr = strchr(l1, '=');
    if (t_ptr) {
      char *eq_ptr = strchr(t_ptr, '=');
      if (!eq_ptr && (t_ptr[0] >= '0' && t_ptr[0] <= '9'))
        eq_ptr = t_ptr - 1;
      if (eq_ptr) {
        float t_val = 0;
        int c_val = 0;
        if (sscanf(eq_ptr + 1, "%f %d", &t_val, &c_val) >= 1) {
          temp = t_val;
          if (c_val >= 0 && c_val <= 99) {
            countdown = c_val;
          }
        }
      }
    }
    renderModernMeasuringScreen(formatted_sample, temp, countdown,
                                screen_type_changed);
    break;
  }

  case 3: { // Results Screen
    float fat = 0.0f, snf = 0.0f, density = 0.0f, protein = 0.0f,
          lactose = 0.0f;
    char *f_ptr = strstr(l1, "F=");
    if (f_ptr)
      sscanf(f_ptr + 2, "%f", &fat);

    char *s_ptr = strstr(l1, "S=");
    if (!s_ptr)
      s_ptr = strstr(l1, "S ");
    if (!s_ptr)
      s_ptr = strstr(l1, "s=");
    if (!s_ptr)
      s_ptr = strstr(l1, "s ");
    if (s_ptr) {
      int offset = 1;
      while (s_ptr[offset] == '=' || s_ptr[offset] == ' ' ||
             s_ptr[offset] == ':')
        offset++;
      sscanf(s_ptr + offset, "%f", &snf);
    }

    char *d_ptr = strstr(l2, "D=");
    if (d_ptr)
      sscanf(d_ptr + 2, "%f", &density);

    char *p_ptr = strstr(l2, "P=");
    if (!p_ptr)
      p_ptr = strstr(l2, "P ");
    if (!p_ptr)
      p_ptr = strstr(l2, "p=");
    if (!p_ptr)
      p_ptr = strstr(l2, "p ");
    if (p_ptr) {
      int offset = 1;
      while (p_ptr[offset] == '=' || p_ptr[offset] == ' ' ||
             p_ptr[offset] == ':')
        offset++;
      sscanf(p_ptr + offset, "%f", &protein);
    }

    char *l_ptr = strstr(l3, "L=");
    if (l_ptr)
      sscanf(l_ptr + 2, "%f", &lactose);

    // Water (W): parsed from Line 4 (L=00.0   W=00.0) with fallback; defaults
    // to 99.9 if absent
    float water = 99.9f;
    char *w_ptr = strstr(l3, "W=");
    if (!w_ptr)
      w_ptr = strstr(l2, "W=");
    if (!w_ptr)
      w_ptr = strstr(l1, "W=");
    if (w_ptr) {
      float w_val = 0.0f;
      if (sscanf(w_ptr + 2, "%f", &w_val) == 1) {
        water = w_val;
      }
    }

    // Extract Generic Milk Type (e.g. Buffalo Milk, Cow Milk, Mix Milk)
    char res_sample[24] = {0};

    // 1. Detect directly from Line 0 (header line)
    const char *d0 = detectMilkSampleType(l0);
    if (d0) {
      strncpy(res_sample, d0, sizeof(res_sample) - 1);
    }

    // 2. If not on Line 0, check other lines or after colon
    if (res_sample[0] == '\0') {
      char *colon_ptr = strchr(l0, ':');
      if (colon_ptr) {
        char extra[16] = {0};
        if (sscanf(colon_ptr + 1, "%15s", extra) == 1) {
          const char *d_col = detectMilkSampleType(extra);
          if (d_col) {
            strncpy(res_sample, d_col, sizeof(res_sample) - 1);
          }
        }
      }
    }

    // 3. Fall back to sample captured during measuring screen or menu selection
    if (res_sample[0] == '\0' && strlen(m_last_sample) > 0 &&
        !containsIgnoreCase(m_last_sample, "resul") &&
        !containsIgnoreCase(m_last_sample, "page")) {
      const char *d_prev = detectMilkSampleType(m_last_sample);
      if (d_prev) {
        strncpy(res_sample, d_prev, sizeof(res_sample) - 1);
      } else {
        strncpy(res_sample, m_last_sample, sizeof(res_sample) - 1);
      }
    }

    // 4. If still empty, check previous results sample
    if (res_sample[0] == '\0' && strlen(m_last_results_sample) > 0 &&
        !containsIgnoreCase(m_last_results_sample, "resul") &&
        !containsIgnoreCase(m_last_results_sample, "page")) {
      const char *d_p2 = detectMilkSampleType(m_last_results_sample);
      if (d_p2) {
        strncpy(res_sample, d_p2, sizeof(res_sample) - 1);
      } else {
        strncpy(res_sample, m_last_results_sample, sizeof(res_sample) - 1);
      }
    }

    // 5. Check other lines in case sample indicator is present
    if (res_sample[0] == '\0') {
      const char *d1 = detectMilkSampleType(l1);
      if (!d1)
        d1 = detectMilkSampleType(l2);
      if (!d1)
        d1 = detectMilkSampleType(l3);
      if (d1) {
        strncpy(res_sample, d1, sizeof(res_sample) - 1);
      }
    }

    // 6. Final fallback default
    if (res_sample[0] == '\0') {
      strncpy(res_sample, "Cow Milk", sizeof(res_sample) - 1);
    }
    res_sample[sizeof(res_sample) - 1] = '\0';

    // Synchronize sample state
    strncpy(m_last_sample, res_sample, sizeof(m_last_sample) - 1);
    m_last_sample[sizeof(m_last_sample) - 1] = '\0';
    strncpy(m_last_results_sample, res_sample,
            sizeof(m_last_results_sample) - 1);
    m_last_results_sample[sizeof(m_last_results_sample) - 1] = '\0';
    // In-flight parameter fallback handover during rapid transitions
    // Only allow fallback within an already-settled results screen, NEVER during screen transition!
    if (!screen_type_changed) {
      if (fat <= 0.001f && m_last_fat > 0.001f)
        fat = m_last_fat;
      if (snf <= 0.001f && m_last_snf > 0.001f)
        snf = m_last_snf;
      if (density <= 0.001f && m_last_density > 0.001f)
        density = m_last_density;
      if (protein <= 0.001f && m_last_protein > 0.001f)
        protein = m_last_protein;
      if (lactose <= 0.001f && m_last_lactose > 0.001f)
        lactose = m_last_lactose;
    }

    renderModernResultsScreen(res_sample, fat, snf, density, protein, lactose,
                              water, screen_type_changed);
    break;
  }

  case 5: { // Results Page 2 Screen
    float temp = 0.0f;
    // Search lines for temperature (case-insensitive)
    for (uint8_t l = 0; l < 4; l++) {
      char cur_line[17];
      emulator.getTextLine(l, cur_line, sizeof(cur_line));
      for (int i = 0; cur_line[i]; i++) {
        if ((cur_line[i] == 'T' || cur_line[i] == 't') &&
            (cur_line[i + 1] == 'e' || cur_line[i + 1] == 'E') &&
            (cur_line[i + 2] == 'm' || cur_line[i + 2] == 'M') &&
            (cur_line[i + 3] == 'p' || cur_line[i + 3] == 'P')) {
          char *eq = strchr(&cur_line[i], '=');
          if (eq) {
            float val = 0.0f;
            if (sscanf(eq + 1, "%f", &val) >= 1) {
              temp = val;
              break;
            }
          }
        }
      }
      if (temp > 0.0f)
        break;
    }
    if (temp <= 0.0f && m_last_temp > 0.0f) {
      temp = m_last_temp;
    }

    float fzp = 0.0f;
    for (uint8_t l = 0; l < 4; l++) {
      char cur_line[17];
      emulator.getTextLine(l, cur_line, sizeof(cur_line));
      char *f_ptr = strstr(cur_line, "zP=");
      if (!f_ptr)
        f_ptr = strstr(cur_line, "zp=");
      if (!f_ptr)
        f_ptr = strstr(cur_line, "ZP=");
      if (f_ptr) {
        char *eq = strchr(f_ptr, '=');
        if (eq) {
          float val = 0.0f;
          if (sscanf(eq + 1, "%f", &val) >= 1) {
            fzp = val;
            break;
          }
        }
      }
    }

    float solubility = 0.0f;
    for (uint8_t l = 0; l < 4; l++) {
      char cur_line[17];
      emulator.getTextLine(l, cur_line, sizeof(cur_line));
      char *s_ptr = strstr(cur_line, "S=");
      if (!s_ptr)
        s_ptr = strstr(cur_line, "s=");
      if (s_ptr) {
        float val = 0.0f;
        if (sscanf(s_ptr + 2, "%f", &val) >= 1) {
          solubility = val;
          break;
        }
      }
    }

    // Extract / reuse generic sample name
    char p2_sample[24] = "";
    const char *dp2 = detectMilkSampleType(l0);
    if (dp2) {
      strncpy(p2_sample, dp2, sizeof(p2_sample) - 1);
    } else if (strlen(m_last_results_sample) > 0 &&
               !containsIgnoreCase(m_last_results_sample, "resul") &&
               !containsIgnoreCase(m_last_results_sample, "page")) {
      const char *d = detectMilkSampleType(m_last_results_sample);
      if (d) {
        strncpy(p2_sample, d, sizeof(p2_sample) - 1);
      } else {
        strncpy(p2_sample, m_last_results_sample, sizeof(p2_sample) - 1);
      }
    } else if (strlen(m_last_sample) > 0 &&
               !containsIgnoreCase(m_last_sample, "resul") &&
               !containsIgnoreCase(m_last_sample, "page")) {
      const char *d = detectMilkSampleType(m_last_sample);
      if (d) {
        strncpy(p2_sample, d, sizeof(p2_sample) - 1);
      } else {
        strncpy(p2_sample, m_last_sample, sizeof(p2_sample) - 1);
      }
    }
    if (p2_sample[0] == '\0') {
      strncpy(p2_sample, "Cow Milk", sizeof(p2_sample) - 1);
    }
    p2_sample[sizeof(p2_sample) - 1] = '\0';

    strncpy(m_last_results_sample, p2_sample,
            sizeof(m_last_results_sample) - 1);
    m_last_results_sample[sizeof(m_last_results_sample) - 1] = '\0';
    strncpy(m_last_sample, p2_sample, sizeof(m_last_sample) - 1);
    m_last_sample[sizeof(m_last_sample) - 1] = '\0';
    // In-flight parameter fallback handover during rapid transitions
    if (temp <= 0.0f && m_last_p2_temp > 0.0f) {
      temp = m_last_p2_temp;
    } else if (temp <= 0.0f && m_last_temp > 0.0f) {
      temp = m_last_temp;
    }
    if (fzp == 0.0f && m_last_p2_fzp != -999.0f) {
      fzp = m_last_p2_fzp;
    }
    if (solubility == 0.0f && m_last_p2_solubility != -999.0f) {
      solubility = m_last_p2_solubility;
    }

    renderModernResultsPage2Screen(p2_sample, temp, fzp, solubility,
                                   screen_type_changed);
    break;
  }

  default: { // Generic Text Cards
    renderModernGenericCards(emulator, screen_type_changed);
    break;
  }
  }
#endif
}

void DisplayRenderer::update(ST7920Emulator &emulator, bool force_full_redraw) {
  m_frame_count++;
  uint32_t now = millis();
  if (now - m_last_fps_time >= 1000) {
    m_fps = (m_frame_count * 1000.0f) / (float)(now - m_last_fps_time);
    m_frame_count = 0;
    m_last_fps_time = now;
  }

#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  // 1. Render Top Mobile-Style Status Bar (Clock + Battery + Bus Status)
  renderTopStatusBar(emulator, force_full_redraw);

#if ENABLE_MILK_BUBBLE_REMOVER
  // Track transitions into/out of setup mode to force full redraw
  static bool s_was_setup_mode = false;
  bool is_setup = BubbleRemover.isSetupMode();
  bool setup_transition = (is_setup != s_was_setup_mode);
  if (setup_transition) {
    force_full_redraw = true;
    m_boot_phase = BOOT_PHASE_DEGASSER;
    s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
  }
  s_was_setup_mode = is_setup;

  // Setup Mode Card (has top priority if entered via Long Press or Boot-Hold at
  // any time)
  if (is_setup) {
    if (m_boot_phase == BOOT_PHASE_WELCOME) {
      m_boot_phase = BOOT_PHASE_DEGASSER;
    }
    static SetupPage s_last_setup_page = SETUP_PAGE_DEGASSER;
    SetupPage cur_page = BubbleRemover.getSetupPage();
    bool page_changed = (cur_page != s_last_setup_page);
    s_last_setup_page = cur_page;

    if (page_changed) {
      s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
      force_full_redraw = true;
    }

    if (cur_page == SETUP_PAGE_RTC) {
      renderRtcSetupCard(setup_transition || page_changed || force_full_redraw);
    } else {
      renderDegasserSetupCard(setup_transition || page_changed ||
                              force_full_redraw);
    }
    return;
  }

  // Startup Lifecycle Orchestration:
  // 1. Phase 0: Welcome Screen on Boot (0.0s - 3.0s)
  if (m_boot_phase == BOOT_PHASE_WELCOME) {
    const char *esp_serial = getEsp32SerialNumber();
    renderModernSplashScreen(FIRMWARE_VERSION, STARTUP_SPLASH_DATE, esp_serial,
                             force_full_redraw);
    if ((now - m_boot_phase_start_ms >= WELCOME_SCREEN_DURATION_MS) ||
        BubbleRemover.isRunning()) {
      // Transition to Dedicated Degasser Screen
      m_boot_phase = BOOT_PHASE_DEGASSER;
      m_boot_phase_start_ms = now;
      s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
      force_full_redraw = true;
      m_splash_card_drawn = false;
#if BUBBLE_REMOVER_AUTO_START_ON_BOOT
      BubbleRemover.startCycle(BubbleRemover.getDuration());
      m_degasser_started = true;
#else
      // Hold on the bubble remover screen and wait for SET button to start!
      m_degasser_started = false;
      if (!BubbleRemover.isSetupMode()) {
        BubbleRemover.enterReady();
      }
#endif
    }
    return;
  }

  // 2. Phase 1: Dedicated Degasser Screen
  if (m_boot_phase == BOOT_PHASE_DEGASSER || BubbleRemover.isRunning() ||
      BubbleRemover.isReady()) {
    if (m_boot_phase != BOOT_PHASE_DEGASSER) {
      // Re-entering degasser from sniffer mode -> reset standby latch
      m_degasser_started = false;
      m_boot_phase = BOOT_PHASE_DEGASSER;
      s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
      force_full_redraw = true;
    }

    static bool s_last_running_state = false;
    bool cur_running = BubbleRemover.isRunning();
    if (cur_running != s_last_running_state) {
      s_last_running_state = cur_running;
      force_full_redraw = true;
    }

    if (cur_running) {
      m_degasser_started = true;
    }

    uint16_t rem_sec = cur_running ? BubbleRemover.getRemainingSeconds()
                                   : BubbleRemover.getDuration();
    renderModernDegasserScreen(rem_sec, BubbleRemover.getDuration(),
                               BubbleRemover.getFrequency(), force_full_redraw);
    if (m_degasser_started && !cur_running) {
      // Degassing finished! Transition to suppress Nuvoton splash screen
      m_boot_phase = BOOT_PHASE_SUPPRESS_SPLASH;
      m_boot_phase_start_ms = now;
      s_tft.fillRect(0, TFT_STATUS_BAR_HEIGHT, 320, TFT_ACTIVE_HEIGHT, 0x0000);
      force_full_redraw = true;
    }
    return;
  }
#endif

  // 2. Render Active Display Mode
  if (m_scaling_mode == SCALE_MODE_MODERN_DASHBOARD) {
    renderModernDashboard(emulator, force_full_redraw);
    updateCursor(emulator);
  } else {
#if ENABLE_MILK_BUBBLE_REMOVER
    trackMeasurementAndPrinterBackground(emulator);
#endif
    if (m_scaling_mode == SCALE_MODE_LARGE_TEXT) {
      renderLargeText(emulator, force_full_redraw);
      updateCursor(emulator);
    } else {
      uint8_t current_fb[ST7920_BUFFER_SIZE];
      emulator.copyFramebuffer(current_fb);

      switch (m_scaling_mode) {
      case SCALE_MODE_FULL_STRETCH:
        renderFullStretch(current_fb, force_full_redraw);
        break;
      case SCALE_MODE_PROPORTIONAL_WIDE:
        renderProportionalWide(current_fb, emulator, force_full_redraw);
        break;
      case SCALE_MODE_CLASSIC_CENTERED:
      default:
        renderClassicCentered(current_fb, force_full_redraw);
        break;
      }
      updateCursor(emulator);
    }
  }
#endif
}

void DisplayRenderer::eraseCursorArea(ST7920Emulator &emulator, int16_t x,
                                      int16_t y, int16_t w, int16_t h) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available || x < 0 || w <= 0 || h <= 0)
    return;

  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
  if (m_scaling_mode == SCALE_MODE_MODERN_DASHBOARD) {
    // In Modern Dashboard mode, card selection is rendered cleanly via glowing
    // card borders in renderModernGenericCards() Do NOT draw destructive
    // fillRect boxes over card text!
    return;
  } else if (m_scaling_mode == SCALE_MODE_LARGE_TEXT) {
    s_tft.setFont(NULL);
    s_tft.fillRect(x, y, w, h, pal.tft_bg);
    if (m_last_cursor_row >= 0 && m_last_cursor_row < 4 &&
        m_last_cursor_col >= 0 && m_last_cursor_col < 16) {
      char ch = emulator.getTextCharAt((uint8_t)m_last_cursor_row,
                                       (uint8_t)m_last_cursor_col);
      s_tft.setTextColor(pal.tft_fg, pal.tft_bg);
      s_tft.setTextSize(3);
      s_tft.setCursor(x, y);
      s_tft.print(ch);
    }
  } else {
    s_tft.fillRect(x, y, w, h, pal.tft_bg);
  }
  s_tft.setFont(NULL);
  s_tft.setTextSize(1);
#endif
}

void DisplayRenderer::updateCursor(ST7920Emulator &emulator) {
#if ENABLE_PHYSICAL_TFT || ENABLE_UART_VIRTUAL_TFT
  if (!m_tft_available)
    return;

  // Modern dashboard handles card selection highlight cleanly in
  // renderModernGenericCards()
  if (m_scaling_mode == SCALE_MODE_MODERN_DASHBOARD) {
    m_last_cursor_visible = false;
    m_last_cursor_x = -1;
    m_last_cursor_y = -1;
    m_last_cursor_w = 0;
    m_last_cursor_h = 0;
    m_last_cursor_row = -1;
    m_last_cursor_col = -1;
    return;
  }

  // Check if cursor should be active
  bool should_display = false;
  if (m_cursor_mode == CURSOR_MODE_ALWAYS_ON) {
    should_display = true;
  } else if (m_cursor_mode == CURSOR_MODE_AUTO) {
    should_display = emulator.isCursorOn() || emulator.isBlinkOn();
  }

  if (!should_display) {
    if (m_last_cursor_visible) {
      eraseCursorArea(emulator, m_last_cursor_x, m_last_cursor_y,
                      m_last_cursor_w, m_last_cursor_h);
      m_last_cursor_visible = false;
      m_last_cursor_x = -1;
      m_last_cursor_row = -1;
      m_last_cursor_col = -1;
    }
    return;
  }

  // Blink timer (400 ms toggle interval)
  uint32_t now = millis();
  if (now - m_last_cursor_blink_toggle >= 400) {
    m_last_cursor_blink_toggle = now;
    m_cursor_blink_state = !m_cursor_blink_state;
  }

  bool blink_required =
      (m_cursor_mode == CURSOR_MODE_ALWAYS_ON) || emulator.isBlinkOn();
  bool is_visible = !blink_required || m_cursor_blink_state;

  uint8_t row = 0, col = 0;
  emulator.getCursorPosition(row, col);

  // Modern dashboard generic cards: snap cursor to column 0 if address is left
  // in trailing empty space
  if (m_scaling_mode == SCALE_MODE_MODERN_DASHBOARD &&
      m_last_screen_type == 4) {
    char card_text[17] = {0};
    emulator.getTextLine(row, card_text, sizeof(card_text));
    int text_len = strlen(card_text);
    while (text_len > 0 && card_text[text_len - 1] == ' ') {
      text_len--;
    }
    if (text_len == 0) {
      is_visible = false;
    } else if (col >= text_len) {
      col = 0; // Snap cursor to the first character of the selected menu item!
    }
  }

  const ThemeColors &pal = THEME_PALETTES[m_current_theme];
  int16_t cur_x = 0, cur_y = 0, cur_w = 0, cur_h = 0;

  switch (m_scaling_mode) {
  case SCALE_MODE_MODERN_DASHBOARD: {
    const int card_y_positions[4] = {46, 94, 142, 190};
    cur_x = 24 + (col * 12);
    cur_y = card_y_positions[row] + 31;
    cur_w = 12;
    cur_h = 3;
    break;
  }

  case SCALE_MODE_LARGE_TEXT: {
    const int card_y_positions[4] = {46, 94, 142, 190};
    cur_x = 16 + (col * 18);
    cur_y = card_y_positions[row] + 9;
    cur_w = 18;
    cur_h = 24;
    break;
  }

  case SCALE_MODE_CLASSIC_CENTERED:
    cur_x = TFT_OFFSET_X + (col * 16);
    cur_y = TFT_OFFSET_Y + (row * 32) + 26;
    cur_w = 16;
    cur_h = 4;
    break;

  case SCALE_MODE_FULL_STRETCH:
    cur_x = col * 20;
    cur_y = TFT_STATUS_BAR_HEIGHT + ((row * 16 + 13) * TFT_ACTIVE_HEIGHT) / 64;
    cur_w = 20;
    cur_h = 5;
    break;

  case SCALE_MODE_PROPORTIONAL_WIDE:
    cur_x = col * 20;
    cur_y = TFT_STATUS_BAR_HEIGHT + (row * 40) + 33;
    cur_w = 20;
    cur_h = 5;
    break;

  default:
    break;
  }

  // If cursor moved or visibility state changed, erase old cursor first
  if (m_last_cursor_visible &&
      (m_last_cursor_x != cur_x || m_last_cursor_y != cur_y || !is_visible)) {
    eraseCursorArea(emulator, m_last_cursor_x, m_last_cursor_y, m_last_cursor_w,
                    m_last_cursor_h);
    m_last_cursor_visible = false;
  }

  // Draw new cursor if visible
  if (is_visible && cur_w > 0) {
    if (m_scaling_mode == SCALE_MODE_MODERN_DASHBOARD) {
      s_tft.fillRect(cur_x, cur_y, cur_w, cur_h, MOD_CYAN);
    } else if (m_scaling_mode == SCALE_MODE_LARGE_TEXT) {
      char ch = emulator.getTextCharAt(row, col);
      s_tft.fillRect(cur_x, cur_y, cur_w, cur_h, pal.tft_fg);
      s_tft.setFont(NULL);
      s_tft.setTextColor(pal.tft_bg, pal.tft_fg);
      s_tft.setTextSize(3);
      s_tft.setCursor(cur_x, cur_y);
      s_tft.print(ch);
    } else {
      uint16_t cursor_color = (pal.tft_fg == pal.tft_bg) ? 0xFFFF : pal.tft_fg;
      s_tft.fillRect(cur_x, cur_y, cur_w, cur_h, cursor_color);
    }
    m_last_cursor_x = cur_x;
    m_last_cursor_y = cur_y;
    m_last_cursor_w = cur_w;
    m_last_cursor_h = cur_h;
    m_last_cursor_row = row;
    m_last_cursor_col = col;
    m_last_cursor_visible = true;
    s_tft.setFont(NULL);
    s_tft.setTextSize(1);
  }
#endif
}

void DisplayRenderer::trackMeasurementAndPrinterBackground(ST7920Emulator &emulator) {
#if ENABLE_MILK_BUBBLE_REMOVER
  if (!emulator.hasTextData()) {
    return;
  }
  char l0[17], l1[17], l2[17], l3[17];
  emulator.getTextLine(0, l0, sizeof(l0));
  emulator.getTextLine(1, l1, sizeof(l1));
  emulator.getTextLine(2, l2, sizeof(l2));
  emulator.getTextLine(3, l3, sizeof(l3));

  bool is_settings =
      containsIgnoreCase(l0, "Setting") || containsIgnoreCase(l1, "Setting") ||
      containsIgnoreCase(l0, "Settings") ||
      containsIgnoreCase(l1, "Settings") || containsIgnoreCase(l0, "Baud") ||
      containsIgnoreCase(l1, "Baud") || containsIgnoreCase(l2, "Baud") ||
      containsIgnoreCase(l3, "Baud") || containsIgnoreCase(l0, "Format") ||
      containsIgnoreCase(l1, "Format");

  bool has_milk_fat = (strstr(l1, "F=") != NULL || strstr(l1, "f=") != NULL ||
                       strstr(l1, "F =") != NULL);
  bool has_milk_params =
      (strstr(l1, "S=") != NULL || strstr(l1, "s=") != NULL ||
       strstr(l2, "D=") != NULL || strstr(l2, "d=") != NULL ||
       strstr(l2, "P=") != NULL || strstr(l2, "p=") != NULL ||
       strstr(l3, "L=") != NULL || strstr(l3, "l=") != NULL ||
       strstr(l3, "W=") != NULL || strstr(l3, "w=") != NULL);

  if (!is_settings && (strstr(l0, "Meas:") != NULL || strstr(l0, "Meas :") != NULL ||
                       (strstr(l1, "Temp") != NULL && !has_milk_fat))) {
    // Invalidate stale results and arm auto-print
    BubbleRemover.notifyMeasurementStarted();
    m_last_fat = -1.0f;
    m_last_snf = -1.0f;
    m_last_density = -1.0f;
    m_last_protein = -1.0f;
    m_last_lactose = -1.0f;
    m_last_water = -1.0f;

    char *t_ptr = strstr(l1, "Temp");
    if (!t_ptr) t_ptr = strstr(l1, "Tep");
    if (!t_ptr) t_ptr = strstr(l1, "emp");
    if (!t_ptr) t_ptr = strstr(l1, "mp");
    if (t_ptr) {
      char *eq_ptr = strchr(t_ptr, '=');
      if (eq_ptr) {
        float t_val = 0.0f;
        if (sscanf(eq_ptr + 1, "%f", &t_val) >= 1) {
          m_last_temp = t_val;
        }
      }
    }
  } else if (!is_settings && has_milk_fat &&
             ((containsIgnoreCase(l0, "Results") && !containsIgnoreCase(l0, "Page2")) ||
              has_milk_params)) {
    float fat = 0.0f, snf = 0.0f, density = 0.0f, protein = 0.0f, lactose = 0.0f, water = 99.9f;
    char *f_ptr = strstr(l1, "F=");
    if (f_ptr) sscanf(f_ptr + 2, "%f", &fat);
    char *s_ptr = strstr(l1, "S=");
    if (!s_ptr) s_ptr = strstr(l1, "s=");
    if (s_ptr) {
      int offset = 1;
      while (s_ptr[offset] == '=' || s_ptr[offset] == ' ' || s_ptr[offset] == ':') offset++;
      sscanf(s_ptr + offset, "%f", &snf);
    }
    char *d_ptr = strstr(l2, "D=");
    if (d_ptr) sscanf(d_ptr + 2, "%f", &density);
    char *p_ptr = strstr(l2, "P=");
    if (!p_ptr) p_ptr = strstr(l2, "p=");
    if (p_ptr) {
      int offset = 1;
      while (p_ptr[offset] == '=' || p_ptr[offset] == ' ' || p_ptr[offset] == ':') offset++;
      sscanf(p_ptr + offset, "%f", &protein);
    }
    char *l_ptr = strstr(l3, "L=");
    if (l_ptr) sscanf(l_ptr + 2, "%f", &lactose);
    char *w_ptr = strstr(l3, "W=");
    if (!w_ptr) w_ptr = strstr(l2, "W=");
    if (!w_ptr) w_ptr = strstr(l1, "W=");
    if (w_ptr) {
      float w_val = 0.0f;
      if (sscanf(w_ptr + 2, "%f", &w_val) == 1) water = w_val;
    }
    const char *sample_name = detectMilkSampleType(l0);
    if (!sample_name) sample_name = detectMilkSampleType(l1);
    if (!sample_name) sample_name = "Cow Milk";

    if (fat > 0.001f && snf > 0.001f) {
      m_last_fat = fat;
      m_last_snf = snf;
      if (density > 0.001f) m_last_density = density;
      if (protein > 0.001f) m_last_protein = protein;
      if (lactose > 0.001f) m_last_lactose = lactose;
      m_last_water = water;
      BubbleRemover.notifyResultsScreenDetected(sample_name, fat, snf, density, protein, lactose, water, m_last_temp);
    }
  }
#endif
}

