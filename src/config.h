#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ========== Feature Flags ==========
#define ENABLE_DEBUG_SERIAL true
#define ENABLE_OTA true
#define ENABLE_WATCHDOG true
#define ENABLE_SHIFT_LIGHT true
#define ENABLE_WEBSOCKET true
#define ENABLE_DEMO_MODE false
#define ENABLE_CAN_FILTER true

// ========== CAN Protocol Selection ==========
// Choose ONE protocol to use:
// 0 = Custom protocol (original)
// 1 = Link ECU Generic Dashboard (Fury X compatible)
// 2 = Link ECU Generic Dashboard 2 (newer protocol)
#define CAN_PROTOCOL 1

// ========== Hardware Configuration ==========
constexpr uint8_t CAN_TX_PIN = 21;
constexpr uint8_t CAN_RX_PIN = 22;
constexpr uint8_t LED_PIN = 4;
constexpr uint8_t LED_COUNT = 60;
constexpr uint8_t LED_BRIGHTNESS = 128;

// ========== Network Configuration ==========
constexpr char WIFI_SSID[] = "CANLED_AP";
constexpr char WIFI_PASSWORD[] = "canled123";

// ========== CAN Message IDs ==========
// Custom Protocol (CAN_PROTOCOL = 0)
constexpr uint32_t ID_THROTTLE = 0x100;
constexpr uint32_t ID_PEDALS = 0x101;
constexpr uint32_t ID_RPM = 0x102;
constexpr uint32_t ID_COOLANT = 0x103;
constexpr uint32_t ID_OIL_PRESSURE = 0x104;
constexpr uint32_t ID_FLAGS = 0x105;
constexpr uint32_t ID_IGNITION = 0x106;

// Link ECU Generic Dashboard (CAN_PROTOCOL = 1)
// Standard CAN IDs for Link ECU Fury X and compatible ECUs
namespace LinkGenericDashboard {
    constexpr uint32_t ID_RPM_TPS = 0x5F0;        // RPM & TPS (bytes 0-3: RPM, bytes 4-5: TPS)
    constexpr uint32_t ID_FUEL_IGN = 0x5F1;       // Fuel pressure & Ignition timing
    constexpr uint32_t ID_PRESSURES = 0x5F2;      // MAP, Baro, Lambda
    constexpr uint32_t ID_TEMPERATURES = 0x5F3;   // Coolant, Air temp
    constexpr uint32_t ID_VOLTAGE_FLAGS = 0x5F4;  // Battery voltage, Flags
    constexpr uint32_t ID_GEAR_OIL = 0x5F5;       // Gear position, Oil pressure
    constexpr uint32_t ID_VEHICLE_SPEED = 0x5F6;  // Vehicle speed, Wheel speeds
    constexpr uint32_t ID_THROTTLE_SENSORS = 0x5F7; // Throttle position sensors
}

// Link ECU Generic Dashboard 2 (CAN_PROTOCOL = 2)
// Newer protocol with extended data
namespace LinkGenericDashboard2 {
    constexpr uint32_t ID_ENGINE_DATA_1 = 0x2000; // RPM, TPS, ECT, IAT
    constexpr uint32_t ID_ENGINE_DATA_2 = 0x2001; // MAP, Battery, Fuel Pressure, Oil Pressure
    constexpr uint32_t ID_ENGINE_DATA_3 = 0x2002; // Lambda, Ignition timing, Fuel level
    constexpr uint32_t ID_ENGINE_DATA_4 = 0x2003; // Boost control, Idle control
    constexpr uint32_t ID_VEHICLE_DATA_1 = 0x2004; // Speed, Gear, Launch/Flat shift status
    constexpr uint32_t ID_VEHICLE_DATA_2 = 0x2005; // Wheel speeds
    constexpr uint32_t ID_FLAGS_WARNINGS = 0x2006; // Engine protection flags, warnings
    constexpr uint32_t ID_ANALOG_INPUTS = 0x2007; // User-configurable analog inputs
}

// ========== Timing Configuration ==========
constexpr uint32_t CAN_TIMEOUT_MS = 10;
constexpr uint32_t WS_BROADCAST_INTERVAL = 100; // 10Hz
constexpr uint32_t BRIGHTNESS_UPDATE_INTERVAL = 60000; // 60 seconds
constexpr uint8_t MAX_MESSAGES_PER_LOOP = 5;
constexpr uint32_t WATCHDOG_TIMEOUT_S = 30;

// ========== Frame Logging ==========
constexpr size_t FRAME_LOG_SIZE = 50;

// ========== Demo Mode Configuration ==========
#if ENABLE_DEMO_MODE
constexpr float DEMO_ACCEL_RATE = 2.0f;
constexpr float DEMO_RPM_SMOOTHING = 0.05f;
#endif

// ========== CAN Health Monitoring ==========
constexpr uint32_t CAN_HEALTH_CHECK_INTERVAL = 5000;
constexpr uint32_t CAN_RESTART_COOLDOWN = 10000;

#endif // CONFIG_H
