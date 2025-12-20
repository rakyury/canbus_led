#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// ========== Vehicle State Structure ==========
struct VehicleState {
    // Engine data
    uint16_t rpm = 0;
    uint8_t throttlePercent = 0;
    uint16_t coolant10x = 600;        // Temperature in 0.1°C (e.g., 850 = 85.0°C)
    uint16_t airTemp10x = 250;        // Intake air temp in 0.1°C
    uint16_t oilPressure10kPa = 30;   // Oil pressure in 0.1 bar (e.g., 45 = 4.5 bar)
    uint16_t fuelPressure10kPa = 300; // Fuel pressure in 0.1 bar
    uint16_t batteryVoltage100x = 1400; // Battery voltage in 0.01V (e.g., 1400 = 14.00V)
    int16_t ignitionTiming10x = 150;  // Ignition timing in 0.1° (e.g., 150 = 15.0°)
    uint16_t lambda100x = 100;        // Lambda in 0.01 (e.g., 100 = 1.00)

    // Pedals and controls
    uint8_t brakePercent = 0;
    uint8_t clutchPercent = 0;
    uint8_t handbrakePulled = 0;

    // Vehicle data
    uint16_t vehicleSpeed10x = 0;     // Speed in 0.1 km/h
    uint8_t gear = 0;                 // Current gear (0 = neutral, 1-6 = gears)

    // Status flags
    bool revLimiter = false;
    bool alsActive = false;
    bool ignitionOn = false;
    bool engineRunning = false;
    bool launchControl = false;
    bool flatShift = false;

    // Configuration
    uint16_t rpmRedline = 6500;
};

// ========== Trip Statistics Structure ==========
struct TripStatistics {
    uint16_t maxRpm = 0;
    uint16_t maxCoolantTemp = 0;
    uint16_t minOilPressure = 9999;
    uint32_t revLimiterHits = 0;
    uint32_t tripStartTime = 0;
    uint32_t totalRunningTime = 0;
    uint32_t hardBrakingEvents = 0;
    uint64_t rpmSum = 0;
    uint32_t rpmSamples = 0;

    uint16_t getAverageRpm() const {
        return rpmSamples > 0 ? (rpmSum / rpmSamples) : 0;
    }

    void reset() {
        maxRpm = 0;
        maxCoolantTemp = 0;
        minOilPressure = 9999;
        revLimiterHits = 0;
        tripStartTime = millis();
        totalRunningTime = 0;
        hardBrakingEvents = 0;
        rpmSum = 0;
        rpmSamples = 0;
    }

    void update(const VehicleState& state) {
        if (state.rpm > maxRpm) maxRpm = state.rpm;

        uint16_t coolantTemp = state.coolant10x / 10;
        if (coolantTemp > maxCoolantTemp) maxCoolantTemp = coolantTemp;

        if (state.oilPressure10kPa < minOilPressure) minOilPressure = state.oilPressure10kPa;

        if (state.revLimiter) revLimiterHits++;

        if (state.brakePercent > 80) hardBrakingEvents++;

        if (state.engineRunning) {
            rpmSum += state.rpm;
            rpmSamples++;
        }
    }
};

// ========== User Configuration Structure ==========
struct UserConfig {
    uint16_t rpmRedline = 6500;
    uint16_t shiftLightRpm = 6175;
    uint8_t ledBrightness = 128;
    uint8_t nightModeBrightness = 42;
    uint8_t nightModeStartHour = 20;
    uint8_t nightModeEndHour = 6;
    bool autoNightMode = false;
    uint8_t visualMode = 0;
};

// ========== Lookup Tables for Performance ==========
struct LookupTables {
    uint8_t throttleToLedCount[101];
    uint8_t rpmToLedCount[101];

    void init(uint16_t redline) {
        for (int i = 0; i <= 100; ++i) {
            throttleToLedCount[i] = (i * LED_COUNT) / 100;
            rpmToLedCount[i] = (i * LED_COUNT) / 100;
        }
    }
};

// ========== CAN Status Enum ==========
enum class CanStatus {
    STOPPED,
    RUNNING,
    BUS_OFF,
    RECOVERING,
    FAILED
};

#endif // TYPES_H
