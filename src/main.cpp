#include <Arduino.h>
#include <FastLED.h>
#include "driver/twai.h"

// ----------- User configuration -----------
static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_21;  // Check silk screen, adjust if needed
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_22;  // Check silk screen, adjust if needed
static constexpr int LED_PIN = 4;                      // Data pin for the addressable strip
static constexpr int LED_COUNT = 60;                   // Number of LEDs on the strip
static constexpr int LED_BRIGHTNESS = 180;             // 0-255
static constexpr uint32_t CAN_BITRATE = 1'000'000;     // 1 Mbps

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

    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);

    configureCan();
}

void loop() {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
        processFrame(message);
    }

    // Compose the LED pattern for the current state
    drawThrottleBar();
    drawRpmGradient();
    drawCoolantIndicator();
    applyBrakeOverlays();
    drawRevLimiter();

    FastLED.show();
}

