#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"

// ----------- User configuration -----------
static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_21;  // Check silk screen, adjust if needed
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_22;  // Check silk screen, adjust if needed
static constexpr int LED_PIN = 4;                      // Data pin for the addressable strip
static constexpr int LED_COUNT = 60;                   // Number of LEDs on the strip
static constexpr int LED_BRIGHTNESS = 180;             // 0-255
static constexpr uint32_t CAN_BITRATE = 1000000;       // 1 Mbps

static constexpr char WIFI_SSID[] = "CANLED_AP";             // SSID for the device access point
static constexpr char WIFI_PASSWORD[] = "canled123";         // WPA2 password for the AP (min 8 chars)
static constexpr uint16_t HTTP_PORT = 80;

// CAN message identifiers (standard 11-bit frames)
static constexpr uint32_t ID_THROTTLE = 0x100; // data[0]: 0-100 %
static constexpr uint32_t ID_PEDALS   = 0x101; // data[0]: brake 0-100 %, data[1]: handbrake 0-100 %, data[2]: clutch 0-100 %
static constexpr uint32_t ID_RPM      = 0x102; // uint16_t little-endian: RPM
static constexpr uint32_t ID_COOLANT  = 0x103; // uint16_t little-endian: temperature * 10 °C
static constexpr uint32_t ID_REV_LIM  = 0x104; // data[0] non-zero when limiter active
static constexpr uint32_t ID_ALS      = 0x105; // data[0] non-zero when anti-lag active
static constexpr uint32_t ID_OIL_PRES = 0x106; // uint16_t little-endian: oil pressure * 10 kPa (≈0.1 bar)
static constexpr uint32_t ID_IGNITION = 0x107; // data[0] non-zero when ignition is on

// Data validation ranges
static constexpr uint16_t MAX_REASONABLE_RPM = 12000;
static constexpr uint16_t MAX_REASONABLE_COOLANT = 1500; // 150°C
static constexpr uint16_t MAX_REASONABLE_OIL_PRESSURE = 1000; // 10 bar (100 kPa)

// Timeouts and retry configuration
static constexpr uint32_t DATA_STALE_THRESHOLD_MS = 2000; // Data considered stale after 2s
static constexpr uint32_t CAN_TIMEOUT_WARNING_MS = 5000; // Warn if no CAN messages for 5s
static constexpr uint8_t MAX_WIFI_RETRIES = 5;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;

// CAN bus status tracking
enum class CanStatus {
    NOT_INITIALIZED,
    INITIALIZING,
    RUNNING,
    FAILED_DRIVER_INSTALL,
    FAILED_START,
    BUS_OFF,
    ERROR_PASSIVE
};

// Wi-Fi status tracking
enum class WifiStatus {
    NOT_STARTED,
    STARTING,
    RUNNING,
    FAILED
};

CanStatus canBusStatus = CanStatus::NOT_INITIALIZED;
String canErrorMessage = "";
uint32_t lastCanMessageTime = 0;

WifiStatus wifiStatus = WifiStatus::NOT_STARTED;
uint8_t wifiRetryCount = 0;
uint32_t lastWifiRetry = 0;

bool ledStripInitialized = false;

CRGB leds[LED_COUNT];

// Optimized: Pre-computed lookup table for throttle/RPM bar lengths
// Avoids multiplication and division in hot path
struct LookupTables {
    uint8_t throttleLedCount[101];  // throttle % -> LED count
    uint16_t rpmLedCount[LED_COUNT + LED_COUNT/5 + 1]; // LED position -> RPM threshold

    void init(uint16_t redline) {
        // Pre-compute throttle percentage to LED count mapping
        for (int i = 0; i <= 100; ++i) {
            throttleLedCount[i] = (i * LED_COUNT + 50) / 100;
        }

        // Pre-compute RPM thresholds for each LED position
        uint32_t maxLit = LED_COUNT + LED_COUNT / 5;
        for (uint32_t i = 0; i <= maxLit; ++i) {
            rpmLedCount[i] = (i * redline + LED_COUNT / 2) / LED_COUNT;
        }
    }
};

LookupTables lookupTables;

struct VehicleState {
    uint8_t throttlePercent = 0;
    uint8_t brakePercent = 0;
    uint8_t handBrakePercent = 0;
    uint8_t clutchPercent = 0;
    uint16_t rpm = 0;
    uint16_t rpmRedline = 6500;
    uint16_t coolant10x = 900; // 90.0°C
    bool revLimiter = false;
    bool alsActive = false;
    uint16_t oilPressure10kPa = 0; // 0.1 bar resolution (10 kPa)
    bool ignitionOn = false;
};

VehicleState state;

// Data age tracking for staleness detection
struct DataAgeTracker {
    uint32_t lastThrottleUpdate = 0;
    uint32_t lastRpmUpdate = 0;
    uint32_t lastCoolantUpdate = 0;
    uint32_t lastOilPressureUpdate = 0;
};

DataAgeTracker dataAge;

// Data validation failure tracking
struct DataValidationStats {
    uint32_t invalidRpmCount = 0;
    uint32_t invalidCoolantCount = 0;
    uint32_t invalidOilPressureCount = 0;
    uint32_t totalMessagesProcessed = 0;
};

DataValidationStats validationStats;

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
bool apStarted = false;
bool serverStarted = false;

// ---- Forward declarations ----
void setupServer();

// ---- Helpers for Wi-Fi telemetry ----
bool isWarmingUp();
bool isPanicError();

// Optimized: use buffer to avoid String reallocations
void activeModes(char* buf, size_t bufSize) {
    int pos = snprintf(buf, bufSize, "base");
    if (state.brakePercent > 0 && pos < (int)bufSize - 10) pos += snprintf(buf + pos, bufSize - pos, ", brake");
    if (state.handBrakePercent > 0 && pos < (int)bufSize - 15) pos += snprintf(buf + pos, bufSize - pos, ", handbrake");
    if (state.clutchPercent > 0 && pos < (int)bufSize - 10) pos += snprintf(buf + pos, bufSize - pos, ", clutch");
    if (state.revLimiter && pos < (int)bufSize - 15) pos += snprintf(buf + pos, bufSize - pos, ", rev_limiter");
    if (state.rpm >= state.rpmRedline && pos < (int)bufSize - 12) pos += snprintf(buf + pos, bufSize - pos, ", redline");
    if (state.alsActive && pos < (int)bufSize - 7) pos += snprintf(buf + pos, bufSize - pos, ", als");
    if (isWarmingUp() && pos < (int)bufSize - 14) pos += snprintf(buf + pos, bufSize - pos, ", warming_up");
    if (isPanicError() && pos < (int)bufSize - 13) pos += snprintf(buf + pos, bufSize - pos, ", panic_oil");
    if (state.ignitionOn && state.rpm == 0 && pos < (int)bufSize - 27) pos += snprintf(buf + pos, bufSize - pos, ", ignition_on_engine_off");
}

inline bool isWarmingUp() {
    return state.coolant10x < 600; // <60.0°C
}

inline bool isPanicError() {
    return state.throttlePercent > 40 && state.oilPressure10kPa < 200; // <2.0 bar @ >40% TPS
}

// Optimized: inline and reduce function call overhead
inline bool isDataStale() {
    uint32_t now = millis();
    // Check the most recent update across all tracked data fields
    uint32_t lastUpdate = max(dataAge.lastRpmUpdate,
                              max(dataAge.lastThrottleUpdate,
                                  max(dataAge.lastCoolantUpdate, dataAge.lastOilPressureUpdate)));
    return lastUpdate > 0 && (now - lastUpdate) > DATA_STALE_THRESHOLD_MS;
}

inline bool hasCanError() {
    return canBusStatus != CanStatus::RUNNING;
}

inline bool hasWifiError() {
    return wifiStatus == WifiStatus::FAILED;
}

void appendFrameToLog(const twai_message_t &msg) {
    ReceivedFrame &slot = frameLog[frameLogHead];
    slot.id = msg.identifier;

    // Validate DLC before using it to prevent buffer overflow
    uint8_t dlc = msg.data_length_code;
    if (dlc > 8) {
        Serial.printf("ERROR: Invalid DLC %u in CAN ID 0x%03X, clamping to 8\n", dlc, msg.identifier);
        dlc = 8;
    }

    slot.dlc = dlc;
    memcpy(slot.data, msg.data, dlc);
    slot.timestampMs = millis();
    frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
}

// Optimized: avoid String allocation, write directly to buffer
void byteToHex(uint8_t b, char* out) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[b >> 4];
    out[1] = hex[b & 0x0F];
}

String formatTenths(uint16_t value10) {
    char buf[10];
    uint16_t whole = value10 / 10;
    uint16_t frac = value10 % 10;
    snprintf(buf, sizeof(buf), "%u.%u", whole, frac);
    return String(buf);
}

String formatHundredths(uint16_t value100) {
    char buf[12];
    uint16_t whole = value100 / 100;
    uint16_t frac = value100 % 100;
    snprintf(buf, sizeof(buf), "%u.%02u", whole, frac);
    return String(buf);
}

// Optimized: use pre-allocated buffer to avoid heap fragmentation
void formatFrame(const twai_message_t &msg, char* buf, size_t bufSize) {
    char hexBuf[2];
    int pos = snprintf(buf, bufSize, "ID 0x%03X DLC%u DATA", msg.identifier, msg.data_length_code);
    for (uint8_t i = 0; i < msg.data_length_code && pos < (int)bufSize - 3; ++i) {
        buf[pos++] = ' ';
        byteToHex(msg.data[i], hexBuf);
        buf[pos++] = hexBuf[0];
        buf[pos++] = hexBuf[1];
    }
    buf[pos] = '\0';
}

const char *boolWord(bool v) { return v ? "true" : "false"; }

void streamApiState() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();

    if (!client.connected()) {
        Serial.println("Client disconnected before API response");
        return;
    }

    client.print('{');
    client.print("\"throttle_percent\":");
    client.print(state.throttlePercent);
    client.print(",\"rpm\":");
    client.print(state.rpm);
    client.print(",\"rpm_redline\":");
    client.print(state.rpmRedline);
    client.print(",\"coolant_c\":\"");
    client.print(formatTenths(state.coolant10x));
    client.print("\",");
    client.print("\"oil_pressure_bar\":\"");
    client.print(formatHundredths(state.oilPressure10kPa * 10));
    client.print("\",");
    client.print("\"brake_percent\":");
    client.print(state.brakePercent);
    client.print(",\"handbrake_percent\":");
    client.print(state.handBrakePercent);
    client.print(",\"clutch_percent\":");
    client.print(state.clutchPercent);
    client.print(",\"rev_limiter\":");
    client.print(boolWord(state.revLimiter));
    client.print(",\"als_active\":");
    client.print(boolWord(state.alsActive));
    client.print(",\"warming_up\":");
    client.print(boolWord(isWarmingUp()));
    client.print(",\"panic_error\":");
    client.print(boolWord(isPanicError()));
    client.print(",\"ignition_on\":");
    client.print(boolWord(state.ignitionOn));
    client.print(",\"active_modes\":\"");
    char modesBuf[128];
    activeModes(modesBuf, sizeof(modesBuf));
    client.print(modesBuf);
    client.print("\",\"can_bus_status\":\"");

    // Add CAN bus status
    switch (canBusStatus) {
        case CanStatus::NOT_INITIALIZED: client.print("not_initialized"); break;
        case CanStatus::INITIALIZING: client.print("initializing"); break;
        case CanStatus::RUNNING: client.print("running"); break;
        case CanStatus::FAILED_DRIVER_INSTALL: client.print("failed_driver_install"); break;
        case CanStatus::FAILED_START: client.print("failed_start"); break;
        case CanStatus::BUS_OFF: client.print("bus_off"); break;
        case CanStatus::ERROR_PASSIVE: client.print("error_passive"); break;
        default: client.print("unknown"); break;
    }

    client.print("\",\"can_error_message\":\"");
    client.print(canErrorMessage);
    client.print("\",\"data_stale\":");
    client.print(boolWord(isDataStale()));
    client.print(",\"led_strip_ok\":");
    client.print(boolWord(ledStripInitialized));
    client.print(",\"wifi_status\":\"");

    // Add Wi-Fi status
    switch (wifiStatus) {
        case WifiStatus::NOT_STARTED: client.print("not_started"); break;
        case WifiStatus::STARTING: client.print("starting"); break;
        case WifiStatus::RUNNING: client.print("running"); break;
        case WifiStatus::FAILED: client.print("failed"); break;
        default: client.print("unknown"); break;
    }

    client.print("\",\"validation_stats\":{");
    client.print("\"total_messages\":");
    client.print(validationStats.totalMessagesProcessed);
    client.print(",\"invalid_rpm\":");
    client.print(validationStats.invalidRpmCount);
    client.print(",\"invalid_coolant\":");
    client.print(validationStats.invalidCoolantCount);
    client.print(",\"invalid_oil_pressure\":");
    client.print(validationStats.invalidOilPressureCount);
    client.print("},\"frames\":");
    client.print('[');

    char hexBuf[2];
    bool first = true;
    for (size_t i = 0; i < FRAME_LOG_SIZE; ++i) {
        size_t idx = (frameLogHead + i) % FRAME_LOG_SIZE;
        const ReceivedFrame &f = frameLog[idx];
        if (f.dlc == 0 && f.timestampMs == 0) continue;
        if (!first) client.print(',');
        client.print('{');
        client.print("\"id\":\"0x");
        client.print(String(f.id, HEX));
        client.print("\",\"dlc\":");
        client.print(f.dlc);
        client.print(",\"timestamp_ms\":");
        client.print(f.timestampMs);
        client.print(",\"data\":\"");
        for (uint8_t b = 0; b < f.dlc; ++b) {
            byteToHex(f.data[b], hexBuf);
            client.write(hexBuf, 2);
            if (b + 1 < f.dlc) client.print(' ');
        }
        client.print("\"}");
        first = false;
    }
    client.print(']');
    client.print('}');
    client.stop();
}

void handleApiState() {
    streamApiState();
}

void handleRoot() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    WiFiClient client = server.client();

    client.print(F("<!doctype html><html><head><meta charset=\"utf-8\"/><title>CAN LED Telemetry</title><style>body{font-family:Arial,Helvetica,sans-serif;margin:20px;}code{background:#f2f2f2;padding:2px 4px;}table{border-collapse:collapse;}td,th{padding:6px 10px;border:1px solid #ddd;}</style></head><body>"));
    client.print(F("<h1>CAN LED Telemetry</h1>"));

    // Display system status warnings
    if (canBusStatus != CanStatus::RUNNING) {
        client.print(F("<p style='color:red;font-weight:bold;'>⚠ CAN BUS ERROR: "));
        client.print(canErrorMessage);
        client.print(F("</p>"));
    }
    if (wifiStatus == WifiStatus::FAILED) {
        client.print(F("<p style='color:orange;font-weight:bold;'>⚠ Wi-Fi failed after multiple retries</p>"));
    }
    if (!ledStripInitialized) {
        client.print(F("<p style='color:orange;font-weight:bold;'>⚠ LED strip initialization may have failed</p>"));
    }
    if (isDataStale()) {
        client.print(F("<p style='color:orange;font-weight:bold;'>⚠ Vehicle data is stale (no recent updates)</p>"));
    }

    client.print(F("<p><strong>Active modes:</strong> "));
    char modesBuf[128];
    activeModes(modesBuf, sizeof(modesBuf));
    client.print(modesBuf);
    client.print(F("</p><ul>"));
    client.printf("<li>Throttle: %u%%</li>", state.throttlePercent);
    client.printf("<li>RPM: %u / redline %u</li>", state.rpm, state.rpmRedline);
    client.print("<li>Coolant: ");
    client.print(formatTenths(state.coolant10x));
    client.print("C</li><li>Oil pressure: ");
    client.print(formatHundredths(state.oilPressure10kPa * 10));
    client.print(" bar</li>");
    client.printf("<li>Brake: %u%%</li>", state.brakePercent);
    client.printf("<li>Handbrake: %u%%</li>", state.handBrakePercent);
    client.printf("<li>Clutch: %u%%</li>", state.clutchPercent);
    client.printf("<li>Rev limiter: %s</li>", state.revLimiter ? "ON" : "OFF");
    client.printf("<li>ALS: %s</li>", state.alsActive ? "ON" : "OFF");
    client.printf("<li>Warming up: %s</li>", isWarmingUp() ? "YES" : "NO");
    client.printf("<li>Panic (low oil @ throttle): %s</li>", isPanicError() ? "YES" : "NO");
    client.printf("<li>Ignition on: %s</li>", state.ignitionOn ? "YES" : "NO");
    client.print(F("</ul><h2>Data Validation</h2><ul>"));
    client.printf("<li>Total CAN messages: %u</li>", validationStats.totalMessagesProcessed);
    client.printf("<li>Invalid RPM: %u</li>", validationStats.invalidRpmCount);
    client.printf("<li>Invalid coolant: %u</li>", validationStats.invalidCoolantCount);
    client.printf("<li>Invalid oil pressure: %u</li>", validationStats.invalidOilPressureCount);
    client.print(F("</ul><h2>Recent CAN frames</h2><table><tr><th>#</th><th>ID</th><th>DLC</th><th>Data</th><th>Age (ms)</th></tr>"));

    uint32_t now = millis();
    size_t rowIndex = 0;
    for (size_t i = 0; i < FRAME_LOG_SIZE; ++i) {
        size_t idx = (frameLogHead + i) % FRAME_LOG_SIZE;
        const ReceivedFrame &f = frameLog[idx];
        if (f.dlc == 0 && f.timestampMs == 0) continue;
        client.print(F("<tr><td>"));
        client.print(++rowIndex);
        client.print(F("</td><td>0x"));
        client.print(String(f.id, HEX));
        client.print(F("</td><td>"));
        client.print(f.dlc);
        client.print(F("</td><td>"));
        char hexBuf[2];
        for (uint8_t b = 0; b < f.dlc; ++b) {
            byteToHex(f.data[b], hexBuf);
            client.write(hexBuf, 2);
            if (b + 1 < f.dlc) client.print(' ');
        }
        client.print(F("</td><td>"));
        client.print(now - f.timestampMs);
        client.print(F("</td></tr>"));
    }

    if (rowIndex == 0) {
        client.print(F("<tr><td colspan=5>No frames yet</td></tr>"));
    }

    client.print(F("</table><p>JSON API: <a href=\"/api/state\">/api/state</a></p></body></html>"));
    client.stop();
}

void ensureAccessPoint() {
    if (apStarted) return;
    if (wifiStatus == WifiStatus::FAILED) return; // Don't retry indefinitely

    uint32_t now = millis();
    if (wifiRetryCount > 0 && (now - lastWifiRetry) < WIFI_RETRY_INTERVAL_MS) {
        return; // Wait before retrying
    }

    Serial.printf("Starting Wi-Fi access point '%s' (attempt %u/%u)...\n",
                  WIFI_SSID, wifiRetryCount + 1, MAX_WIFI_RETRIES);
    wifiStatus = WifiStatus::STARTING;
    WiFi.mode(WIFI_AP);

    if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
        apStarted = true;
        wifiStatus = WifiStatus::RUNNING;
        Serial.print("AP ready, connect to http://");
        Serial.print(WiFi.softAPIP());
        Serial.println('/');

        // Start HTTP server now that AP is running
        setupServer();
    } else {
        wifiRetryCount++;
        lastWifiRetry = now;

        if (wifiRetryCount >= MAX_WIFI_RETRIES) {
            wifiStatus = WifiStatus::FAILED;
            Serial.println("Failed to start AP after maximum retries. Wi-Fi will be disabled.");
        } else {
            Serial.printf("Failed to start AP. Will retry in %u ms.\n", WIFI_RETRY_INTERVAL_MS);
        }
    }
}

void setupServer() {
    if (!apStarted) {
        Serial.println("Cannot start HTTP server: AP not running");
        return;
    }

    if (serverStarted) {
        return; // Already started
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.begin();
    serverStarted = true;
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

// Optimized: use pre-computed lookup table
void drawThrottleBar() {
    int lit = lookupTables.throttleLedCount[state.throttlePercent];
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = (i < lit) ? CRGB::Green : CRGB::Black;
    }
}

void drawRpmGradient() {
    // Prevent division by zero
    if (state.rpmRedline == 0) return;

    uint32_t lit = (static_cast<uint32_t>(state.rpm) * LED_COUNT + state.rpmRedline / 2) / state.rpmRedline;
    const uint32_t litCap = LED_COUNT + LED_COUNT / 5; // allow ~20% past redline
    if (lit > litCap) {
        lit = litCap;
    }
    for (uint32_t i = 0; i < lit && i < static_cast<uint32_t>(LED_COUNT); ++i) {
        // Color from blue (low) to yellow (near redline)
        uint8_t ratio = (static_cast<uint32_t>(i) * 255) / LED_COUNT;
        CRGB color = blend(CRGB::Blue, CRGB::Yellow, ratio);
        leds[i] = blend(leds[i], color, 128);
    }
    if (state.rpm >= state.rpmRedline) {
        // Past redline -> flashing red overlay
        uint8_t pulse = beatsin8(5, 64, 255);
        fill_solid(leds, LED_COUNT, CRGB(pulse, 0, 0));
    }
}

void drawCoolantIndicator() {
    // Map 60-110°C to blue->green->red using integer math
    uint16_t temp10 = state.coolant10x;
    if (temp10 < 600) temp10 = 600;
    if (temp10 > 1100) temp10 = 1100;

    CRGB color;
    if (temp10 <= 850) {
        uint8_t mix = static_cast<uint32_t>(temp10 - 600) * 255 / 250; // 60-85C
        color = blend(CRGB::Blue, CRGB::Green, mix);
    } else {
        uint8_t mix = static_cast<uint32_t>(temp10 - 850) * 255 / 250; // 85-110C
        color = blend(CRGB::Green, CRGB::Red, mix);
    }
    leds[LED_COUNT - 1] = color; // place indicator on the last pixel
}

// Optimized: combine all pedal overlays in a single pass
void applyPedalOverlays() {
    static constexpr int handbrakeSection = LED_COUNT / 4;
    static constexpr int clutchSection = LED_COUNT / 5;
    static constexpr int clutchStart = LED_COUNT - clutchSection;

    // Pre-calculate colors if pedals are active
    CRGB brakeColor = CRGB::Black;
    uint8_t brakeIntensity = 0;
    if (state.brakePercent > 0) {
        brakeIntensity = map(state.brakePercent, 0, 100, 20, 255);
        brakeColor = CRGB(brakeIntensity, 0, 0);
    }

    CRGB hbColor = CRGB::Black;
    if (state.handBrakePercent > 0) {
        uint8_t intensity = map(state.handBrakePercent, 0, 100, 10, 220);
        hbColor = CRGB(180, 0, 180);
        hbColor.nscale8_video(intensity);
    }

    CRGB clutchColor = CRGB::Black;
    if (state.clutchPercent > 0) {
        uint8_t intensity = map(state.clutchPercent, 0, 100, 10, 220);
        clutchColor = CRGB(0, 120, 255);
        clutchColor.nscale8_video(intensity);
    }

    // Single pass through the LED array
    for (int i = 0; i < LED_COUNT; ++i) {
        if (state.brakePercent > 0) {
            leds[i] = blend(leds[i], brakeColor, brakeIntensity);
        }
        if (state.handBrakePercent > 0 && i < handbrakeSection) {
            leds[i] = blend(leds[i], hbColor, 192);
        }
        if (state.clutchPercent > 0 && i >= clutchStart) {
            leds[i] = blend(leds[i], clutchColor, 192);
        }
    }
}

void drawRevLimiter() {
    if (!state.revLimiter) return;
    uint8_t pulse = beatsin8(8, 96, 255);
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], CRGB(pulse, pulse, 0), 192);
    }
}

void drawAlsOverlay() {
    if (!state.alsActive) return;
    uint8_t pulse = beatsin8(12, 80, 200);
    CRGB color = CRGB(pulse, 80, 0); // amber pulse
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], color, 160);
    }
}

void drawWarmingOverlay() {
    if (!isWarmingUp()) return;
    uint8_t pulse = beatsin8(6, 40, 120);
    CRGB color = CRGB(0, 100, pulse + 40); // teal/blue breathing
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], color, 128);
    }
}

void drawPanicError() {
    if (!isPanicError()) return;
    uint8_t pulse = beatsin8(18, 180, 255);
    CRGB alert = (millis() / 200) % 2 ? CRGB::Red : CRGB::White;
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(alert, CRGB(pulse, 0, 0), 200);
    }
}

void drawIgnitionStandby() {
    if (!state.ignitionOn || state.rpm != 0) return;
    uint8_t pulse = beatsin8(5, 20, 80);
    CRGB base = CRGB(80, 60, 0);
    base.nscale8_video(pulse + 60);
    fill_solid(leds, LED_COUNT, base);
}

// ---- Error visualization ----
void drawCanError() {
    if (canBusStatus == CanStatus::RUNNING) return;

    // Blink red to indicate CAN error
    uint8_t pulse = beatsin8(4, 0, 255);
    fill_solid(leds, LED_COUNT, CRGB(pulse, 0, 0));
}

void drawStaleDataWarning() {
    if (!isDataStale()) return;

    // Dim yellow blink for stale data
    uint8_t pulse = beatsin8(3, 20, 100);
    CRGB warning = CRGB(pulse, pulse / 2, 0);
    for (int i = 0; i < LED_COUNT; i += 4) {
        leds[i] = warning;
    }
}

// ---- CAN handling ----
void processFrame(const twai_message_t &msg) {
    uint32_t now = millis();
    validationStats.totalMessagesProcessed++;

    switch (msg.identifier) {
        case ID_THROTTLE:
            if (msg.data_length_code >= 1) {
                state.throttlePercent = constrain(msg.data[0], (uint8_t)0, (uint8_t)100);
                dataAge.lastThrottleUpdate = now;
            }
            break;

        case ID_PEDALS:
            if (msg.data_length_code >= 1) {
                state.brakePercent = constrain(msg.data[0], (uint8_t)0, (uint8_t)100);
            }
            if (msg.data_length_code >= 2) {
                state.handBrakePercent = constrain(msg.data[1], (uint8_t)0, (uint8_t)100);
            }
            if (msg.data_length_code >= 3) {
                state.clutchPercent = constrain(msg.data[2], (uint8_t)0, (uint8_t)100);
            }
            break;

        case ID_RPM:
            if (msg.data_length_code >= 2) {
                uint16_t rpm = msg.data[0] | (msg.data[1] << 8);
                if (rpm <= MAX_REASONABLE_RPM) {
                    state.rpm = rpm;
                    dataAge.lastRpmUpdate = now;
                } else {
                    validationStats.invalidRpmCount++;
                    Serial.printf("Invalid RPM value: %u (max %u) [total invalid: %u]\n",
                                  rpm, MAX_REASONABLE_RPM, validationStats.invalidRpmCount);
                }
            }
            break;

        case ID_COOLANT:
            if (msg.data_length_code >= 2) {
                uint16_t coolant = msg.data[0] | (msg.data[1] << 8);
                if (coolant <= MAX_REASONABLE_COOLANT) {
                    state.coolant10x = coolant;
                    dataAge.lastCoolantUpdate = now;
                } else {
                    validationStats.invalidCoolantCount++;
                    Serial.printf("Invalid coolant temp: %u (max %u) [total invalid: %u]\n",
                                  coolant, MAX_REASONABLE_COOLANT, validationStats.invalidCoolantCount);
                }
            }
            break;

        case ID_REV_LIM:
            if (msg.data_length_code >= 1) {
                state.revLimiter = msg.data[0] != 0;
            }
            break;

        case ID_ALS:
            if (msg.data_length_code >= 1) {
                state.alsActive = msg.data[0] != 0;
            }
            break;

        case ID_OIL_PRES:
            if (msg.data_length_code >= 2) {
                uint16_t oilPres = msg.data[0] | (msg.data[1] << 8);
                if (oilPres <= MAX_REASONABLE_OIL_PRESSURE) {
                    state.oilPressure10kPa = oilPres;
                    dataAge.lastOilPressureUpdate = now;
                } else {
                    validationStats.invalidOilPressureCount++;
                    Serial.printf("Invalid oil pressure: %u (max %u) [total invalid: %u]\n",
                                  oilPres, MAX_REASONABLE_OIL_PRESSURE, validationStats.invalidOilPressureCount);
                }
            }
            break;

        case ID_IGNITION:
            if (msg.data_length_code >= 1) {
                state.ignitionOn = msg.data[0] != 0;
            }
            break;

        default:
            break;
    }

    char frameBuf[64];
    formatFrame(msg, frameBuf, sizeof(frameBuf));
    Serial.print("CAN frame: ");
    Serial.println(frameBuf);
}

void configureCan() {
    canBusStatus = CanStatus::INITIALIZING;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t result = twai_driver_install(&g_config, &t_config, &f_config);
    if (result == ESP_OK) {
        Serial.println("TWAI driver installed");
    } else {
        canBusStatus = CanStatus::FAILED_DRIVER_INSTALL;
        canErrorMessage = "Driver install failed (error " + String(result) + "). Check GPIO pins.";
        Serial.println(canErrorMessage);
        return;
    }

    result = twai_start();
    if (result == ESP_OK) {
        Serial.println("CAN bus started at 1 Mbps");
        canBusStatus = CanStatus::RUNNING;
        canErrorMessage = "";
    } else {
        canBusStatus = CanStatus::FAILED_START;
        canErrorMessage = "Failed to start CAN bus (error " + String(result) + "). Check wiring and termination.";
        Serial.println(canErrorMessage);
    }
}

// CAN bus health monitoring
void monitorCanHealth() {
    if (canBusStatus != CanStatus::RUNNING) return;

    twai_status_info_t status;
    esp_err_t result = twai_get_status_info(&status);
    if (result == ESP_OK) {
        // Check for bus-off condition
        if (status.state == TWAI_STATE_BUS_OFF) {
            canBusStatus = CanStatus::BUS_OFF;

            // Attempt recovery
            esp_err_t recovery_result = twai_initiate_recovery();
            if (recovery_result == ESP_OK) {
                Serial.println("Bus-off recovery initiated");
                canErrorMessage = "CAN bus in BUS-OFF. Recovery in progress...";
            } else {
                Serial.printf("ERROR: Failed to initiate bus-off recovery (error %d)\n", recovery_result);
                canErrorMessage = "CAN bus BUS-OFF. Recovery failed. Check wiring and restart device.";
            }
            Serial.println(canErrorMessage);
            return;
        }

        // Check for error-passive (error counters between 128-255)
        // According to CAN spec, error-passive occurs when error counter >= 128
        if (status.rx_error_counter >= 128 || status.tx_error_counter >= 128) {
            canBusStatus = CanStatus::ERROR_PASSIVE;
            canErrorMessage = "CAN bus in ERROR-PASSIVE. RX errors: " + String(status.rx_error_counter) +
                            ", TX errors: " + String(status.tx_error_counter);
            Serial.println(canErrorMessage);
        } else if (canBusStatus == CanStatus::ERROR_PASSIVE) {
            // Recovered from error-passive (error counters back below 128)
            canBusStatus = CanStatus::RUNNING;
            canErrorMessage = "";
            Serial.println("CAN bus recovered to normal state");
        }

        // Warn if queue is getting full
        if (status.msgs_to_rx >= 16) {
            Serial.printf("WARNING: CAN RX queue nearly full (%u messages)\n", status.msgs_to_rx);
        }
    } else {
        // Failed to get CAN status info
        Serial.printf("ERROR: Failed to get CAN status info (error %d)\n", result);
    }

    // Check for message timeout (only when CAN is actually running)
    if (canBusStatus == CanStatus::RUNNING && lastCanMessageTime > 0 &&
        (millis() - lastCanMessageTime) > CAN_TIMEOUT_WARNING_MS) {
        Serial.println("WARNING: No CAN messages received for 5 seconds");
    }
}

// LED strip self-test
void setupLeds() {
    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);

    // Self-test: brief RGB cycle
    fill_solid(leds, LED_COUNT, CRGB::Black);
    leds[0] = CRGB::Red;
    FastLED.show();
    delay(150);

    leds[0] = CRGB::Green;
    FastLED.show();
    delay(150);

    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(150);

    fill_solid(leds, LED_COUNT, CRGB::Black);
    FastLED.show();

    ledStripInitialized = true;
    Serial.println("LED strip initialized and self-test complete");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("Booting CAN LED firmware...");
    Serial.printf("AP SSID: '%s'\n", WIFI_SSID);

    // Initialize lookup tables for optimized calculations
    lookupTables.init(state.rpmRedline);

    setupLeds();
    configureCan();

    ensureAccessPoint();
    setupServer();
}

void loop() {
    // Monitor CAN bus health
    monitorCanHealth();

    // Optimized: batch process CAN messages (up to 5 per loop iteration)
    // This reduces overhead and improves throughput under high CAN traffic
    static constexpr uint8_t MAX_MESSAGES_PER_LOOP = 5;
    uint8_t messagesProcessed = 0;

    while (messagesProcessed < MAX_MESSAGES_PER_LOOP) {
        twai_message_t message;
        // Use 0 timeout for batch processing (non-blocking after first message)
        uint32_t timeout = (messagesProcessed == 0) ? pdMS_TO_TICKS(10) : 0;
        esp_err_t result = twai_receive(&message, timeout);

        if (result == ESP_OK) {
            lastCanMessageTime = millis();
            processFrame(message);
            appendFrameToLog(message);
            messagesProcessed++;
        } else if (result != ESP_ERR_TIMEOUT) {
            // Log non-timeout errors
            Serial.printf("CAN receive error: %d\n", result);
            break;
        } else {
            // No more messages available
            break;
        }
    }

    fill_solid(leds, LED_COUNT, CRGB::Black);

    // Check for critical errors first
    if (canBusStatus != CanStatus::RUNNING) {
        drawCanError();
    } else if (state.ignitionOn && state.rpm == 0) {
        // Ignition standby mode
        drawIgnitionStandby();
        applyPedalOverlays();
        drawStaleDataWarning();
    } else {
        // Normal operation mode
        drawThrottleBar();
        drawRpmGradient();
        drawCoolantIndicator();
        applyPedalOverlays();
        drawRevLimiter();
        drawAlsOverlay();
        drawWarmingOverlay();
        drawStaleDataWarning();
    }

    // Panic error overrides everything except CAN error
    drawPanicError();

    FastLED.show();

    ensureAccessPoint();
    if (apStarted) {
        server.handleClient();
    }
}

