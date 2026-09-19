#ifndef SERIAL_TFT_BRIDGE_H
#define SERIAL_TFT_BRIDGE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "config.h"

/**
 * @class Dual_ILI9341
 * @brief Dual-output display driver that writes simultaneously to:
 *        1. Physical ILI9341 TFT display via Hardware SPI (if ENABLE_PHYSICAL_TFT is active)
 *        2. Virtual TFT Display over UART Serial (if ENABLE_UART_VIRTUAL_TFT is active)
 *
 * Drops in directly in place of Adafruit_ILI9341 with zero changes to existing UI logic.
 */
class Dual_ILI9341 : public Adafruit_ILI9341 {
public:
    Dual_ILI9341(int8_t cs, int8_t dc, int8_t rst = -1)
        : Adafruit_ILI9341(cs, dc, rst),
          _uartStream(&Serial),
          _uart_enabled(true),
          _physical_enabled(true),
          _suppress_pixel_uart(false),
          _window_x0(0), _window_y0(0),
          _window_w(320), _window_h(240) {}

    void setUartStream(Stream* s) { _uartStream = s; }
    void setUartEnabled(bool en)  { _uart_enabled = en; }
    void setPhysicalEnabled(bool en) { _physical_enabled = en; }
    bool isUartEnabled() const    { return _uart_enabled; }
    bool isPhysicalEnabled() const { return _physical_enabled; }

    void begin(uint32_t freq = 0) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::begin(freq);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->println(F("ROTA,1"));
            _uartStream->println(F("CLS,0x0000"));
        }
#endif
    }

    void setRotation(uint8_t r) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::setRotation(r);
        }
#endif
        Adafruit_GFX::setRotation(r);
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->print(F("ROTA,"));
            _uartStream->println(r & 3);
        }
#endif
    }

    void fillScreen(uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::fillScreen(color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->print(F("CLS,0x"));
            if (color < 0x1000) _uartStream->print('0');
            if (color < 0x0100) _uartStream->print('0');
            if (color < 0x0010) _uartStream->print('0');
            _uartStream->println(color, HEX);
        }
#endif
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::fillRect(x, y, w, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("RECT,%d,%d,%d,%d,0x%04X,1\n", x, y, w, h, color);
        }
#endif
    }

    void fillRectPhysicalOnly(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::fillRect(x, y, w, h, color);
        }
#endif
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawRect(x, y, w, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("RECT,%d,%d,%d,%d,0x%04X,0\n", x, y, w, h, color);
        }
#endif
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawFastHLine(x, y, w, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", x, y, x + w - 1, y, color);
        }
#endif
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawFastVLine(x, y, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", x, y, x, y + h - 1, color);
        }
#endif
    }

    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawLine(x0, y0, x1, y1, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", x0, y0, x1, y1, color);
        }
#endif
    }

    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawCircle(x, y, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("CIRC,%d,%d,%d,0x%04X,0\n", x, y, r, color);
        }
#endif
    }

    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::fillCircle(x, y, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("CIRC,%d,%d,%d,0x%04X,1\n", x, y, r, color);
        }
#endif
    }

    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawRoundRect(x, y, w, h, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("RRECT,%d,%d,%d,%d,%d,0x%04X,0\n", x, y, w, h, r, color);
        }
#endif
    }

    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::fillRoundRect(x, y, w, h, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("RRECT,%d,%d,%d,%d,%d,0x%04X,1\n", x, y, w, h, r, color);
        }
#endif
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::drawPixel(x, y, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_suppress_pixel_uart) {
            _uartStream->printf("PIX,%d,%d,0x%04X\n", x, y, color);
        }
#endif
    }

    // Hardware SPI transaction pass-throughs
    void startWrite() {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) Adafruit_ILI9341::startWrite();
#endif
    }

    void endWrite() {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) Adafruit_ILI9341::endWrite();
#endif
    }

    void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::setAddrWindow(x, y, w, h);
        }
#endif
        _window_x0 = x;
        _window_y0 = y;
        _window_w = w;
        _window_h = h;
    }

    void writePixels(uint16_t *colors, uint32_t len) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            Adafruit_ILI9341::writePixels(colors, len);
        }
#endif
    }

    void sendST7920Row(uint8_t row, const uint8_t *data16, size_t len, uint16_t fg, uint16_t bg) {
#if ENABLE_UART_VIRTUAL_TFT
        if (!_uart_enabled || !_uartStream || !data16 || len < 16) return;
        _uartStream->printf("STROW,%u,0x%04X,0x%04X,", row, fg, bg);
        for (size_t i = 0; i < 16; i++) {
            uint8_t b = data16[i];
            if (b < 0x10) _uartStream->print('0');
            _uartStream->print(b, HEX);
        }
        _uartStream->print('\n');
#endif
    }

    // High-performance Text Routing
    size_t write(const uint8_t *buffer, size_t size) override {
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && size > 0) {
            sendUartText((const char*)buffer, size);
        }
#endif

#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _suppress_pixel_uart = true;
            for (size_t i = 0; i < size; i++) {
                Adafruit_ILI9341::write(buffer[i]);
            }
            _suppress_pixel_uart = false;
        }
#else
        // Keep cursor coordinates tracked if physical display is compiled out
        for (size_t i = 0; i < size; i++) {
            if (buffer[i] == '\n') {
                cursor_y += (gfxFont ? 16 : 8) * textsize_y;
                cursor_x = 0;
            } else if (buffer[i] != '\r') {
                cursor_x += (gfxFont ? 12 : 6) * textsize_x;
            }
        }
#endif
        return size;
    }

    size_t write(uint8_t c) override {
        return write(&c, 1);
    }

private:
    Stream* _uartStream;
    bool _uart_enabled;
    bool _physical_enabled;
    bool _suppress_pixel_uart;

    int16_t _window_x0;
    int16_t _window_y0;
    int16_t _window_w;
    int16_t _window_h;

    void sendUartText(const char* str, size_t len) {
        if (!_uartStream || !str || len == 0) return;

        int16_t vx = cursor_x;
        int16_t vy = cursor_y;
        uint8_t vsize = textsize_x;

        if (gfxFont != NULL) {
            // Adjust baseline to top-left Y for Virtual TFT
            uint8_t y_offset = 11;
            if (gfxFont->yAdvance >= 35) {
                y_offset = 22;
                vsize = 3; // Large 18pt numerical values (yAdvance = 42)
            } else if (gfxFont->yAdvance >= 26) {
                y_offset = 16;
                vsize = 2; // Medium 12pt card titles & headers (yAdvance = 29)
            } else {
                y_offset = 11;
                vsize = 1; // Small 9pt labels, time, & pills (yAdvance = 22)
            }
            vy = (vy >= y_offset) ? (vy - y_offset) : 0;
        }

        _uartStream->print(F("TXT,"));
        _uartStream->print(vx);
        _uartStream->print(',');
        _uartStream->print(vy);
        _uartStream->print(',');
        _uartStream->print(vsize);
        _uartStream->printf(",0x%04X,", textcolor);

        if (textbgcolor != textcolor && textbgcolor != 0) {
            _uartStream->printf("0x%04X,", textbgcolor);
        } else {
            _uartStream->print(F("none,"));
        }

        for (size_t i = 0; i < len; i++) {
            char ch = str[i];
            if (ch == '\r' || ch == '\n') continue;
            _uartStream->print(ch);
        }
        _uartStream->print('\n');
    }
};

#endif // SERIAL_TFT_BRIDGE_H
