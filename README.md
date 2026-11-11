# IoTMag ESP32 Library

A single-file, beginner-friendly Arduino library that connects an **ESP32** to the **IoTMag** device management platform.

Send sensor data as JSON, report device health, receive dashboard commands, and let the library handle login, heartbeat and reconnection for you.

> **Dashboard app (backend + web UI):**
> [IoTMag-Iot-Device-Managemnet-and-Control](https://github.com/httpsanjay/IoTMag-Iot-Device-Managemnet-and-Control)
>
> This library talks to that dashboard's device API. Set up the dashboard first, create a device there, and copy its **Device ID** and **Device Secret**.

---

## Table of contents

1. [Features](#features)
2. [Requirements](#requirements)
3. [Installation](#installation)
4. [Quick start](#quick-start)
5. [How it works](#how-it-works)
6. [Function reference](#function-reference)
7. [Built-in dashboard commands](#built-in-dashboard-commands)
8. [Custom commands](#custom-commands)
9. [Telemetry examples](#telemetry-examples)
10. [Health metrics](#health-metrics)
11. [Failure reporting](#failure-reporting)
12. [HTTPS servers](#https-servers)
13. [Data sent to the server](#data-sent-to-the-server)
14. [Error messages and troubleshooting](#error-messages-and-troubleshooting)
15. [Limits and good to know](#limits-and-good-to-know)
16. [Security tips](#security-tips)

---

## Features

- **Single file**: just `IoTMag.h`, no `.cpp`, no folder structure.
- **Simple API**: `begin()`, `loop()`, `add()`, `send()`. That is all a beginner needs.
- **Telemetry as JSON**: send any values, or your own JSON string.
- **Automatic**: device login, heartbeat, health report, network info and command handling.
- **You control Wi-Fi**: the library never connects to Wi-Fi. It only checks the status, prints an error if Wi-Fi is down, and waits (similar in spirit to the MQTT `PubSubClient` library).
- **Auto re-login** after Wi-Fi comes back or when the server rejects the token.
- **Built-in commands**: `ping`, `reboot` and interval-change commands.
- **Custom commands**: plug in your own function for anything else.
- **Failure reporting**: the heartbeat switches to `failure` plus an error text when something goes wrong.
- **Plain-English errors** printed once (no Serial spam).

---

## Requirements

| Item | Requirement |
|---|---|
| Board | ESP32 (Arduino core for ESP32) |
| Library | [ArduinoJson](https://arduinojson.org/) **version 7** or newer |
| Server | A running IoTMag dashboard ([repo](https://github.com/httpsanjay/IoTMag-Iot-Device-Managemnet-and-Control)) reachable from the ESP32's network |

Install ArduinoJson from **Arduino IDE → Sketch → Include Library → Manage Libraries → "ArduinoJson"**. PlatformIO users can add:

```ini
lib_deps = bblanchon/ArduinoJson @ ^7
```

---

## Installation

**Option A: next to your sketch (easiest)**

1. Put `IoTMag.h` in the same folder as your `.ino` file.
2. Add this at the top of your sketch:

```cpp
#include "IoTMag.h"
```

**Option B: as a library**

1. Copy `IoTMag.h` to `Documents/Arduino/libraries/IoTMag/IoTMag.h`.
2. Restart the Arduino IDE.
3. Use `#include <IoTMag.h>`.

---

## Quick start

```cpp
#include <WiFi.h>
#include "IoTMag.h"

const char* WIFI_NAME     = "your-wifi-name";
const char* WIFI_PASSWORD = "your-wifi-password";

// From the IoTMag dashboard (use your computer's IP, never "localhost")
const char* SERVER_URL    = "http://192.168.1.10:3000";
const char* DEVICE_ID     = "your-device-id";
const char* DEVICE_SECRET = "your-device-secret";

IoTMag iot;

void setup() {
  Serial.begin(115200);

  // You connect to Wi-Fi. The library does not do this.
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected");

  iot.begin(SERVER_URL, DEVICE_ID, DEVICE_SECRET);
}

void loop() {
  iot.loop();                       // keeps everything running

  if (iot.timeToSend()) {           // true every ~10 seconds
    iot.add("temperature", 28.5);   // replace with real sensor readings
    iot.add("humidity", 60);
    iot.send();                     // sent to IoTMag as JSON
  }
}
```

A longer, fully commented sketch is in `IoTMag_detailed_example.ino`.

---

## How it works

Call `iot.begin(...)` once, then call `iot.loop()` on every pass of `loop()`. From then on the library does the following by itself:

| Task | How often | Endpoint |
|---|---|---|
| Log in (device ID + secret, receives a token) | When Wi-Fi is up and not logged in; retries every 3 s | `/device/verify/` |
| Heartbeat (`online`, or `failure` + error text) | 30 s | `/device/heartbeat/` |
| Check for dashboard commands, then report completed or failed | 5 s | `/device/commands-get/`, `/device/commands-mark/` |
| Health report (memory, temperature, RSSI + your metrics) | 60 s | `/device/health/` |
| Network info (IP and MAC address) | 5 min, and right after login | `/device/device_network_info/` |
| Telemetry | Only when **you** call `send()` | `/device/telemetry/` |

**Wi-Fi handling:** if Wi-Fi drops, the library prints one error, does nothing else, and waits. It never calls `WiFi.begin()`. When Wi-Fi is back, it logs in again automatically. Reconnecting Wi-Fi is your sketch's job (see the detailed example for a ready-made `keepWiFiAlive()` helper).

---

## Function reference

### Setup

| Function | Description |
|---|---|
| `iot.begin(serverUrl, deviceId, deviceSecret)` | Stores the server address and device credentials. Call once in `setup()`. It does no networking. |
| `iot.onCommand(myFunction)` | *Optional.* Registers your function for commands that are not built in. |
| `iot.setCACert(cert)` | *Optional.* Certificate for https servers. Without it, https works but the certificate is not verified. |
| `iot.printMessages(false)` | *Optional.* Stops the library printing to Serial. |

### Running

| Function | Description |
|---|---|
| `iot.loop()` | Runs login, heartbeat, commands, health and network info. Call it on every pass of `loop()` and avoid long `delay()` calls. |

### Status

| Function | Description |
|---|---|
| `iot.connected()` | `true` when Wi-Fi is up **and** the device is logged in. |
| `iot.error()` | Last error as text. Empty when everything is fine; clears after the next successful request. |

### Telemetry

| Function | Description |
|---|---|
| `iot.timeToSend()` | `true` once per telemetry interval (default 10 s) while connected. The dashboard can change the interval. |
| `iot.add("name", value)` | Adds a value to the next message. Value can be a number, `true`/`false` or text. The name must be a quoted literal. |
| `iot.send()` | Sends everything added with `add()` as one JSON message, then clears it. Returns `true` or `false`. |
| `iot.sendJson("{...}")` | Sends your own JSON text instead. Returns `false` if the text is not valid JSON. |

### Health

| Function | Description |
|---|---|
| `iot.addHealth("name", value)` | Adds or replaces a health metric, e.g. `cpu_usage`. It is sent with every health report until you change it. |
| `iot.sendHealth()` | Sends a health report right now, in addition to the automatic one. |

### Failure reporting

| Function | Description |
|---|---|
| `iot.reportFailure("text")` | Makes the heartbeat send `status: "failure"` with your text as `error`. |
| `iot.clearFailure()` | Goes back to `status: "online"`. |

### Types

```cpp
typedef bool (*CommandHandler)(String command);
```

Your command function must look like `bool myCommands(String command)`. Return `true` if it worked and `false` if it failed.

---

## Built-in dashboard commands

These are handled by the library and cannot be overridden.

| Command | Data it needs | Effect |
|---|---|---|
| `ping` | none | Reports completed. |
| `reboot` | none | Reports completed, **then** restarts the ESP32. |
| `update_intervals` | `heartbeat_interval` and/or `telemetry_interval` (seconds) | Changes those intervals. Each must be 5 to 3600 s. |
| `set_heartbeat_interval` | `interval_ms` | Changes the heartbeat interval. Must be 5000 to 3600000 ms. |
| `set_telemetry_interval` | `interval_ms` | Changes the telemetry interval. Same range. |

Only the **telemetry** and **heartbeat** intervals can be changed from the dashboard. The health (60 s), command (5 s) and network-info (5 min) intervals are fixed.

Any other command goes to your `onCommand` function. If you have not set one, the command is reported as `failed`.

---

## Custom commands

Write a function that receives the command name and returns `true` or `false`. The names must match what your dashboard sends.

```cpp
const int LED_PIN = 2;

bool myCommands(String command) {
  if (command == "led_on") {
    digitalWrite(LED_PIN, HIGH);
    return true;
  }
  if (command == "led_off") {
    digitalWrite(LED_PIN, LOW);
    return true;
  }
  return false;   // unknown command, dashboard shows "failed"
}

void setup() {
  // ...
  pinMode(LED_PIN, OUTPUT);
  iot.begin(SERVER_URL, DEVICE_ID, DEVICE_SECRET);
  iot.onCommand(myCommands);
}
```

> Your function receives only the command name, not any extra data sent with the command.
> Keep it quick. Long blocking code delays heartbeat and other tasks.

---

## Telemetry examples

**Using `add()` and `send()`**

```cpp
if (iot.timeToSend()) {
  iot.add("temperature", 28.5);   // number
  iot.add("water_raw", 1500);     // integer
  iot.add("pump_on", true);       // true/false
  iot.add("status", "ok");        // text

  if (iot.send()) {
    Serial.println("Sent");
  } else {
    Serial.println("Send failed: " + iot.error());
  }
}
```

Resulting JSON sent in the `telemetry` field:

```json
{ "temperature": 28.5, "water_raw": 1500, "pump_on": true, "status": "ok" }
```

**Using your own JSON text**

```cpp
iot.sendJson("{\"water_level\": 42.5, \"status\": \"ok\"}");
```

---

## Health metrics

The library already sends `memory_usage` (%), `temperature` (chip, °C) and `rssi` (Wi-Fi signal). Add your own with `addHealth()`:

```cpp
iot.addHealth("cpu_mhz", getCpuFrequencyMhz());
iot.addHealth("cpu_usage", 25.0);     // only if you measure it yourself

iot.sendHealth();                      // optional: send a report right now
```

- Values stay stored and are sent with every health report until you change them.
- If a name matches a built-in one (e.g. `memory_usage`), yours replaces it.
- The ESP32 has no simple CPU-usage reading. If your dashboard needs `cpu_usage`, you have to measure it and supply it.

---

## Failure reporting

The heartbeat normally sends `status: "online"`. It sends `status: "failure"` with an `error` text when:

- the **last request failed** (for example "Server error (HTTP 500)"). It returns to `online` after the next successful request, or
- **you reported a problem** yourself:

```cpp
if (sensorIsBroken) {
  iot.reportFailure("Water sensor not responding");
} else {
  iot.clearFailure();
}
```

> Check that your dashboard backend accepts the value `"failure"`. If it expects a different word (for example `"Failure"` or `"error"`), change it in the `sendHeartbeat()` function inside `IoTMag.h`.

---

## HTTPS servers

Use an `https://` server URL and the library switches to a secure connection automatically.

```cpp
iot.begin("https://iot.example.com", DEVICE_ID, DEVICE_SECRET);
iot.setCACert(rootCaPem);   // recommended: verifies the server
```

Without `setCACert()`, https still works but the certificate is **not verified**. That is fine for testing but not recommended for real deployments.

---

## Data sent to the server

All requests are `POST` with `Content-Type: application/json`.

**Login:** `/device/verify/`

```json
{ "device_id": "...", "device_secret": "..." }
```

Reply must contain `"success": true` and an `access_token`. The token is then sent with every other request.

**Telemetry:** `/device/telemetry/`

```json
{ "device_id": "...", "access_token": "...", "telemetry": { "temperature": 28.5 } }
```

**Heartbeat:** `/device/heartbeat/`

```json
{ "device_id": "...", "access_token": "...", "status": "online", "uptime": 1234 }
```

On failure: `"status": "failure", "error": "Server error (HTTP 500)"`.

**Health:** `/device/health/`

```json
{ "device_id": "...", "access_token": "...", "memory_usage": 34.2, "temperature": 45.1, "rssi": -60, "cpu_mhz": 240 }
```

**Network info:** `/device/device_network_info/`

```json
{ "device_id": "...", "access_token": "...", "ip_address": "192.168.1.50", "mac_address": "AA:BB:CC:DD:EE:FF" }
```

**Get commands:** `/device/commands-get/`

The reply contains a `commands` list. Each command has `command_id`, `command`, and its data in `multicommands` (or the older `payload`).

**Mark command:** `/device/commands-mark/`

```json
{ "device_id": "...", "access_token": "...", "command_id": "...", "command_status": "completed" }
```

`command_status` is `completed` or `failed`.

---

## Error messages and troubleshooting

| Message | Meaning / fix |
|---|---|
| `Call iot.begin(serverUrl, deviceId, deviceSecret) in setup()` | `begin()` was not called. |
| `Wi-Fi is not connected. Connect Wi-Fi with WiFi.begin() first` | Wrong Wi-Fi name or password, out of range, or you forgot `WiFi.begin()`. |
| `Wrong device ID or secret` | Copy them again from the IoTMag dashboard. |
| `Cannot reach the server...` | Wrong `SERVER_URL`, server not running, or ESP32 and server on different networks. Use your computer's IP, **never** `localhost`. |
| `Could not start the request. Check the server URL` | The URL is malformed. It needs `http://` or `https://`. |
| `Server refused the request (HTTP 401/403)` | The token is no longer valid. The library logs in again by itself. |
| `Server error (HTTP ...)` | The server returned an error status. Check the dashboard logs. |
| `Server sent a reply that is not JSON` | Wrong URL (e.g. a web page instead of the API) or a server problem. |
| `Nothing to send. Use iot.add() first` | You called `send()` with nothing added. |
| `sendJson(): that is not valid JSON` | The text passed to `sendJson()` is not valid JSON. |

**Other common problems**

- *Telemetry never sends:* make sure `iot.loop()` is in `loop()`, you are not using long `delay()` calls, and `iot.connected()` is `true`.
- *Server unreachable from the ESP32:* check the computer's firewall allows the server port (e.g. 3000), and that the server listens on all interfaces, not only `localhost`.
- *Compile error about ArduinoJson:* install ArduinoJson **version 7**.

---

## Limits and good to know

- Intervals set from the dashboard must be between **5 seconds and 1 hour**.
- Intervals fixed in the library: command check 5 s, health 60 s, network info 5 min. Heartbeat starts at 30 s and telemetry at 10 s.
- Use quoted text for value **names** in `add()` and `addHealth()`, e.g. `"temperature"`.
- Custom command functions get the command name only.
- The library uses blocking HTTP requests (timeouts: 5 s to connect, 8 s for the reply), so a slow server can briefly pause your `loop()`.
- There is no built-in LED command. Add LED control through `onCommand()` if you need it.

---

## Security tips

- **Never commit real Wi-Fi passwords or device secrets** to GitHub. Keep them in a separate `secrets.h` file and add it to `.gitignore`.
- If a secret has been shared publicly, regenerate it in the IoTMag dashboard.
- Prefer `https://` with `setCACert()` for anything outside your local network.

---

## Files in this project

| File | Purpose |
|---|---|
| `IoTMag.h` | The whole library |
| `IoTMag_example.ino` | Minimal example |
| `IoTMag_detailed_example.ino` | Fully commented example with Wi-Fi reconnect, custom commands, health and failure handling |
| `README.md` | This file |

## Related

- Dashboard and backend: <https://github.com/httpsanjay/IoTMag-Iot-Device-Managemnet-and-Control>