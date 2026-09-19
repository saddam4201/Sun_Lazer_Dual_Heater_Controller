# Version Increment Rule

Whenever modifying and pushing code:
1. Always increment the version:
   - In `include/config.h`: `#define FIRMWARE_VERSION "vX.Y"`
   - In `Virtual TFT/tft_display_app.py`: `APP_VERSION = "vX.Y"`
2. Verify the PlatformIO build (`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run`).
3. Commit and push the code with the incremented version.
