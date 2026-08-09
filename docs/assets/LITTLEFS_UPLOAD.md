Uploading web assets to the ESP32 filesystem (LittleFS / SPIFFS)

This guide explains how to put the web UI files (index.html, styles.css, app.js, images, etc.) onto the ESP32 filesystem so the device can serve them directly (SPIFFS or LittleFS). Instructions are Windows/PowerShell-focused and assume PlatformIO + VS Code.

Recommended layout
- Project root
  - data\        <- PlatformIO expects the filesystem source here by default
    - index.html
    - styles.css
    - app.js
    - (other files & folders)

Note: this repository currently contains the assets under data\www (data\www\index.html, ...). Two options:
1) Copy the files into data\ (recommended)
   - PowerShell (run from project root):
     Copy-Item -Path .\data\www\* -Destination .\data -Recurse -Force
2) Or use data\www as your root and adjust your PlatformIO config to point to a different data dir (advanced). Copying is simpler and consistent with PlatformIO defaults.

PlatformIO configuration (platformio.ini)
- Ensure your environment uses LittleFS or SPIFFS. Example for LittleFS (ESP32):

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
board_build.filesystem = littlefs

- If you prefer SPIFFS, use board_build.filesystem = spiffs instead. LittleFS is recommended for new projects.

Upload filesystem image (CLI)
- Open PowerShell at the project root.
- Build and upload the filesystem image for the esp32dev environment:
  platformio run --target uploadfs -e esp32dev
  (shorthand: pio run -t uploadfs -e esp32dev)

- This packages the contents of the data\ directory into a filesystem image and flashes it to the ESP32's flash allocated for LittleFS/SPIFFS.

Upload filesystem image (VS Code / PlatformIO IDE)
- Open the PlatformIO sidebar -> Project Tasks -> <your env> (esp32dev) ->
  - Choose "Upload File System Image" (or "Upload SPIFFS/LittleFS image").
- This runs the equivalent uploadfs target for the selected environment.

Upload firmware (if you changed code)
- To upload the firmware binary (flash the code):
  platformio run --target upload -e esp32dev
  (or use Project Tasks -> Upload)

Troubleshooting & tips
- Make sure the data\ directory exists and contains index.html before running uploadfs.
- If uploadfs fails with a "No filesystem image" error, try running platformio run --target buildfs -e esp32dev first (generates the image) then uploadfs.
- If the board isn't found, set upload_port in platformio.ini or provide -p COM3 (Windows) to the upload command.
- If you switch between spiffs and littlefs, fully erase flash or re-create filesystem image; leftover filesystem metadata can cause unexpected behavior.

Serve files from firmware (Arduino WebServer example)
- If using the Arduino WebServer and LittleFS, add this snippet to setup() (requires LittleFS.begin() or SPIFFS.begin() and LittleFS/SPIFFS includes):

#include "FS.h"
#include <LittleFS.h>   // or <SPIFFS.h>
#include <WebServer.h>

WebServer server(80);

void setup() {
  Serial.begin(115200);
  if(!LittleFS.begin()){
    Serial.println("LittleFS mount failed");
  }
  // Serve static files from LittleFS root and default to index.html
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Example dynamic endpoints the web UI expects
  server.on("/status", HTTP_GET, [](){
    // compose JSON with runtime telemetry
    String json = "{\"ip\":\"192.168.4.1\", \"sd\":true, \"rtc\":\"2026-08-10T04:24:00\", \"h1_set\":180.0}";
    server.send(200, "application/json", json);
  });

  server.begin();
}

void loop(){
  server.handleClient();
}

Security & size considerations
- Keep the web app small: minify CSS/JS to reduce space.
- If the total size exceeds available LittleFS partition, reduce assets or enable compression.
- Consider serving only a compact SPA (single index.html + small JS) to save space.

Reverting to local testing
- To test the web UI in a browser on your PC, open data\index.html directly (file://) or host it with a simple local server (python -m http.server).

If you'd like, I can:
- Add a PlatformIO build target to platformio.ini to streamline uploadfs commands.
- Copy the current data\www files into data\ for you (PowerShell command included above).  

