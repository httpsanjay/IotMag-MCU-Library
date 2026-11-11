/*
 * IoTMag.h  -  simple ESP32 client for the IoTMag platform (single file)
 *
 * SETUP
 *   1. Install the "ArduinoJson" library (version 7) from Library Manager.
 *   2. Put this file in the same folder as your .ino file.
 *   3. Add  #include "IoTMag.h"  at the top of your sketch.
 *
 * USAGE (see IoTMag_example.ino)
 *   iot.begin(serverUrl, deviceId, deviceSecret);   // once, in setup()
 *   iot.loop();                                     // every pass of loop()
 *
 *   if (iot.timeToSend()) {                         // true every few seconds
 *     iot.add("temperature", 28.5);                 // add any values you like
 *     iot.add("humidity", 60);
 *     iot.send();                                   // sent to IoTMag as JSON
 *   }
 *
 * You connect to Wi-Fi yourself (WiFi.begin). This library never does it:
 * if Wi-Fi is off, it just prints an error and waits.
 *
 * Automatic: device login, heartbeat, health report, network info, command
 * handling, and login again after Wi-Fi comes back.
 *
 * TELEMETRY
 *   Send any data as JSON with iot.add() + iot.send(), or send your own JSON
 *   text with iot.sendJson("{...}").
 *
 * HEALTH
 *   The library sends memory_usage, temperature and rssi by itself. Add or
 *   replace metrics with iot.addHealth("cpu_usage", 12.5). The latest values
 *   are sent every health interval, or right away with iot.sendHealth().
 *
 * FAILURES
 *   The heartbeat normally says status "online". If something failed (for
 *   example a send was refused) it says status "failure" with an "error"
 *   text instead, until the next request succeeds. You can also report your
 *   own problem: iot.reportFailure("Sensor not responding") and later
 *   iot.clearFailure().
 *
 * DASHBOARD
 *   From the IoTMag dashboard you can send these commands: ping, reboot,
 *   update_intervals (changes telemetry and heartbeat interval),
 *   set_heartbeat_interval, set_telemetry_interval.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#if !defined(ARDUINOJSON_VERSION_MAJOR) || ARDUINOJSON_VERSION_MAJOR < 7
#error "IoTMag needs ArduinoJson version 7 (Library Manager -> ArduinoJson)"
#endif

class IoTMag {
 public:
  // A function that runs your own commands. Return true if it worked.
  // Example:  bool myCommands(String command) { ... return true; }
  typedef bool (*CommandHandler)(String command);

  // ---------- Setup ----------

  void begin(const String& serverUrl, const String& deviceId,
             const String& deviceSecret) {
    _url = serverUrl;
    while (_url.endsWith("/")) _url.remove(_url.length() - 1);
    _id = deviceId;
    _secret = deviceSecret;
  }

  // Optional: run your own commands (anything that is not built in).
  void onCommand(CommandHandler handler) { _handler = handler; }

  // Optional: certificate for https servers. Without it https is not verified.
  void setCACert(const char* cert) { _cert = cert; }

  // Optional: pass false to stop the library printing messages.
  void printMessages(bool on) { _print = on; }

  // ---------- Call this every loop() ----------

  void loop() {
    if (_url.isEmpty() || _id.isEmpty() || _secret.isEmpty()) {
      fail("Call iot.begin(serverUrl, deviceId, deviceSecret) in setup()");
      return;
    }

    if (WiFi.status() != WL_CONNECTED) {
      _verified = false;
      fail("Wi-Fi is not connected. Connect Wi-Fi with WiFi.begin() first");
      return;
    }

    if (!_verified) {
      if (_tried && millis() - _lastTry < 3000UL) return;  // retry every 3 s
      _tried = true;
      _lastTry = millis();
      if (!login()) return;
      unsigned long now = millis();
      _lastHeartbeat = _lastCommand = _lastHealth = _lastNetwork = now;
      _lastTelemetry = now;
      sendNetworkInfo();
    }

    unsigned long now = millis();

    if (now - _lastHeartbeat >= _heartbeatMs) {
      _lastHeartbeat = now;
      sendHeartbeat();
    }
    if (now - _lastCommand >= COMMAND_MS) {
      _lastCommand = now;
      checkCommands();
    }
    if (now - _lastHealth >= HEALTH_MS) {
      _lastHealth = now;
      sendHealth();
    }
    if (now - _lastNetwork >= NETWORK_MS) {
      _lastNetwork = now;
      sendNetworkInfo();
    }
  }

  // ---------- Status ----------

  bool connected() { return WiFi.status() == WL_CONNECTED && _verified; }

  // Last error as text (empty when everything is fine).
  String error() { return _error; }

  // ---------- Telemetry ----------

  // True once every telemetry interval (default 10 s) while connected.
  bool timeToSend() {
    if (!connected()) return false;
    if (millis() - _lastTelemetry < _telemetryMs) return false;
    _lastTelemetry = millis();
    return true;
  }

  // Add a value:  iot.add("temperature", 28.5);
  // Use plain text names in quotes. Values can be numbers, true/false or text.
  template <typename T>
  void add(const char* name, T value) {
    _data[name] = value;
  }

  // Send everything added with add() as one JSON message.
  bool send() {
    if (_data.isNull()) return fail("Nothing to send. Use iot.add() first");
    bool ok = sendTelemetry(_data);
    _data.clear();
    return ok;
  }

  // Or send your own JSON text:  iot.sendJson("{\"temperature\":28.5}");
  bool sendJson(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return fail("sendJson(): that is not valid JSON");
    return sendTelemetry(doc);
  }

  // ---------- Health ----------

  // Add or replace a health metric, e.g. iot.addHealth("cpu_usage", 12.5);
  // It is sent with every health report until you change it.
  template <typename T>
  void addHealth(const char* name, T value) {
    _health[name] = value;
  }

  // Send a health report now (memory_usage, temperature, rssi + your metrics).
  bool sendHealth() {
    if (!ready()) return false;
    JsonDocument request, reply;
    addLogin(request);
    float total = (float)ESP.getHeapSize();
    float used = total - (float)ESP.getFreeHeap();
    request["memory_usage"] = total > 0 ? used * 100.0f / total : 0.0f;
    request["temperature"] = temperatureRead();
    request["rssi"] = WiFi.RSSI();

    // Your metrics (these replace the built-in ones if the names match)
    JsonObject mine = _health.as<JsonObject>();
    for (JsonPair item : mine) {
      request[item.key()] = item.value();
    }
    return post("/device/health/", request, reply);
  }

  // ---------- Failures ----------

  // Make the heartbeat report status "failure" with this text.
  void reportFailure(const String& message) { _failure = message; }

  // Go back to normal.
  void clearFailure() { _failure = ""; }

 private:
  static const uint32_t COMMAND_MS = 5000UL;
  static const uint32_t HEALTH_MS = 60000UL;
  static const uint32_t NETWORK_MS = 300000UL;

  String _url, _id, _secret, _token, _error;
  const char* _cert = nullptr;
  CommandHandler _handler = nullptr;
  bool _verified = false, _tried = false, _print = true;

  uint32_t _telemetryMs = 10000UL;
  uint32_t _heartbeatMs = 30000UL;
  unsigned long _lastTelemetry = 0, _lastHeartbeat = 0, _lastCommand = 0;
  unsigned long _lastHealth = 0, _lastNetwork = 0, _lastTry = 0;

  JsonDocument _data;
  JsonDocument _health;
  String _failure;
  WiFiClient _plain;
  WiFiClientSecure _secure;

  // Remember the error, print it once, return false.
  bool fail(const String& message) {
    if (message != _error) {
      _error = message;
      if (_print) Serial.println("[IoTMag] " + message);
    }
    return false;
  }

  // True if Wi-Fi is up and the device is logged in.
  bool ready() {
    if (WiFi.status() != WL_CONNECTED) return fail("Wi-Fi is not connected");
    if (!_verified) return fail("Not logged in to IoTMag yet");
    return true;
  }

  // Send a JSON request to the server and read the JSON reply.
  bool post(const char* path, JsonDocument& request, JsonDocument& reply) {
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);

    String url = _url + path;
    bool started;
    if (url.startsWith("https://")) {
      if (_cert) _secure.setCACert(_cert);
      else _secure.setInsecure();
      started = http.begin(_secure, url);
    } else {
      started = http.begin(_plain, url);
    }
    if (!started) return fail("Could not start the request. Check the server URL");

    http.addHeader("Content-Type", "application/json");
    String body;
    serializeJson(request, body);

    int code = http.POST(body);
    String text = http.getString();
    http.end();

    if (code <= 0) return fail("Cannot reach the server. Check the URL and that the server is running");
    if (code == 401 || code == 403) {
      _verified = false;  // log in again
      return fail("Server refused the request (HTTP " + String(code) + ")");
    }
    if (code < 200 || code >= 300) return fail("Server error (HTTP " + String(code) + ")");
    if (deserializeJson(reply, text)) return fail("Server sent a reply that is not JSON");

    _error = "";
    return true;
  }

  void addLogin(JsonDocument& request) {
    request["device_id"] = _id;
    request["access_token"] = _token;
  }

  bool login() {
    JsonDocument request, reply;
    request["device_id"] = _id;
    request["device_secret"] = _secret;

    if (!post("/device/verify/", request, reply)) return false;

    if (!(reply["success"] | false) || reply["access_token"].isNull()) {
      return fail("Wrong device ID or secret");
    }
    _token = reply["access_token"].as<String>();
    _verified = !_token.isEmpty();
    if (_verified && _print) Serial.println("[IoTMag] Logged in");
    return _verified;
  }

  bool sendTelemetry(JsonVariantConst data) {
    if (!ready()) return false;
    JsonDocument request, reply;
    addLogin(request);
    request["telemetry"] = data;
    return post("/device/telemetry/", request, reply);
  }

  bool sendHeartbeat() {
    if (!ready()) return false;
    JsonDocument request, reply;
    addLogin(request);
    // Report "failure" if you reported one, or if the last request failed.
    String problem = _failure.isEmpty() ? _error : _failure;
    if (problem.isEmpty()) {
      request["status"] = "online";
    } else {
      request["status"] = "failure";
      request["error"] = problem;
    }
    request["uptime"] = millis() / 1000UL;
    return post("/device/heartbeat/", request, reply);
  }

  bool sendNetworkInfo() {
    if (!ready()) return false;
    JsonDocument request, reply;
    addLogin(request);
    request["ip_address"] = WiFi.localIP().toString();
    request["mac_address"] = WiFi.macAddress();
    return post("/device/device_network_info/", request, reply);
  }

  // ---------- Commands ----------

  // Seconds -> milliseconds, only accepted between 5 s and 1 hour.
  static bool secondsOk(uint32_t seconds) { return seconds >= 5 && seconds <= 3600; }

  // Built-in commands. Sets `known` to false if the command is not built in.
  bool runBuiltIn(const String& command, JsonObjectConst payload, bool& known,
                  bool& reboot) {
    known = true;

    if (command == "ping") return true;

    if (command == "reboot") {
      reboot = true;  // happens after the server is told it worked
      return true;
    }

    if (command == "update_intervals") {  // values in seconds
      bool found = false, good = true;
      if (!payload["heartbeat_interval"].isNull()) {
        found = true;
        uint32_t s = payload["heartbeat_interval"].as<uint32_t>();
        if (secondsOk(s)) _heartbeatMs = s * 1000UL; else good = false;
      }
      if (!payload["telemetry_interval"].isNull()) {
        found = true;
        uint32_t s = payload["telemetry_interval"].as<uint32_t>();
        if (secondsOk(s)) _telemetryMs = s * 1000UL; else good = false;
      }
      return found && good;
    }

    if (command == "set_heartbeat_interval") {  // value in milliseconds
      uint32_t ms = payload["interval_ms"].as<uint32_t>();
      if (!secondsOk(ms / 1000UL)) return false;
      _heartbeatMs = ms;
      return true;
    }

    if (command == "set_telemetry_interval") {  // value in milliseconds
      uint32_t ms = payload["interval_ms"].as<uint32_t>();
      if (!secondsOk(ms / 1000UL)) return false;
      _telemetryMs = ms;
      return true;
    }

    known = false;
    return false;
  }

  void checkCommands() {
    if (!ready()) return;

    JsonDocument request, reply;
    addLogin(request);
    if (!post("/device/commands-get/", request, reply)) return;

    JsonArray commands = reply["commands"].as<JsonArray>();
    if (commands.isNull()) return;

    for (JsonObject cmd : commands) {
      String commandId = cmd["command_id"] | "";
      String command = cmd["command"] | "";
      if (commandId.isEmpty() || command.isEmpty()) continue;

      JsonObjectConst payload = cmd["multicommands"].as<JsonObjectConst>();
      if (payload.isNull()) payload = cmd["payload"].as<JsonObjectConst>();

      if (_print) Serial.println("[IoTMag] Command: " + command);

      bool known = false, reboot = false;
      bool worked = runBuiltIn(command, payload, known, reboot);
      if (!known) worked = _handler ? _handler(command) : false;

      // Tell the server whether it worked.
      JsonDocument ack, ackReply;
      addLogin(ack);
      ack["command_id"] = commandId;
      ack["command_status"] = worked ? "completed" : "failed";
      bool told = post("/device/commands-mark/", ack, ackReply) &&
                  (ackReply["success"] | false);

      if (reboot && told) {
        delay(300);
        ESP.restart();
      }
      if (!_verified) break;
    }
  }
};