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
constexpr uint32_t ID_THROTTLE = 0x100;
constexpr uint32_t ID_PEDALS = 0x101;
constexpr uint32_t ID_RPM = 0x102;
constexpr uint32_t ID_COOLANT = 0x103;
constexpr uint32_t ID_OIL_PRESSURE = 0x104;
constexpr uint32_t ID_FLAGS = 0x105;
constexpr uint32_t ID_IGNITION = 0x106;

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
