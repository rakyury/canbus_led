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

static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";        // Replace with your Wi-Fi network
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD"; // Replace with your Wi-Fi password
static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint32_t WIFI_RECONNECT_MS = 10000;

// CAN message identifiers (standard 11-bit frames)
static constexpr uint32_t ID_THROTTLE = 0x100; // data[0]: 0-100 %
static constexpr uint32_t ID_BRAKES   = 0x101; // data[0] bit0: brake pedal, bit1: handbrake
static constexpr uint32_t ID_RPM      = 0x102; // uint16_t little-endian: RPM
static constexpr uint32_t ID_COOLANT  = 0x103; // uint16_t little-endian: temperature * 10 °C
static constexpr uint32_t ID_REV_LIM  = 0x104; // data[0] non-zero when limiter active
static constexpr uint32_t ID_ALS      = 0x105; // data[0] non-zero when anti-lag active
static constexpr uint32_t ID_OIL_PRES = 0x106; // uint16_t little-endian: oil pressure * 10 kPa (≈0.1 bar)

CRGB leds[LED_COUNT];

struct VehicleState {
    uint8_t throttlePercent = 0;
    bool brakePedal = false;
    bool handBrake = false;
    uint16_t rpm = 0;
    uint16_t rpmRedline = 6500;
    uint16_t coolant10x = 900; // 90.0°C
    bool revLimiter = false;
    bool alsActive = false;
    uint16_t oilPressure10kPa = 0; // 0.1 bar resolution (10 kPa)
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
const char *wifiSsid = WIFI_SSID;
const char *wifiPassword = WIFI_PASSWORD;

// ---- Helpers for Wi-Fi telemetry ----
bool isWarmingUp();
bool isPanicError();

String activeModes() {
    String modes = "base"; // base visualization of throttle/rpm/temp
    if (state.brakePedal) modes += ", brake";
    if (state.handBrake) modes += ", handbrake";
    if (state.revLimiter) modes += ", rev_limiter";
    if (state.rpm >= state.rpmRedline) modes += ", redline";
    if (state.alsActive) modes += ", als";
    if (isWarmingUp()) modes += ", warming_up";
    if (isPanicError()) modes += ", panic_oil";
    return modes;
}

bool isWarmingUp() {
    return state.coolant10x < 600; // <60.0°C
}

bool isPanicError() {
    return state.throttlePercent > 40 && state.oilPressure10kPa < 200; // <2.0 bar @ >40% TPS
}

void appendFrameToLog(const twai_message_t &msg) {
    ReceivedFrame &slot = frameLog[frameLogHead];
    slot.id = msg.identifier;
    slot.dlc = msg.data_length_code;
    memcpy(slot.data, msg.data, msg.data_length_code);
    slot.timestampMs = millis();
    frameLogHead = (frameLogHead + 1) % FRAME_LOG_SIZE;
}

String byteToHex(uint8_t b) {
    const char hex[] = "0123456789ABCDEF";
    String out;
    out += hex[b >> 4];
    out += hex[b & 0x0F];
    return out;
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

const char *boolWord(bool v) { return v ? "true" : "false"; }

void streamApiState() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();

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
    client.print("\"brake_pedal\":");
    client.print(boolWord(state.brakePedal));
    client.print(",\"handbrake\":");
    client.print(boolWord(state.handBrake));
    client.print(",\"rev_limiter\":");
    client.print(boolWord(state.revLimiter));
    client.print(",\"als_active\":");
    client.print(boolWord(state.alsActive));
    client.print(",\"warming_up\":");
    client.print(boolWord(isWarmingUp()));
    client.print(",\"panic_error\":");
    client.print(boolWord(isPanicError()));
    client.print(",\"active_modes\":\"");
    client.print(activeModes());
    client.print("\",\"frames\":");
    client.print('[');

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
            client.print(byteToHex(f.data[b]));
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
    client.print(F("<h1>CAN LED Telemetry</h1><p><strong>Active modes:</strong> "));
    client.print(activeModes());
    client.print(F("</p><ul>"));
    client.printf("<li>Throttle: %u%%</li>", state.throttlePercent);
    client.printf("<li>RPM: %u / redline %u</li>", state.rpm, state.rpmRedline);
    client.print("<li>Coolant: ");
    client.print(formatTenths(state.coolant10x));
    client.print("C</li><li>Oil pressure: ");
    client.print(formatHundredths(state.oilPressure10kPa * 10));
    client.print(" bar</li>");
    client.printf("<li>Brake: %s</li>", state.brakePedal ? "ON" : "OFF");
    client.printf("<li>Handbrake: %s</li>", state.handBrake ? "ON" : "OFF");
    client.printf("<li>Rev limiter: %s</li>", state.revLimiter ? "ON" : "OFF");
    client.printf("<li>ALS: %s</li>", state.alsActive ? "ON" : "OFF");
    client.printf("<li>Warming up: %s</li>", isWarmingUp() ? "YES" : "NO");
    client.printf("<li>Panic (low oil @ throttle): %s</li>", isPanicError() ? "YES" : "NO");
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
        for (uint8_t b = 0; b < f.dlc; ++b) {
            client.print(byteToHex(f.data[b]));
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

void ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();
    if (now - lastWifiAttempt < WIFI_RECONNECT_MS) {
        return;
    }
    lastWifiAttempt = now;

    Serial.printf("Connecting to Wi-Fi SSID '%s'...\n", wifiSsid);
    WiFi.begin(wifiSsid, wifiPassword);
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
    int lit = (state.throttlePercent * LED_COUNT + 50) / 100;
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = (i < lit) ? CRGB::Green : CRGB::Black;
    }
}

void drawRpmGradient() {
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
        case ID_ALS:
            if (msg.data_length_code >= 1) {
                state.alsActive = msg.data[0] != 0;
            }
            break;
        case ID_OIL_PRES:
            if (msg.data_length_code >= 2) {
                state.oilPressure10kPa = msg.data[0] | (msg.data[1] << 8);
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
    Serial.printf("Using SSID: '%s'\n", wifiSsid);

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
    drawAlsOverlay();
    drawWarmingOverlay();
    drawPanicError();

    FastLED.show();

    ensureWifi();
    server.handleClient();
}

