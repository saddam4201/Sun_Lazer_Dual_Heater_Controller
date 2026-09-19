#ifndef DISPLAY_RENDERER_H
#define DISPLAY_RENDERER_H

#include <Arduino.h>
#include "config.h"
#include "st7920_protocol.h"

enum InstrumentBootPhase {
    BOOT_PHASE_WELCOME = 0,        // 0..3s: Show Initial Welcome Splash
    BOOT_PHASE_DEGASSER = 1,        // 3..18s: Show Dedicated Degasser Screen with Countdown
    BOOT_PHASE_SUPPRESS_SPLASH = 2, // Degassing done, Nuvoton booting: suppress Nuvoton splash
    BOOT_PHASE_RUNNING = 3          // Normal instrument operation
};

class DisplayRenderer {
public:
    DisplayRenderer();
    bool begin();
    
    // Update the physical TFT screen from the ST7920 emulator
    void update(ST7920Emulator& emulator, bool force_full_redraw = false);

    // Theme management
    void setTheme(LCDTheme theme);
    LCDTheme getTheme() const { return m_current_theme; }
    void nextTheme();

    // Scaling mode management
    void setScalingMode(DisplayScalingMode mode);
    DisplayScalingMode getScalingMode() const { return m_scaling_mode; }
    void nextScalingMode();
    const char* getScalingModeName() const;

    // Cursor display management
    void setCursorMode(CursorDisplayMode mode);
    CursorDisplayMode getCursorMode() const { return m_cursor_mode; }
    void nextCursorMode();
    const char* getCursorModeName() const;

    // Top Mobile-Style Status Bar
    void setClock(uint8_t hours, uint8_t minutes, uint8_t seconds = 0);
    void setBatteryLevel(uint8_t pct, bool charging = false);
    void renderTopStatusBar(ST7920Emulator& emulator, bool force_redraw = false);

    // Stats
    float getMeasuredFPS() const { return m_fps; }

    // Unique ESP32 Chip Serial Number
    static const char* getEsp32SerialNumber();

private:
    LCDTheme m_current_theme;
    DisplayScalingMode m_scaling_mode;
    CursorDisplayMode m_cursor_mode;
    uint8_t m_shadow_framebuffer[ST7920_BUFFER_SIZE];
    char m_shadow_text_buffer[64];
    bool m_tft_available;
    uint32_t m_last_telemetry_time;

    // Top Status Bar tracking
    uint32_t m_last_status_bar_time;
    uint8_t m_battery_pct;
    bool m_battery_charging;
    uint32_t m_clock_offset_seconds;

    // Cursor tracking
    int16_t m_last_cursor_x;
    int16_t m_last_cursor_y;
    int16_t m_last_cursor_w;
    int16_t m_last_cursor_h;
    int8_t  m_last_cursor_row;
    int8_t  m_last_cursor_col;
    bool m_last_cursor_visible;
    uint32_t m_last_cursor_blink_toggle;
    bool m_cursor_blink_state;

    // FPS calculation
    uint32_t m_frame_count;
    uint32_t m_last_fps_time;
    float m_fps;

    // Modern Smart Instrument Dashboard Engine
    uint8_t m_last_screen_type;
    int m_last_countdown;
    float m_last_temp;
    char m_last_sample[24];
    char m_last_results_sample[24];
    float m_last_fat, m_last_snf, m_last_density, m_last_protein, m_last_lactose, m_last_water;
    float m_last_p2_temp, m_last_p2_fzp, m_last_p2_solubility;
    char m_last_splash_vers[16];
    char m_last_splash_date[16];
    char m_last_splash_ser[16];
    bool m_splash_card_drawn;

    void drawBezelAndBackground();
    void renderModernDashboard(ST7920Emulator& emulator, bool force_full_redraw);
    void renderModernMeasuringScreen(const char* sample, float temp, int countdown, bool force_redraw);
    void renderModernResultsScreen(const char* sample, float fat, float snf, float density, float protein, float lactose, float water, bool force_redraw);
    void renderModernResultsPage2Screen(const char* sample, float temp, float fzp, float solubility, bool force_redraw);
    void renderModernSplashScreen(const char* vers, const char* date, const char* ser_num, bool force_redraw);
    void renderModernGenericCards(ST7920Emulator& emulator, bool force_redraw);

    void renderClassicCentered(const uint8_t* current_fb, bool force_full_redraw);
    void renderFullStretch(const uint8_t* current_fb, bool force_full_redraw);
    void renderProportionalWide(const uint8_t* current_fb, ST7920Emulator& emulator, bool force_full_redraw);
    void renderLargeText(ST7920Emulator& emulator, bool force_full_redraw);
    void renderDegasserSetupCard(bool force_redraw = false);
    void renderRtcSetupCard(bool force_redraw = false);
    void renderModernDegasserScreen(uint16_t remainingSec, uint16_t totalDuration, uint32_t freqHz, bool force_redraw = false);
    void updateTelemetryBar(ST7920Emulator& emulator);
    void updateCursor(ST7920Emulator& emulator);
    void eraseCursorArea(ST7920Emulator& emulator, int16_t x, int16_t y, int16_t w, int16_t h);
    void trackMeasurementAndPrinterBackground(ST7920Emulator& emulator);

    // Startup Lifecycle Management
    InstrumentBootPhase m_boot_phase;
    uint32_t m_boot_phase_start_ms;
    uint16_t m_last_degas_remaining;
    bool m_degasser_started;
};

extern DisplayRenderer Renderer;

#endif // DISPLAY_RENDERER_H
