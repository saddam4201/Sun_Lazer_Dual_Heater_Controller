#ifndef SERIAL_TFT_BRIDGE_H
#define SERIAL_TFT_BRIDGE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

/**
 * @class Dual_TFT_eSPI
 * @brief Dual-output display driver that writes to:
 *        1. Physical ILI9341 TFT display via Hardware SPI (if ENABLE_PHYSICAL_TFT is active)
 *        2. Virtual TFT Display over UART Serial (if ENABLE_UART_VIRTUAL_TFT is active)
 *
 * Drops in directly in place of TFT_eSPI with zero changes to existing UI logic.
 */
class Dual_TFT_eSPI : public TFT_eSPI {
public:
    Dual_TFT_eSPI(int16_t w = TFT_WIDTH, int16_t h = TFT_HEIGHT)
        : TFT_eSPI(w, h),
          _uartStream(&Serial),
          _uart_enabled(true),
          _physical_enabled(true),
          _curTextColor(TFT_WHITE),
          _curTextBgColor(TFT_BLACK),
          _curTextSize(1),
          _curFreeFont(NULL),
          _rotation(1),
          _inHighLevelText(false) {}

    void setUartStream(Stream* s)       { _uartStream = s; }
    void setUartEnabled(bool en)        { _uart_enabled = en; }
    void setPhysicalEnabled(bool en)    { _physical_enabled = en; }
    bool isUartEnabled() const          { return _uart_enabled; }
    bool isPhysicalEnabled() const      { return _physical_enabled; }

    void init(uint8_t tc = TAB_COLOUR) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::init(tc);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->println(F("ROTA,1"));
            _uartStream->println(F("CLS,0x0000"));
            _uartStream->printf("[ESP32 Virtual UART TFT 2.8\" Ready (%s)]\n", FIRMWARE_VERSION);
        }
#endif
    }

    void begin(uint8_t tc = TAB_COLOUR) {
        init(tc);
    }

    void setRotation(uint8_t r) {
        _rotation = r & 3;
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setRotation(r);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("ROTA,%d\n", _rotation);
        }
#endif
    }

    void fillScreen(uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::fillScreen(color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            _uartStream->printf("CLS,0x%04X\n", (uint16_t)color);
        }
#endif
    }

    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::fillRect(x, y, w, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("RECT,%d,%d,%d,%d,0x%04X,1\n", (int)x, (int)y, (int)w, (int)h, (uint16_t)color);
        }
#endif
    }

    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawRect(x, y, w, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("RECT,%d,%d,%d,%d,0x%04X,0\n", (int)x, (int)y, (int)w, (int)h, (uint16_t)color);
        }
#endif
    }

    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawRoundRect(x, y, w, h, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("RRECT,%d,%d,%d,%d,%d,0x%04X,0\n", (int)x, (int)y, (int)w, (int)h, (int)r, (uint16_t)color);
        }
#endif
    }

    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::fillRoundRect(x, y, w, h, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("RRECT,%d,%d,%d,%d,%d,0x%04X,1\n", (int)x, (int)y, (int)w, (int)h, (int)r, (uint16_t)color);
        }
#endif
    }

    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawLine(x0, y0, x1, y1, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", (int)x0, (int)y0, (int)x1, (int)y1, (uint16_t)color);
        }
#endif
    }

    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawFastHLine(x, y, w, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", (int)x, (int)y, (int)(x + w - 1), (int)y, (uint16_t)color);
        }
#endif
    }

    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawFastVLine(x, y, h, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("LINE,%d,%d,%d,%d,0x%04X\n", (int)x, (int)y, (int)x, (int)(y + h - 1), (uint16_t)color);
        }
#endif
    }

    void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawCircle(x, y, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("CIRC,%d,%d,%d,0x%04X,0\n", (int)x, (int)y, (int)r, (uint16_t)color);
        }
#endif
    }

    void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::fillCircle(x, y, r, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("CIRC,%d,%d,%d,0x%04X,1\n", (int)x, (int)y, (int)r, (uint16_t)color);
        }
#endif
    }

    void drawPixel(int32_t x, int32_t y, uint32_t color) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::drawPixel(x, y, color);
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && !_inHighLevelText) {
            _uartStream->printf("PIX,%d,%d,0x%04X\n", (int)x, (int)y, (uint16_t)color);
        }
#endif
    }

    void setTextColor(uint16_t color) {
        _curTextColor = color;
        _curTextBgColor = color; // transparent / same
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setTextColor(color);
        }
#endif
    }

    void setTextColor(uint16_t fgcolor, uint16_t bgcolor, bool bgfill = false) {
        _curTextColor = fgcolor;
        _curTextBgColor = bgcolor;
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setTextColor(fgcolor, bgcolor, bgfill);
        }
#endif
    }

    void setTextSize(uint8_t size) {
        _curTextSize = size;
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setTextSize(size);
        }
#endif
    }

    void setFreeFont(const GFXfont *f = NULL) {
        _curFreeFont = (GFXfont *)f;
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setFreeFont(f);
        }
#endif
    }

    void setTextFont(uint8_t font) {
        _curFreeFont = NULL;
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setTextFont(font);
        }
#endif
    }

    uint8_t getEffectiveFontSize() const {
        if (_curFreeFont) {
            if (_curFreeFont->yAdvance >= 40) return 4; // FreeSansBold18pt7b (yAdvance = 42)
            if (_curFreeFont->yAdvance >= 28) return 3; // FreeSansBold12pt7b (yAdvance = 29)
            if (_curFreeFont->yAdvance >= 20) return 2; // FreeSansBold9pt7b  (yAdvance = 22)
            return 2;
        }
        return max((uint8_t)1, _curTextSize);
    }

    void setTextPadding(uint16_t pad) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            TFT_eSPI::setTextPadding(pad);
        }
#endif
    }

    int16_t drawString(const char *string, int32_t poX, int32_t poY, uint8_t font) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawString(string, poX, poY, font);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            sendUartText(string, poX, poY, font);
        }
#endif
        return 0;
    }

    int16_t drawString(const String &string, int32_t poX, int32_t poY, uint8_t font) {
        return drawString(string.c_str(), poX, poY, font);
    }

    int16_t drawString(const char *string, int32_t poX, int32_t poY) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawString(string, poX, poY);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            sendUartText(string, poX, poY, getEffectiveFontSize());
        }
#endif
        return 0;
    }

    int16_t drawString(const String &string, int32_t poX, int32_t poY) {
        return drawString(string.c_str(), poX, poY);
    }

    int16_t drawFloat(float floatNumber, uint8_t decimal, int32_t poX, int32_t poY, uint8_t font) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawFloat(floatNumber, decimal, poX, poY, font);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            char buf[32];
            dtostrf(floatNumber, 0, decimal, buf);
            sendUartText(buf, poX, poY, font);
        }
#endif
        return 0;
    }

    int16_t drawFloat(float floatNumber, uint8_t decimal, int32_t poX, int32_t poY) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawFloat(floatNumber, decimal, poX, poY);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            char buf[32];
            dtostrf(floatNumber, 0, decimal, buf);
            sendUartText(buf, poX, poY, getEffectiveFontSize());
        }
#endif
        return 0;
    }

    int16_t drawNumber(long long_num, int32_t poX, int32_t poY, uint8_t font) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawNumber(long_num, poX, poY, font);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", long_num);
            sendUartText(buf, poX, poY, font);
        }
#endif
        return 0;
    }

    int16_t drawNumber(long long_num, int32_t poX, int32_t poY) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawNumber(long_num, poX, poY);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", long_num);
            sendUartText(buf, poX, poY, getEffectiveFontSize());
        }
#endif
        return 0;
    }

    int16_t drawCentreString(const char *string, int32_t dX, int32_t poY, uint8_t font) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawCentreString(string, dX, poY, font);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            int16_t w = TFT_eSPI::textWidth(string, font);
            int32_t poX = dX - (w / 2);
            if (poX < 0) poX = 0;
            sendUartText(string, poX, poY, font);
        }
#endif
        return 0;
    }

    int16_t drawCentreString(const char *string, int32_t dX, int32_t poY) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawCentreString(string, dX, poY, 1);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            int16_t w = TFT_eSPI::textWidth(string);
            int32_t poX = dX - (w / 2);
            if (poX < 0) poX = 0;
            sendUartText(string, poX, poY, getEffectiveFontSize());
        }
#endif
        return 0;
    }

    int16_t drawRightString(const char *string, int32_t dX, int32_t poY, uint8_t font) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawRightString(string, dX, poY, font);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            int16_t w = TFT_eSPI::textWidth(string, font);
            int32_t poX = dX - w;
            if (poX < 0) poX = 0;
            sendUartText(string, poX, poY, font);
        }
#endif
        return 0;
    }

    int16_t drawRightString(const char *string, int32_t dX, int32_t poY) {
#if ENABLE_PHYSICAL_TFT
        if (_physical_enabled) {
            _inHighLevelText = true;
            TFT_eSPI::drawRightString(string, dX, poY, 1);
            _inHighLevelText = false;
        }
#endif
#if ENABLE_UART_VIRTUAL_TFT
        if (_uart_enabled && _uartStream && string) {
            int16_t w = TFT_eSPI::textWidth(string);
            int32_t poX = dX - w;
            if (poX < 0) poX = 0;
            sendUartText(string, poX, poY, getEffectiveFontSize());
        }
#endif
        return 0;
    }

private:
    Stream*  _uartStream;
    bool     _uart_enabled;
    bool     _physical_enabled;
    uint16_t _curTextColor;
    uint16_t _curTextBgColor;
    uint8_t  _curTextSize;
    GFXfont* _curFreeFont;
    uint8_t  _rotation;
    bool     _inHighLevelText;

    void sendUartText(const char* str, int32_t x, int32_t y, uint8_t font) {
        if (!_uartStream || !str || !str[0]) return;

        uint8_t sz = 1;
        if (font >= 4) sz = 4;
        else if (font == 3) sz = 3;
        else if (font == 2) sz = 2;
        else sz = max((uint8_t)1, _curTextSize);

        _uartStream->print(F("TXT,"));
        _uartStream->print((int)x);
        _uartStream->print(',');
        _uartStream->print((int)y);
        _uartStream->print(',');
        _uartStream->print((int)sz);
        _uartStream->printf(",0x%04X,", _curTextColor);

        if (_curTextBgColor != _curTextColor) {
            _uartStream->printf("0x%04X,", _curTextBgColor);
        } else {
            _uartStream->print(F("none,"));
        }

        while (*str) {
            char c = *str++;
            if (c == '\r' || c == '\n') continue;
            _uartStream->print(c);
        }
        _uartStream->print('\n');
    }
};

#endif // SERIAL_TFT_BRIDGE_H
