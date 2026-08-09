Sun Lazer Dual Heater Controller — Quick Reference (Print Ready)

This one-page quick reference is formatted for printing (A4 / US Letter). It embeds the SVG mockups created alongside for visual orientation. Convert this Markdown to PDF using pandoc or VS Code "Print to PDF".

---

Safety (Top)
- Disconnect mains before wiring. Use fuses, earthing and isolation.
- During commissioning test SSR outputs with a low-voltage dummy load or lamp.
- Keep clear of moving parts during motor tests.

Power-up checklist
- SD card present, DS3231 battery installed, ESP32 powered, TFT visible.
- Serial monitor at 115200 should show: "SD: OK" and "RTC: OK".

Start a process
1. Select Program. Verify Timer, Setpoints, PIDs.
2. Press OK to Start. Monitor temps and torque on display.
3. On completion: check SD /process_history.csv for log entry.

Stop / Emergency
- OK stops process. For E-STOP cut mains or use hardware interlock to disable SSRs.

Controls
- UP/DOWN: increment / decrement
- LEFT/RIGHT: move between fields / menus
- OK: select / save / start

Common tasks
- Timer Editor: Menu → Timer Editor (HH:MM:SS). Default step: 1s.
- PID Tuning: Menu → PID Tuning. Kp step 0.1, Ki step 0.01, Kd step 0.1.
- RTC Set: Menu → RTC Set (fields validated). Success: popup + beep/blink.

Logs
- SD CSV: /process_history.csv — timestamp, program, h1_set, h1_act, h2_set, h2_act, process_time_sec, max_torque_nm, result, alarm_code
- Recent in-RAM logs: small ring buffer (default 10) — LOG_RECENT_COUNT in include/config.h

Service tests
- Motor jog (ensure clear), SSR test (safe load), HX711 tare and MAX31865 check.

PID quick tips
1. Start Ki=0, Kd=0
2. Increase Kp until response approaches setpoint
3. Add Ki to remove steady-state offset
4. Add small Kd to reduce overshoot

If things fail
- SD missing: check SD CS and card format
- RTC wrong: check I2C wiring and battery; set via TFT or /rtc/set
- Sensors bad: verify wiring for PT100/MAX31865 and HX711

Embedded mockups (open these SVGs for a visual):

- Run screen: ./run_screen.svg
- Timer editor: ./timer_editor.svg
- PID tuning: ./pid_tuning.svg
- RTC set: ./rtc_set.svg
- Confirmation popup: ./popup_confirm.svg

Converting to PDF
- With pandoc: pandoc QUICK_REFERENCE_PRINT.md -o QUICK_REFERENCE_PRINT.pdf
- With Inkscape (SVGs to PNG first): inkscape run_screen.svg --export-type=png --export-filename=run_screen.png
- With VS Code: open this file, Print to PDF or use Markdown PDF extension.

Print notes
- Use A4 or Letter portrait. Scale to fit page. The SVGs are 480x320 and will render clearly when converted.

End of print-ready quick reference
