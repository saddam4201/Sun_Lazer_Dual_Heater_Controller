TFT Screen Mockups — ASCII Layouts

These are simple ASCII mockups of key TFT screens to help with operator orientation and optional documentation images. Use them as references when creating PNG mockups or screenshots.

1) Run / Main Screen
+------------------------------------------------+
| SUN LAZER  Dual Heater Controller              |
| Program: <Program Name>      Time: 00:12:34    |
|                                                |
| H1 Set: 180.0 C   H1 Act: 178.6 C  [####### ]  |
| H2 Set: 175.0 C   H2 Act: 174.1 C  [ #####  ]  |
|                                                |
| Torque: 2.3 Nm   Max: 3.1 Nm   Result: RUNNING  |
| SD: OK   RTC: OK   Alarms: 0                     |
|                                                |
| <UP> <DOWN>    <LEFT> <RIGHT>    <OK>           |
+------------------------------------------------+

Notes: progress bars are illustrative; actual UI uses numeric and small bar graphics.

2) Timer Editor (HH:MM:SS)
+-----------------------------+
| Timer Editor                |
| Program: <Program Name>     |
|                             |
|   Hours  : 00   <--         |
|   Minutes: 12   <--  (cursor)|
|   Seconds: 34   -->         |
|                             |
|   [SAVE]     [CANCEL]       |
+-----------------------------+

Controls: UP/DOWN change value; LEFT/RIGHT move field; OK=Save.

3) PID Tuning (Per-Program)
+-------------------------------------------+
| PID Tuning — Program: <Program Name>      |
| H1 Kp: 12.3   H1 Ki: 0.25   H1 Kd: 0.8     |
| H2 Kp: 11.0   H2 Ki: 0.20   H2 Kd: 0.6     |
|                                           |
| Select param -> UP/DOWN to adjust          |
| [Save]   [Revert]                         |
+-------------------------------------------+

4) RTC Set Screen
+--------------------------------+
| RTC Set                        |
| Year: 2026   Month: 08   Day:10 |
| Hour: 04    Min: 24    Sec: 00  |
|                                |
| [Save]   [Cancel]               |
+--------------------------------+

On Save: validation occurs (days-in-month, leap year). Success shows a short confirmation popup and a beep/blink.

5) Confirmation Popup (transient)
+-----------------------------+
|  Saved — RTC updated        |
|  (beep)  [OK]               |
+-----------------------------+

Images and PNGs
- To add real screenshots or PNGs, capture the TFT display and place images in /docs/screens/ or /assets/.
- Suggested filenames: run_screen.png, timer_editor.png, pid_tuning.png, rtc_set.png, popup_confirm.png.
- If desired, request generation of simple PNG mockups (SVG-derived) and this file can be updated with embedded links.

End of mockups