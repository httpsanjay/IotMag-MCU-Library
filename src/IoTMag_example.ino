/*
 * IoTMag - detailed example
 *
 * What this sketch does:
 *   1. Connects to Wi-Fi (you do this, the library never does)
 *   2. Logs in to IoTMag with your device ID and secret
 *   3. Sends sensor data as JSON every telemetry interval (default 10 s)
 *   4. Reacts to dashboard commands: led_on, led_off, play_alert
 *      (ping, reboot and interval commands are built in)
 *   5. Reconnects Wi-Fi by itself if the router goes away
 *   6. Sends health metrics, and reports "failure" in the heartbeat on problems
 *
 * Good to know:
 *   - Send any data as JSON with iot.add() + iot.send(),
 *     or send your own JSON text with iot.sendJson().
 *   - You can change the telemetry and heartbeat interval from the IoTMag
 *     dashboard (commands: update_intervals, set_telemetry_interval,
 *     set_heartbeat_interval).
 *   - Health metrics (cpu_usage, memory_usage, ...) can be added with
 *     iot.addHealth() and sent with iot.sendHealth().
 *
 * Files needed in the same folder:  this .ino  +  IoTMag.h
 * Library needed:  ArduinoJson 7  (Sketch -> Include Library -> Manage Libraries)
 */

#include <WiFi.h>
#include "IoTMag.h"

// =====================================================
// 1. SETTINGS - change these
// =====================================================

// Your Wi-Fi
const char* WIFI_NAME     = "your-wifi-name";   // change this
const char* WIFI_PASSWORD = "your-wifi-password"; // change this

// From the IoTMag dashboard (use your computer's IP, not "localhost")
const char* SERVER_URL    = "http://your-server-ip:3000"; // example: http://192.168.1.1:3000
const char* DEVICE_ID     = "your-device-id";      // change this
const char* DEVICE_SECRET = "your-device-secret"; // change this

// Pins
const int LED_PIN          = 2;    // built-in LED on most ESP32 boards
const int WATER_SENSOR_PIN = 34;   // analog sensor (input-only pin)

// =====================================================
// 2. VARIABLES
// =====================================================

IoTMag iot;                          // the library object

unsigned long lastWifiRetry  = 0;    // for reconnecting Wi-Fi
unsigned long lastStatusShow = 0;    // for printing status
bool ledIsOn = false;

// =====================================================
// 3. WI-FI (your code, not the library's)
// =====================================================

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

  // Wait up to 20 seconds
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    // Not fatal: loop() keeps trying
    Serial.println("Wi-Fi failed. Will keep retrying...");
  }
}

// If Wi-Fi drops, ask the ESP32 to reconnect (every 10 seconds)
void keepWiFiAlive() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiRetry >= 10000UL) {
    lastWifiRetry = millis();
    Serial.println("Wi-Fi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  }
}

// =====================================================
// 4. SENSORS
// =====================================================

// Water sensor as a percentage (0-100)
float readWaterPercent() {
  int raw = analogRead(WATER_SENSOR_PIN);   // 0 - 4095
  return raw * 100.0 / 4095.0;
}

// =====================================================
// 5. YOUR OWN COMMANDS (optional)
// =====================================================
// The library calls this when the dashboard sends a command that
// is NOT built in. The text must match what the dashboard sends.
// Return true if it worked, false if it failed.

bool myCommands(String command) {
  if (command == "led_on") {
    digitalWrite(LED_PIN, HIGH);
    ledIsOn = true;
    return true;
  }

  if (command == "led_off") {
    digitalWrite(LED_PIN, LOW);
    ledIsOn = false;
    return true;
  }

  if (command == "play_alert") {
    // Blink the LED 3 times as a simple "alert"
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    digitalWrite(LED_PIN, ledIsOn ? HIGH : LOW);   // restore LED
    return true;
  }

  Serial.println("Unknown command: " + command);
  return false;   // dashboard will show this command as "failed"
}

// =====================================================
// 6. SETUP - runs once
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);
  digitalWrite(LED_PIN, LOW);

  connectWiFi();

  // Tell the library where to send data and who this device is
  iot.begin(SERVER_URL, DEVICE_ID, DEVICE_SECRET);

  // Optional: connect your command function
  iot.onCommand(myCommands);

  // Optional: extra health metrics. The library already sends memory_usage,
  // temperature and rssi. These are added to every health report.
  iot.addHealth("cpu_mhz", getCpuFrequencyMhz());
  // iot.addHealth("cpu_usage", 25.0);   // only if you measure it yourself

  // Optional: stop the library printing messages
  // iot.printMessages(false);
}

// =====================================================
// 7. LOOP - runs forever
// =====================================================

void loop() {
  keepWiFiAlive();   // your Wi-Fi helper
  iot.loop();        // MUST be called every loop (login, heartbeat, commands...)

  // ---- Send telemetry ----
  // timeToSend() is true once every telemetry interval while connected.
  if (iot.timeToSend()) {
    float water = readWaterPercent();

    // Report a problem: the heartbeat then says status "failure" + your text.
    // Call iot.clearFailure() when the problem is gone.
    // if (sensorIsBroken) iot.reportFailure("Water sensor not responding");
    // else iot.clearFailure();

    // Numbers, true/false and text all work. Names must be in "quotes".
    iot.add("water_level", water);
    iot.add("water_raw", analogRead(WATER_SENSOR_PIN));
    iot.add("led_on", ledIsOn);
    iot.add("status", "ok");

    if (iot.send()) {
      Serial.printf("Sent: water=%.1f%%, led=%s\n", water, ledIsOn ? "ON" : "OFF");
    } else {
      Serial.println("Send failed: " + iot.error());
    }

    // Alternative: write the JSON yourself instead of add()/send()
    // iot.sendJson("{\"water_level\": 42.5, \"status\": \"ok\"}");
  }

  // ---- Send a health report right now (optional) ----
  // The library already does this every 60 s by itself. Only call it
  // yourself if you want an extra report, for example after a change:
  // iot.addHealth("free_heap_kb", ESP.getFreeHeap() / 1024);
  // iot.sendHealth();

  // ---- Print status every 15 seconds ----
  if (millis() - lastStatusShow >= 15000UL) {
    lastStatusShow = millis();
    Serial.print("Status -> Wi-Fi: ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(", IoTMag: ");
    Serial.println(iot.connected() ? "connected" : "not connected");
  }
}

/*
 * COMMON PROBLEMS
 *
 * "Wi-Fi is not connected"            -> wrong Wi-Fi name/password, or out of range.
 * "Cannot reach the server"           -> wrong SERVER_URL, server not running, or the
 *                                        ESP32 and server are on different networks.
 *                                        Use your computer's IP, never "localhost".
 * "Wrong device ID or secret"         -> copy them again from the IoTMag dashboard.
 * "Nothing to send. Use iot.add()"    -> call iot.add() before iot.send().
 * Telemetry never sends               -> make sure iot.loop() is in loop() and you
 *                                        are not using long delay() calls.
 */
