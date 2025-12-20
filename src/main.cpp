#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include "driver/twai.h"

// ----------- User configuration -----------
static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_21;  // Check silk screen, adjust if needed
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_22;  // Check silk screen, adjust if needed
static constexpr int LED_PIN = 4;                      // Data pin for the addressable strip
static constexpr int LED_COUNT = 60;                   // Number of LEDs on the strip
static constexpr int LED_BRIGHTNESS = 180;             // 0-255
static constexpr uint32_t CAN_BITRATE = 1'000'000;     // 1 Mbps

static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";        // Replace with your Wi-Fi network
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD"; // Replace with your Wi-Fi password
static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint32_t WIFI_RECONNECT_MS = 10'000;
static constexpr char BT_DEVICE_NAME[] = "TCAN48-CFG";
static constexpr char PREF_NAMESPACE[] = "can_led";

// CAN message identifiers (standard 11-bit frames)
static constexpr uint32_t ID_THROTTLE = 0x100; // data[0]: 0-100 %
static constexpr uint32_t ID_BRAKES   = 0x101; // data[0] bit0: brake pedal, bit1: handbrake
static constexpr uint32_t ID_RPM      = 0x102; // uint16_t little-endian: RPM
static constexpr uint32_t ID_COOLANT  = 0x103; // uint16_t little-endian: temperature * 10 °C
static constexpr uint32_t ID_REV_LIM  = 0x104; // data[0] non-zero when limiter active

CRGB leds[LED_COUNT];

struct VehicleState {
    uint8_t throttlePercent = 0;
    bool brakePedal = false;
    bool handBrake = false;
    uint16_t rpm = 0;
    uint16_t rpmRedline = 6500;
    uint16_t coolant10x = 900; // 90.0°C
    bool revLimiter = false;
};

VehicleState state;

// ---- Telemetry buffers ----
struct ReceivedFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {0};
    uint32_t timestampMs = 0;
};

static constexpr size_t FRAME_LOG_SIZE = 20;
ReceivedFrame frameLog[FRAME_LOG_SIZE];
size_t frameLogHead = 0;

WebServer server(HTTP_PORT);
uint32_t lastWifiAttempt = 0;
BluetoothSerial btSerial;
Preferences prefs;

String wifiSsid = WIFI_SSID;
String wifiPassword = WIFI_PASSWORD;

// ---- Debug helpers ----
void printBoth(const String &msg) {
    Serial.println(msg);
    if (btSerial.hasClient()) {
        btSerial.println(msg);
    }
}

// ---- Helpers for Wi-Fi telemetry ----
String activeModes() {
    String modes = "base"; // base visualization of throttle/rpm/temp
    if (state.brakePedal) modes += ", brake";
    if (state.handBrake) modes += ", handbrake";
    if (state.revLimiter) modes += ", rev_limiter";
    if (state.rpm >= state.rpmRedline) modes += ", redline";
    return modes;
}

void appendFrameToLog(const twai_message_t &msg) {
    ReceivedFrame &slot = frameLog[frameLogHead];
    slot.id = msg.identifier;
    slot.dlc = msg.data_length_code;
    memcpy(slot.data, msg.data, msg.data_length_code);
    slot.timestampMs = millis();
    frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
}

// ---- Configuration persistence ----
void loadPreferences() {
    if (!prefs.begin(PREF_NAMESPACE, true)) {
        Serial.println("Failed to open preferences (read)");
        return;
    }
    String storedSsid = prefs.getString("ssid", wifiSsid);
    String storedPass = prefs.getString("pass", wifiPassword);
    prefs.end();

    wifiSsid = storedSsid;
    wifiPassword = storedPass;
}

void savePreferences() {
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        Serial.println("Failed to open preferences (write)");
        return;
    }
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPassword);
    prefs.end();
}

String byteToHex(uint8_t b) {
    const char hex[] = "0123456789ABCDEF";
    String out;
    out += hex[b >> 4];
    out += hex[b & 0x0F];
    return out;
}

String formatFrame(const twai_message_t &msg) {
    String out = "ID 0x";
    out += String(msg.identifier, HEX);
    out += " DLC";
    out += String(msg.data_length_code);
    out += " DATA";
    for (uint8_t i = 0; i < msg.data_length_code; ++i) {
        out += " ";
        out += byteToHex(msg.data[i]);
    }
    return out;
}

void handleApiState() {
    String json = "{";
    json += "\"throttle_percent\":" + String(state.throttlePercent) + ",";
    json += "\"rpm\":" + String(state.rpm) + ",";
    json += "\"rpm_redline\":" + String(state.rpmRedline) + ",";
    json += "\"coolant_c\":" + String(state.coolant10x / 10.0f, 1) + ",";
    json += "\"brake_pedal\":" + String(state.brakePedal ? "true" : "false") + ",";
    json += "\"handbrake\":" + String(state.handBrake ? "true" : "false") + ",";
    json += "\"rev_limiter\":" + String(state.revLimiter ? "true" : "false") + ",";
    json += "\"active_modes\":\"" + activeModes() + "\",";
    json += "\"frames\":[";
    bool first = true;
    for (size_t i = 0; i < FRAME_LOG_SIZE; ++i) {
        size_t idx = (frameLogHead + i) % FRAME_LOG_SIZE;
        const ReceivedFrame &f = frameLog[idx];
        if (f.dlc == 0 && f.timestampMs == 0) continue; // skip empty entries
        if (!first) json += ",";
        json += "{\"id\":\"0x" + String(f.id, HEX) + "\",";
        json += "\"dlc\":" + String(f.dlc) + ",";
        json += "\"timestamp_ms\":" + String(f.timestampMs) + ",";
        json += "\"data\":\"";
        for (uint8_t b = 0; b < f.dlc; ++b) {
            json += byteToHex(f.data[b]);
            if (b + 1 < f.dlc) json += " ";
        }
        json += "\"}";
        first = false;
    }
    json += "]}";

    server.send(200, "application/json", json);
}

void handleRoot() {
    String html = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>CAN LED Telemetry</title>
  <style>body{font-family:Arial,Helvetica,sans-serif;margin:20px;}code{background:#f2f2f2;padding:2px 4px;}table{border-collapse:collapse;}td,th{padding:6px 10px;border:1px solid #ddd;}</style>
</head>
<body>
  <h1>CAN LED Telemetry</h1>
  <p><strong>Active modes:</strong> %MODES%</p>
  <ul>
    <li>Throttle: %THR%%</li>
    <li>RPM: %RPM% / redline %REDLINE%</li>
    <li>Coolant: %COOLANT%C</li>
    <li>Brake: %BRAKE%</li>
    <li>Handbrake: %HAND%</li>
    <li>Rev limiter: %REV%</li>
  </ul>
  <h2>Recent CAN frames</h2>
  <table>
    <tr><th>#</th><th>ID</th><th>DLC</th><th>Data</th><th>Age (ms)</th></tr>
    %ROWS%
  </table>
  <p>JSON API: <a href="/api/state">/api/state</a></p>
</body>
</html>)rawliteral";

    html.replace("%MODES%", activeModes());
    html.replace("%THR%", String(state.throttlePercent));
    html.replace("%RPM%", String(state.rpm));
    html.replace("%REDLINE%", String(state.rpmRedline));
    html.replace("%COOLANT%", String(state.coolant10x / 10.0f, 1));
    html.replace("%BRAKE%", state.brakePedal ? "ON" : "OFF");
    html.replace("%HAND%", state.handBrake ? "ON" : "OFF");
    html.replace("%REV%", state.revLimiter ? "ON" : "OFF");

    String rows;
    uint32_t now = millis();
    size_t rowIndex = 0;
    for (size_t i = 0; i < FRAME_LOG_SIZE; ++i) {
        size_t idx = (frameLogHead + i) % FRAME_LOG_SIZE;
        const ReceivedFrame &f = frameLog[idx];
        if (f.dlc == 0 && f.timestampMs == 0) continue;
        rows += "<tr><td>" + String(++rowIndex) + "</td><td>0x" + String(f.id, HEX) + "</td><td>" + String(f.dlc) + "</td><td>";
        for (uint8_t b = 0; b < f.dlc; ++b) {
            rows += byteToHex(f.data[b]);
            if (b + 1 < f.dlc) rows += " ";
        }
        rows += "</td><td>" + String(now - f.timestampMs) + "</td></tr>";
    }
    if (rows.isEmpty()) {
        rows = "<tr><td colspan=5>No frames yet</td></tr>";
    }
    html.replace("%ROWS%", rows);
    server.send(200, "text/html", html);
}

void ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();
    if (now - lastWifiAttempt < WIFI_RECONNECT_MS) {
        return;
    }
    lastWifiAttempt = now;

    Serial.printf("Connecting to Wi-Fi SSID '%s'...\n", wifiSsid.c_str());
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
        delay(200);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Wi-Fi connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Wi-Fi connect timeout");
    }
}

// ---- Bluetooth configurator ----
void printHelp() {
    printBoth("Commands:");
    printBoth(" HELP               - show this help");
    printBoth(" STATUS             - show Wi-Fi status and active modes");
    printBoth(" SSID <name>        - set Wi-Fi SSID");
    printBoth(" PASS <password>    - set Wi-Fi password");
    printBoth(" SAVE               - persist Wi-Fi settings to flash");
}

void printStatus() {
    String status = "Wi-Fi: ";
    status += (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
    status += " (SSID='" + wifiSsid + "')";
    printBoth(status);
    if (WiFi.status() == WL_CONNECTED) {
        printBoth("IP: " + WiFi.localIP().toString());
    }
    printBoth("Active modes: " + activeModes());
}

void reconnectWifi() {
    WiFi.disconnect();
    lastWifiAttempt = 0;
    ensureWifi();
}

void handleBluetoothCommand(const String &line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.equalsIgnoreCase("HELP")) {
        printHelp();
    } else if (trimmed.equalsIgnoreCase("STATUS")) {
        printStatus();
    } else if (trimmed.startsWith("SSID ")) {
        wifiSsid = trimmed.substring(5);
        printBoth("SSID updated to '" + wifiSsid + "'. Reconnecting...");
        reconnectWifi();
    } else if (trimmed.startsWith("PASS ")) {
        wifiPassword = trimmed.substring(5);
        printBoth("Password updated. Reconnecting...");
        reconnectWifi();
    } else if (trimmed.equalsIgnoreCase("SAVE")) {
        savePreferences();
        printBoth("Saved Wi-Fi settings to flash.");
    } else if (!trimmed.isEmpty()) {
        printBoth("Unknown command. Type HELP.");
    }
}

void pollBluetooth() {
    static String buffer;
    while (btSerial.available()) {
        char c = (char)btSerial.read();
        if (c == '\n' || c == '\r') {
            if (!buffer.isEmpty()) {
                handleBluetoothCommand(buffer);
                buffer.clear();
            }
        } else {
            buffer += c;
        }
    }
}

void setupServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.begin();
    Serial.printf("HTTP server started on port %u\n", HTTP_PORT);
}

// ---- Utility functions ----
void blendSegment(int start, int end, const CRGB &color) {
    start = constrain(start, 0, LED_COUNT);
    end = constrain(end, 0, LED_COUNT);
    for (int i = start; i < end; ++i) {
        leds[i] = blend(leds[i], color, 192); // gentle blend instead of full overwrite
    }
}

void drawThrottleBar() {
    float fraction = state.throttlePercent / 100.0f;
    int lit = static_cast<int>(fraction * LED_COUNT + 0.5f);
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = (i < lit) ? CRGB::Green : CRGB::Black;
    }
}

void drawRpmGradient() {
    float fraction = (float)state.rpm / (float)state.rpmRedline;
    fraction = constrain(fraction, 0.0f, 1.2f); // allow a bit beyond redline
    int lit = static_cast<int>(fraction * LED_COUNT + 0.5f);
    for (int i = 0; i < lit && i < LED_COUNT; ++i) {
        // Color from blue (low) to yellow (near redline)
        float ratio = (float)i / (float)LED_COUNT;
        CRGB color = blend(CRGB::Blue, CRGB::Yellow, (uint8_t)(ratio * 255));
        leds[i] = blend(leds[i], color, 128);
    }
    if (fraction >= 1.0f) {
        // Past redline -> flashing red overlay
        uint8_t pulse = beatsin8(5, 64, 255);
        fill_solid(leds, LED_COUNT, CRGB(pulse, 0, 0));
    }
}

void drawCoolantIndicator() {
    // Map 60-110°C to blue->green->red
    float tempC = state.coolant10x / 10.0f;
    float norm = (tempC - 60.0f) / 50.0f;
    norm = constrain(norm, 0.0f, 1.0f);
    CRGB color = (norm < 0.5f) ? blend(CRGB::Blue, CRGB::Green, (uint8_t)(norm * 2 * 255))
                               : blend(CRGB::Green, CRGB::Red, (uint8_t)((norm - 0.5f) * 2 * 255));
    leds[LED_COUNT - 1] = color; // place indicator on the last pixel
}

void applyBrakeOverlays() {
    if (state.brakePedal) {
        fill_solid(leds, LED_COUNT, CRGB::Red);
    }
    if (state.handBrake) {
        // Purple overlay on the first quarter when handbrake set
        int section = LED_COUNT / 4;
        blendSegment(0, section, CRGB(200, 0, 200));
    }
}

void drawRevLimiter() {
    if (!state.revLimiter) return;
    uint8_t pulse = beatsin8(8, 96, 255);
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], CRGB(pulse, pulse, 0), 192);
    }
}

// ---- CAN handling ----
void processFrame(const twai_message_t &msg) {
    switch (msg.identifier) {
        case ID_THROTTLE:
            if (msg.data_length_code >= 1) {
                state.throttlePercent = constrain(msg.data[0], (uint8_t)0, (uint8_t)100);
            }
            break;
        case ID_BRAKES:
            if (msg.data_length_code >= 1) {
                state.brakePedal = msg.data[0] & 0x01;
                state.handBrake = msg.data[0] & 0x02;
            }
            break;
        case ID_RPM:
            if (msg.data_length_code >= 2) {
                state.rpm = msg.data[0] | (msg.data[1] << 8);
            }
            break;
        case ID_COOLANT:
            if (msg.data_length_code >= 2) {
                state.coolant10x = msg.data[0] | (msg.data[1] << 8);
            }
            break;
        case ID_REV_LIM:
            if (msg.data_length_code >= 1) {
                state.revLimiter = msg.data[0] != 0;
            }
            break;
        default:
            break;
    }

    Serial.print("CAN frame: ");
    Serial.println(formatFrame(msg));
}

void configureCan() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        Serial.println("TWAI driver installed");
    } else {
        Serial.println("TWAI driver install failed");
        return;
    }

    if (twai_start() == ESP_OK) {
        Serial.println("CAN bus started at 1 Mbps");
    } else {
        Serial.println("Failed to start CAN bus");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("Booting CAN LED firmware...");
    loadPreferences();
    Serial.printf("Using SSID: '%s'\n", wifiSsid.c_str());

    btSerial.begin(BT_DEVICE_NAME);
    Serial.printf("Bluetooth configurator ready: device '%s'\n", BT_DEVICE_NAME);
    printHelp();

    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);

    configureCan();

    ensureWifi();
    setupServer();
}

void loop() {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
        processFrame(message);
        appendFrameToLog(message);
    }

    // Compose the LED pattern for the current state
    drawThrottleBar();
    drawRpmGradient();
    drawCoolantIndicator();
    applyBrakeOverlays();
    drawRevLimiter();

    FastLED.show();

    ensureWifi();
    server.handleClient();
    pollBluetooth();
}

