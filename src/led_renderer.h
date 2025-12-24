#ifndef LED_RENDERER_H
#define LED_RENDERER_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "types.h"

// ========== LED Array ==========
extern CRGB leds[LED_COUNT];
extern bool ledStripInitialized;

// ========== LED Initialization ==========
void setupLeds();

// ========== Drawing Functions ==========
void drawThrottleBar(const VehicleState &state, const LookupTables &tables);
void drawRpmGradient(const VehicleState &state);
void drawCoolantIndicator(const VehicleState &state);
void applyPedalOverlays(const VehicleState &state);

// ========== Special Effects ==========
void drawRevLimiter(const VehicleState &state);
void drawAlsOverlay(const VehicleState &state);
void drawWarmingOverlay(const VehicleState &state);
void drawPanicError(const VehicleState &state);
void drawIgnitionStandby(const VehicleState &state);

// ========== Advanced Features ==========
void drawShiftLight(const VehicleState &state, const UserConfig &config);
void updateAdaptiveBrightness(const UserConfig &config);

// ========== Error Visualization ==========
void drawCanError(CanStatus status);
void drawStaleDataWarning(bool isStale);

// ========== Utility Functions ==========
void blendSegment(int start, int end, const CRGB &color);
bool isWarmingUp(const VehicleState &state);
bool isPanicError(const VehicleState &state);

// ========== LED Streaming (for emulator) ==========
#if ENABLE_LED_STREAM
void streamLedData();
#endif

#endif // LED_RENDERER_H
