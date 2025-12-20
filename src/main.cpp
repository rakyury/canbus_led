#include <Arduino.h>
#include <FastLED.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "types.h"
#include "can_handler.h"
#include "led_renderer.h"
#include "web_server.h"

// ========== Global State ==========
VehicleState state;
TripStatistics tripStats;
UserConfig userConfig;
LookupTables lookupTables;

// ========== Timing Variables ==========
uint32_t lastBrightnessUpdate = 0;

// ========== Trip Statistics Update ==========
void updateTripStats() {
    tripStats.update(state);

    // Update running time
    if (state.ignitionOn) {
        tripStats.totalRunningTime = millis() - tripStats.tripStartTime;
    }
}

// ========== Setup Function ==========
void setup() {
    #if ENABLE_DEBUG_SERIAL
    Serial.begin(115200);
    delay(500);
    Serial.println("Booting CAN LED firmware...");
    Serial.printf("AP SSID: '%s'\n", WIFI_SSID);
    #endif

    // Initialize watchdog timer
    #if ENABLE_WATCHDOG
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
    #if ENABLE_DEBUG_SERIAL
    Serial.printf("Watchdog timer enabled (%u seconds)\n", WATCHDOG_TIMEOUT_S);
    #endif
    #endif

    // Load configuration from NVS
    loadConfig(userConfig, state, lookupTables);

    // Initialize trip statistics
    tripStats.reset();

    // Initialize hardware
    setupLeds();
    configureCan();

    // Setup networking
    ensureAccessPoint();
    setupServer();
    setupOTA();

    #if ENABLE_DEBUG_SERIAL
    Serial.println("Setup complete. System ready.");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    #endif
}

// ========== Main Loop ==========
void loop() {
    // Reset watchdog timer
    #if ENABLE_WATCHDOG
    esp_task_wdt_reset();
    #endif

    // Handle OTA updates
    #if ENABLE_OTA
    ArduinoOTA.handle();
    #endif

    // Handle WebSocket
    #if ENABLE_WEBSOCKET
    webSocket.loop();
    if (millis() - lastWsBroadcast > WS_BROADCAST_INTERVAL) {
        broadcastWebSocketData();
        lastWsBroadcast = millis();
    }
    #endif

    // Process CAN messages or simulate demo data
    receiveAndProcessCan(state);

    // Monitor CAN health
    monitorCanHealth();

    // Update trip statistics
    updateTripStats();

    // Update adaptive brightness (every 60 seconds)
    if (millis() - lastBrightnessUpdate > BRIGHTNESS_UPDATE_INTERVAL) {
        updateAdaptiveBrightness(userConfig);
        lastBrightnessUpdate = millis();
    }

    // ========== LED Rendering ==========
    // Clear LED buffer
    fill_solid(leds, LED_COUNT, CRGB::Black);

    // Layer 1: Base visualization (throttle or standby)
    if (state.ignitionOn && state.rpm == 0) {
        drawIgnitionStandby(state);
    } else {
        drawThrottleBar(state, lookupTables);
    }

    // Layer 2: RPM gradient overlay
    drawRpmGradient(state);

    // Layer 3: Coolant temperature indicator
    drawCoolantIndicator(state);

    // Layer 4: Pedal overlays (brake, handbrake, clutch)
    applyPedalOverlays(state);

    // Layer 5: Special effects
    drawRevLimiter(state);
    drawAlsOverlay(state);
    drawWarmingOverlay(state);
    drawShiftLight(state, userConfig);

    // Layer 6: Critical errors (overrides everything)
    drawPanicError(state);
    drawCanError(canBusStatus);

    // Update LED strip
    FastLED.show();

    // Handle HTTP server
    server.handleClient();
}
