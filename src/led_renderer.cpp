#include "led_renderer.h"

// ========== Global Variables ==========
CRGB leds[LED_COUNT];
bool ledStripInitialized = false;

// ========== LED Initialization ==========
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

    #if ENABLE_DEBUG_SERIAL
    Serial.println("LED strip initialized and self-test complete");
    #endif
}

// ========== Utility Functions ==========
void blendSegment(int start, int end, const CRGB &color) {
    start = constrain(start, 0, LED_COUNT);
    end = constrain(end, 0, LED_COUNT);
    for (int i = start; i < end; ++i) {
        leds[i] = blend(leds[i], color, 192);
    }
}

bool isWarmingUp(const VehicleState &state) {
    return state.ignitionOn && state.coolant10x < 600; // Below 60°C
}

bool isPanicError(const VehicleState &state) {
    // Low oil pressure while under high throttle
    return state.throttlePercent > 40 && state.oilPressure10kPa < 20; // Below 2 bar
}

// ========== Base Drawing Functions ==========
void drawThrottleBar(const VehicleState &state, const LookupTables &tables) {
    int lit = tables.throttleToLedCount[state.throttlePercent];
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = (i < lit) ? CRGB::Green : CRGB::Black;
    }
}

void drawRpmGradient(const VehicleState &state) {
    if (state.rpmRedline == 0) return;

    uint32_t lit = (static_cast<uint32_t>(state.rpm) * LED_COUNT + state.rpmRedline / 2) / state.rpmRedline;
    const uint32_t litCap = LED_COUNT + LED_COUNT / 5; // Allow ~20% past redline
    if (lit > litCap) {
        lit = litCap;
    }

    for (uint32_t i = 0; i < lit && i < static_cast<uint32_t>(LED_COUNT); ++i) {
        // Color gradient from blue (low) to yellow (near redline)
        uint8_t ratio = (static_cast<uint32_t>(i) * 255) / LED_COUNT;
        CRGB color = blend(CRGB::Blue, CRGB::Yellow, ratio);
        leds[i] = blend(leds[i], color, 128);
    }

    // Past redline -> flashing red overlay
    if (state.rpm >= state.rpmRedline) {
        uint8_t pulse = beatsin8(5, 64, 255);
        fill_solid(leds, LED_COUNT, CRGB(pulse, 0, 0));
    }
}

void drawCoolantIndicator(const VehicleState &state) {
    // Map 60-110°C to blue->green->red
    uint16_t temp10 = state.coolant10x;
    if (temp10 < 600) temp10 = 600;
    if (temp10 > 1100) temp10 = 1100;

    CRGB color;
    if (temp10 <= 850) {
        // 60-85°C: blue to green
        uint8_t mix = static_cast<uint32_t>(temp10 - 600) * 255 / 250;
        color = blend(CRGB::Blue, CRGB::Green, mix);
    } else {
        // 85-110°C: green to red
        uint8_t mix = static_cast<uint32_t>(temp10 - 850) * 255 / 250;
        color = blend(CRGB::Green, CRGB::Red, mix);
    }

    leds[LED_COUNT - 1] = color; // Last pixel shows coolant temp
}

// ========== Pedal Overlays ==========
void applyPedalOverlays(const VehicleState &state) {
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
    if (state.handbrakePulled > 0) {
        uint8_t intensity = map(state.handbrakePulled, 0, 100, 10, 220);
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
        if (state.handbrakePulled > 0 && i < handbrakeSection) {
            leds[i] = blend(leds[i], hbColor, 192);
        }
        if (state.clutchPercent > 0 && i >= clutchStart) {
            leds[i] = blend(leds[i], clutchColor, 192);
        }
    }
}

// ========== Special Effects ==========
void drawRevLimiter(const VehicleState &state) {
    if (!state.revLimiter) return;

    uint8_t pulse = beatsin8(8, 96, 255);
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], CRGB(pulse, pulse, 0), 192); // Yellow pulsing
    }
}

void drawAlsOverlay(const VehicleState &state) {
    if (!state.alsActive) return;

    uint8_t pulse = beatsin8(12, 80, 200);
    CRGB color = CRGB(pulse, 80, 0); // Amber pulse
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], color, 160);
    }
}

void drawWarmingOverlay(const VehicleState &state) {
    if (!isWarmingUp(state)) return;

    uint8_t pulse = beatsin8(6, 40, 120);
    CRGB color = CRGB(0, 100, pulse + 40); // Teal/blue breathing
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(leds[i], color, 128);
    }
}

void drawPanicError(const VehicleState &state) {
    if (!isPanicError(state)) return;

    uint8_t pulse = beatsin8(18, 180, 255);
    CRGB alert = (millis() / 200) % 2 ? CRGB::Red : CRGB::White;
    for (int i = 0; i < LED_COUNT; ++i) {
        leds[i] = blend(alert, CRGB(pulse, 0, 0), 200);
    }
}

void drawIgnitionStandby(const VehicleState &state) {
    if (!state.ignitionOn || state.rpm != 0) return;

    uint8_t pulse = beatsin8(5, 20, 80);
    CRGB base = CRGB(80, 60, 0);
    base.nscale8_video(pulse + 60);
    fill_solid(leds, LED_COUNT, base);
}

// ========== Advanced Features ==========
void drawShiftLight(const VehicleState &state, const UserConfig &config) {
    #if ENABLE_SHIFT_LIGHT
    if (state.rpm < config.shiftLightRpm) return;

    uint8_t pulse = beatsin8(15, 128, 255); // Fast pulsing
    CRGB shiftColor = CRGB(0, 0, pulse);

    // Flash first and last few LEDs
    static constexpr int SHIFT_LED_COUNT = 5;
    for (int i = 0; i < SHIFT_LED_COUNT; ++i) {
        leds[i] = shiftColor;
        leds[LED_COUNT - 1 - i] = shiftColor;
    }
    #endif
}

void updateAdaptiveBrightness(const UserConfig &config) {
    if (!config.autoNightMode) {
        FastLED.setBrightness(config.ledBrightness);
        return;
    }

    // Simple time-based night mode
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        FastLED.setBrightness(config.ledBrightness);
        return;
    }

    int hour = timeinfo.tm_hour;
    bool isNightTime = false;

    if (config.nightModeStartHour > config.nightModeEndHour) {
        // Night spans midnight (e.g., 20:00 - 06:00)
        isNightTime = (hour >= config.nightModeStartHour || hour < config.nightModeEndHour);
    } else {
        // Normal range
        isNightTime = (hour >= config.nightModeStartHour && hour < config.nightModeEndHour);
    }

    uint8_t targetBrightness = isNightTime ? config.nightModeBrightness : config.ledBrightness;
    FastLED.setBrightness(targetBrightness);
}

// ========== Error Visualization ==========
void drawCanError(CanStatus status) {
    if (status == CanStatus::RUNNING) return;

    // Blink red to indicate CAN error
    uint8_t pulse = beatsin8(4, 0, 255);
    fill_solid(leds, LED_COUNT, CRGB(pulse, 0, 0));
}

void drawStaleDataWarning(bool isStale) {
    if (!isStale) return;

    // Dim yellow blink for stale data
    uint8_t pulse = beatsin8(3, 20, 100);
    CRGB warning = CRGB(pulse, pulse / 2, 0);
    for (int i = 0; i < LED_COUNT; i += 4) {
        leds[i] = warning;
    }
}

// ========== LED Streaming (for emulator) ==========
#if ENABLE_LED_STREAM
static uint32_t lastLedStream = 0;
static constexpr uint32_t LED_STREAM_INTERVAL = 33; // ~30 Hz

void streamLedData() {
    if (millis() - lastLedStream < LED_STREAM_INTERVAL) {
        return;
    }
    lastLedStream = millis();

    // Format: LED:count:RRGGBBRRGGBB... (hex encoded RGB values)
    Serial.print("LED:");
    Serial.print(LED_COUNT);
    Serial.print(":");

    for (int i = 0; i < LED_COUNT; ++i) {
        char hex[7];
        snprintf(hex, sizeof(hex), "%02X%02X%02X", leds[i].r, leds[i].g, leds[i].b);
        Serial.print(hex);
    }
    Serial.println();
}
#endif
