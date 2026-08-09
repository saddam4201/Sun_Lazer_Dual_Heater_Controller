Sun Lazer Dual Heater Controller — Quick Reference (One-page)

Safety (Top Priority)
- Always disconnect mains before wiring. Use proper fuses, earthing, and isolation.
- During commissioning, test SSR outputs with a low-voltage dummy load or indicator lamp.
- Keep hands clear of moving parts during motor tests.

Power-up checks
1. Insert SD card (FAT32) and DS3231 battery (CR2032).
2. Apply power. Open serial at 115200 to confirm boot diagnostics: SD: OK, RTC: OK.
3. Verify TFT shows Run screen and sensor values.

Start a process
1. Select program/recipe from Program menu (LEFT/RIGHT to navigate).
2. Check Timer (HH:MM:SS), Setpoints, and PID values.
3. Press OK to Start. Monitor H1/H2 actual temps and torque on screen.
4. On finish, check SD card /process_history.csv for CSV log entry.

Stop / Emergency
- Press OK (Stop) or use E-STOP hardware to immediately disable SSRs and motor power.
- After stop, inspect alarm message on TFT and serial log.

Basic TFT controls
- UP / DOWN: increment/decrement selected value.
- LEFT / RIGHT: move between fields or menus.
- OK: select / save / start / confirm.

Common tasks
- Set RTC via TFT: Menu -> RTC Set. Edit fields; press OK to save. Confirm popup + beep on success.
- Set RTC via web: GET /rtc to read, GET /rtc/set?iso=YYYY-MM-DDTHH:MM:SS to write (validated).
- Edit Timer: Menu -> Timer Editor. Fields: HH, MM, SS. Default step: 1s.
- PID tuning: Menu -> PID Tuning. Adjust Kp/Ki/Kd per heater; save before running.

Logs
- SD CSV: /process_history.csv — fields: timestamp, program, h1_set, h1_act, h2_set, h2_act, process_time_sec, max_torque_nm, result, alarm_code
- Recent in-memory logs: small ring buffer (default 10). Controlled by LOG_RECENT_COUNT in include/config.h.

Service tests (use with care)
- Motor jog: tests motor driver briefly — ensure mechanism clear.
- SSR test: toggles SSR outputs for wiring check; use safe load.
- Calibration: HX711 tare and MAX31865 sensor check in service menu.

PID tuning tips (quick)
1. Start Ki = 0, Kd = 0.
2. Increase Kp slowly until system approaches setpoint with minor oscillation.
3. Add Ki to remove steady-state offset.
4. Add small Kd to reduce overshoot.
5. Make small changes and run full process to evaluate.

If things fail
- SD missing: check SD CS wiring and card seating.
- RTC wrong: check I2C wiring and battery; set via TFT or /rtc/set.
- Sensors off: check PT100/MAX31865 wiring and HX711 connections.

Support
- See USER_GUIDE.md for full instructions and troubleshooting.
- For web API or additional endpoints, refer to API_DOCS.yaml in repository.

End of Quick Reference

(Place this file near operator station; print single page.)