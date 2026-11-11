# IoTMag (ESP32)

Arduino/PlatformIO client for the IoTMag device API.

- Requires ArduinoJson 7.x
- Everything (Wi-Fi, URL, device ID/secret, intervals, timeouts) comes from `IoTMagConfig`
- Telemetry: fill a JSON object in `onTelemetry`, or call `sendTelemetry()` / `sendTelemetryJson()` yourself
- Built-in commands: `ping`, `reboot`, `update_intervals` (seconds), `set_heartbeat_interval`, `set_telemetry_interval` (ms)
- Any other command goes to `onCommand`; return true/false for completed/failed
- Auto re-verifies after Wi-Fi loss or a 401/403 response


- GitHub repo : https://
PlatformIO: `lib_deps = bblanchon/ArduinoJson @ ^7`
