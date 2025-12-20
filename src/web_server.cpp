#include "web_server.h"
#include "led_renderer.h"

// ========== Global Variables ==========
WebServer server(80);
bool apStarted = false;
bool serverStarted = false;

Preferences preferences;

#if ENABLE_WEBSOCKET
WebSocketsServer webSocket(81);
uint32_t lastWsBroadcast = 0;
#endif

// External state references (defined in main.cpp)
extern VehicleState state;
extern TripStatistics tripStats;
extern UserConfig userConfig;
extern LookupTables lookupTables;

// ========== Utility Functions ==========
const char *boolWord(bool v) {
    return v ? "true" : "false";
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

void activeModes(char* buf, size_t bufSize, const VehicleState &state) {
    int pos = snprintf(buf, bufSize, "base");
    if (state.brakePercent > 0 && pos < (int)bufSize - 10)
        pos += snprintf(buf + pos, bufSize - pos, ", brake");
    if (state.handbrakePulled > 0 && pos < (int)bufSize - 15)
        pos += snprintf(buf + pos, bufSize - pos, ", handbrake");
    if (state.clutchPercent > 0 && pos < (int)bufSize - 10)
        pos += snprintf(buf + pos, bufSize - pos, ", clutch");
    if (state.revLimiter && pos < (int)bufSize - 15)
        pos += snprintf(buf + pos, bufSize - pos, ", rev_limiter");
    if (state.rpm >= state.rpmRedline && pos < (int)bufSize - 12)
        pos += snprintf(buf + pos, bufSize - pos, ", redline");
    if (state.alsActive && pos < (int)bufSize - 7)
        pos += snprintf(buf + pos, bufSize - pos, ", als");
    if (isWarmingUp(state) && pos < (int)bufSize - 14)
        pos += snprintf(buf + pos, bufSize - pos, ", warming_up");
    if (isPanicError(state) && pos < (int)bufSize - 13)
        pos += snprintf(buf + pos, bufSize - pos, ", panic_oil");
}

// ========== Configuration Management ==========
void loadConfig(UserConfig &config, VehicleState &state, LookupTables &tables) {
    preferences.begin("canled", false);

    config.rpmRedline = preferences.getUShort("rpmRedline", 6500);
    config.shiftLightRpm = preferences.getUShort("shiftRpm", config.rpmRedline * 95 / 100);
    config.ledBrightness = preferences.getUChar("brightness", LED_BRIGHTNESS);
    config.nightModeBrightness = preferences.getUChar("nightBright", LED_BRIGHTNESS / 3);
    config.nightModeStartHour = preferences.getUChar("nightStart", 20);
    config.nightModeEndHour = preferences.getUChar("nightEnd", 6);
    config.autoNightMode = preferences.getBool("autoNight", false);
    config.visualMode = preferences.getUChar("visualMode", 0);

    preferences.end();

    // Apply loaded config
    state.rpmRedline = config.rpmRedline;
    tables.init(config.rpmRedline);

    #if ENABLE_DEBUG_SERIAL
    Serial.println("Configuration loaded from NVS");
    Serial.printf("  Redline: %u RPM\n", config.rpmRedline);
    Serial.printf("  Shift light: %u RPM\n", config.shiftLightRpm);
    Serial.printf("  Brightness: %u\n", config.ledBrightness);
    #endif
}

void saveConfig(const UserConfig &config) {
    preferences.begin("canled", false);

    preferences.putUShort("rpmRedline", config.rpmRedline);
    preferences.putUShort("shiftRpm", config.shiftLightRpm);
    preferences.putUChar("brightness", config.ledBrightness);
    preferences.putUChar("nightBright", config.nightModeBrightness);
    preferences.putUChar("nightStart", config.nightModeStartHour);
    preferences.putUChar("nightEnd", config.nightModeEndHour);
    preferences.putBool("autoNight", config.autoNightMode);
    preferences.putUChar("visualMode", config.visualMode);

    preferences.end();

    #if ENABLE_DEBUG_SERIAL
    Serial.println("Configuration saved to NVS");
    #endif
}

// ========== Network Setup ==========
void ensureAccessPoint() {
    if (apStarted) {
        return;
    }

    #if ENABLE_DEBUG_SERIAL
    Serial.println("Starting WiFi Access Point...");
    #endif

    WiFi.mode(WIFI_AP);

    if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
        apStarted = true;

        #if ENABLE_DEBUG_SERIAL
        Serial.print("AP ready, connect to http://");
        Serial.print(WiFi.softAPIP());
        Serial.println('/');
        #endif

        setupServer();
    } else {
        #if ENABLE_DEBUG_SERIAL
        Serial.println("Failed to start AP");
        #endif
    }
}

void setupServer() {
    if (!apStarted || serverStarted) {
        return;
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/stats", HTTP_GET, handleApiStats);
    server.on("/api/stats/reset", HTTP_POST, handleApiResetStats);
    server.on("/api/config", HTTP_GET, handleApiConfig);
    server.on("/api/config", HTTP_POST, handleApiConfig);
    server.on("/api/export/csv", HTTP_GET, handleApiExportCsv);
    server.begin();
    serverStarted = true;

    #if ENABLE_DEBUG_SERIAL
    Serial.println("HTTP server started on port 80");
    Serial.println("API endpoints:");
    Serial.println("  GET  /api/state");
    Serial.println("  GET  /api/stats");
    Serial.println("  POST /api/stats/reset");
    Serial.println("  GET/POST /api/config");
    Serial.println("  GET  /api/export/csv");
    #endif

    #if ENABLE_WEBSOCKET
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    #if ENABLE_DEBUG_SERIAL
    Serial.println("WebSocket server started on port 81");
    #endif
    #endif
}

// ========== OTA Setup ==========
void setupOTA() {
    #if ENABLE_OTA
    ArduinoOTA.setHostname("canled");
    ArduinoOTA.setPassword(WIFI_PASSWORD);

    ArduinoOTA.onStart([]() {
        #if ENABLE_DEBUG_SERIAL
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("OTA: Start updating " + type);
        #endif
        fill_solid(leds, LED_COUNT, CRGB::Blue);
        FastLED.show();
    });

    ArduinoOTA.onEnd([]() {
        #if ENABLE_DEBUG_SERIAL
        Serial.println("\nOTA: Update complete");
        #endif
        fill_solid(leds, LED_COUNT, CRGB::Green);
        FastLED.show();
        delay(1000);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        uint8_t percent = (progress * 100) / total;
        int litCount = (percent * LED_COUNT) / 100;
        fill_solid(leds, litCount, CRGB::Cyan);
        fill_solid(leds + litCount, LED_COUNT - litCount, CRGB::Black);
        FastLED.show();

        #if ENABLE_DEBUG_SERIAL
        if (progress % 10000 == 0) {
            Serial.printf("OTA Progress: %u%%\r", percent);
        }
        #endif
    });

    ArduinoOTA.onError([](ota_error_t error) {
        #if ENABLE_DEBUG_SERIAL
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        #endif

        for (int i = 0; i < 5; i++) {
            fill_solid(leds, LED_COUNT, CRGB::Red);
            FastLED.show();
            delay(200);
            fill_solid(leds, LED_COUNT, CRGB::Black);
            FastLED.show();
            delay(200);
        }
    });

    ArduinoOTA.begin();
    #if ENABLE_DEBUG_SERIAL
    Serial.println("OTA ready");
    #endif
    #endif
}

// ========== WebSocket Implementation ==========
#if ENABLE_WEBSOCKET
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_CONNECTED:
            #if ENABLE_DEBUG_SERIAL
            Serial.printf("WebSocket client #%u connected\n", num);
            #endif
            broadcastWebSocketData();
            break;

        case WStype_DISCONNECTED:
            #if ENABLE_DEBUG_SERIAL
            Serial.printf("WebSocket client #%u disconnected\n", num);
            #endif
            break;

        default:
            break;
    }
}

void broadcastWebSocketData() {
    char buf[512];
    int pos = snprintf(buf, sizeof(buf),
        "{\"rpm\":%u,\"throttle\":%u,\"brake\":%u,\"coolant\":%u.%u,"
        "\"oil_pressure\":%u.%02u,\"rev_limiter\":%s,\"als\":%s,"
        "\"ignition\":%s,\"max_rpm\":%u,\"avg_rpm\":%u}",
        state.rpm, state.throttlePercent, state.brakePercent,
        state.coolant10x / 10, state.coolant10x % 10,
        state.oilPressure10kPa / 10, (state.oilPressure10kPa % 10) * 10,
        boolWord(state.revLimiter), boolWord(state.alsActive),
        boolWord(state.ignitionOn), tripStats.maxRpm, tripStats.getAverageRpm()
    );

    if (pos > 0 && pos < (int)sizeof(buf)) {
        webSocket.broadcastTXT(buf, pos);
    }
}
#endif

// ========== HTTP Handlers ==========
void handleRoot() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    WiFiClient client = server.client();

    client.print(F("<!doctype html><html><head><meta charset=\"utf-8\"/>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>"
        "<title>CAN LED Telemetry</title><style>"
        "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5;}"
        "h1{color:#333;margin-bottom:10px;}h2{color:#555;margin-top:20px;}"
        ".card{background:white;border-radius:8px;padding:15px;margin:10px 0;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
        ".metric{display:inline-block;margin:10px 20px 10px 0;}"
        ".metric-label{font-size:12px;color:#666;}"
        ".metric-value{font-size:28px;font-weight:bold;color:#333;}"
        "button{background:#007bff;color:white;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;margin:5px;}"
        "button:hover{background:#0056b3;}"
        "</style></head><body>"
        "<h1>CAN LED Telemetry</h1>"));

    client.print(F("<div class='card'><h2>Current State</h2>"));
    client.print(F("<div class='metric'><div class='metric-label'>RPM</div><div class='metric-value'>"));
    client.print(state.rpm);
    client.print(F("</div></div>"));

    client.print(F("<div class='metric'><div class='metric-label'>Throttle</div><div class='metric-value'>"));
    client.print(state.throttlePercent);
    client.print(F("%</div></div>"));

    client.print(F("<div class='metric'><div class='metric-label'>Coolant</div><div class='metric-value'>"));
    client.print(formatTenths(state.coolant10x));
    client.print(F("°C</div></div>"));

    client.print(F("<div class='metric'><div class='metric-label'>Oil Pressure</div><div class='metric-value'>"));
    client.print(formatHundredths(state.oilPressure10kPa * 10));
    client.print(F(" bar</div></div></div>"));

    client.print(F("<div class='card'><h2>Trip Statistics</h2>"));
    client.print(F("<div class='metric'><div class='metric-label'>Max RPM</div><div class='metric-value'>"));
    client.print(tripStats.maxRpm);
    client.print(F("</div></div>"));

    client.print(F("<div class='metric'><div class='metric-label'>Avg RPM</div><div class='metric-value'>"));
    client.print(tripStats.getAverageRpm());
    client.print(F("</div></div>"));

    client.print(F("<div class='metric'><div class='metric-label'>Rev Limiter Hits</div><div class='metric-value'>"));
    client.print(tripStats.revLimiterHits);
    client.print(F("</div></div>"));

    client.print(F("<div><button onclick='fetch(\"/api/stats/reset\",{method:\"POST\"}).then(()=>location.reload())'>Reset Stats</button></div></div>"));

    client.print(F("</body></html>"));
    client.stop();
}

void handleApiState() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();

    client.print("{");
    client.print("\"rpm\":");
    client.print(state.rpm);
    client.print(",\"throttle\":");
    client.print(state.throttlePercent);
    client.print(",\"brake\":");
    client.print(state.brakePercent);
    client.print(",\"coolant_c\":\"");
    client.print(formatTenths(state.coolant10x));
    client.print("\",\"oil_pressure_bar\":\"");
    client.print(formatHundredths(state.oilPressure10kPa * 10));
    client.print("\",\"rev_limiter\":");
    client.print(boolWord(state.revLimiter));
    client.print(",\"als_active\":");
    client.print(boolWord(state.alsActive));
    client.print(",\"ignition_on\":");
    client.print(boolWord(state.ignitionOn));
    client.print("}");
    client.stop();
}

void handleApiStats() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();

    client.print("{");
    client.print("\"max_rpm\":");
    client.print(tripStats.maxRpm);
    client.print(",\"avg_rpm\":");
    client.print(tripStats.getAverageRpm());
    client.print(",\"max_coolant_temp\":");
    client.print(tripStats.maxCoolantTemp);
    client.print(",\"min_oil_pressure\":");
    client.print(tripStats.minOilPressure);
    client.print(",\"rev_limiter_hits\":");
    client.print(tripStats.revLimiterHits);
    client.print(",\"hard_braking_events\":");
    client.print(tripStats.hardBrakingEvents);
    client.print(",\"running_time_sec\":");
    client.print(tripStats.totalRunningTime / 1000);
    client.print(",\"uptime_sec\":");
    client.print(millis() / 1000);
    client.print("}");
    client.stop();
}

void handleApiResetStats() {
    tripStats.reset();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Trip statistics reset\"}");
}

void handleApiConfig() {
    if (server.method() == HTTP_GET) {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        WiFiClient client = server.client();

        client.print("{");
        client.print("\"rpm_redline\":");
        client.print(userConfig.rpmRedline);
        client.print(",\"shift_light_rpm\":");
        client.print(userConfig.shiftLightRpm);
        client.print(",\"led_brightness\":");
        client.print(userConfig.ledBrightness);
        client.print(",\"auto_night_mode\":");
        client.print(boolWord(userConfig.autoNightMode));
        client.print("}");
        client.stop();
    } else if (server.method() == HTTP_POST) {
        bool updated = false;

        if (server.hasArg("redline")) {
            uint16_t newRedline = server.arg("redline").toInt();
            if (newRedline >= 1000 && newRedline <= 12000) {
                userConfig.rpmRedline = newRedline;
                state.rpmRedline = newRedline;
                lookupTables.init(newRedline);
                updated = true;
            }
        }

        if (server.hasArg("brightness")) {
            uint8_t newBright = server.arg("brightness").toInt();
            if (newBright >= 10 && newBright <= 255) {
                userConfig.ledBrightness = newBright;
                FastLED.setBrightness(newBright);
                updated = true;
            }
        }

        if (updated) {
            saveConfig(userConfig);
            server.send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            server.send(400, "application/json", "{\"status\":\"error\"}");
        }
    }
}

void handleApiExportCsv() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Disposition", "attachment; filename=canled_stats.csv");
    server.send(200, "text/csv", "");
    WiFiClient client = server.client();

    client.println("timestamp,rpm,throttle,brake,coolant_c,oil_pressure_bar");
    client.print(millis());
    client.print(',');
    client.print(state.rpm);
    client.print(',');
    client.print(state.throttlePercent);
    client.print(',');
    client.print(state.brakePercent);
    client.print(',');
    client.print(state.coolant10x / 10.0);
    client.print(',');
    client.print(state.oilPressure10kPa / 10.0);
    client.println();

    client.println();
    client.println("# Trip Statistics");
    client.print("# Max RPM:,");
    client.println(tripStats.maxRpm);
    client.print("# Avg RPM:,");
    client.println(tripStats.getAverageRpm());

    client.stop();
}
