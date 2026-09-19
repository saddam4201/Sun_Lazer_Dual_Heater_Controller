# Version Increment & Repository Rules

Whenever modifying and pushing code:
1. Always increment the version:
   - In `include/config.h`: `#define FIRMWARE_VERSION "vX.Y"`
   - In `Virtual TFT/tft_display_app.py`: `APP_VERSION = "vX.Y"`
2. Never track or push files under `.pio` or `.vscode`.
3. Verify the PlatformIO build (`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run`).
4. Commit and push the code with the incremented version.
