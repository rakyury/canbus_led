#include "can_handler.h"

// ========== Global Variables ==========
FrameLogEntry frameLog[FRAME_LOG_SIZE];
size_t frameLogIndex = 0;

CanStatus canBusStatus = CanStatus::STOPPED;
String canErrorMessage = "";
uint32_t lastCanMessageTime = 0;
uint32_t lastCanHealthCheck = 0;
uint32_t canRestartAttemptTime = 0;

// ========== Utility Functions ==========
void byteToHex(uint8_t b, char* out) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[b >> 4];
    out[1] = hex[b & 0x0F];
}

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

// ========== CAN Configuration ==========
void configureCan() {
    #if ENABLE_DEMO_MODE
    canBusStatus = CanStatus::RUNNING;
    #if ENABLE_DEBUG_SERIAL
    Serial.println("DEMO MODE: CAN bus disabled, using simulated data");
    #endif
    return;
    #endif

    canBusStatus = CanStatus::RUNNING; // Temporarily mark as running during init

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN,
        (gpio_num_t)CAN_RX_PIN,
        TWAI_MODE_NORMAL
    );
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();

    #if ENABLE_CAN_FILTER
    twai_filter_config_t f_config = {
        .acceptance_code = (ID_THROTTLE << 21),
        .acceptance_mask = ~(0x7FF << 21),
        .single_filter = false
    };
    #if ENABLE_DEBUG_SERIAL
    Serial.println("CAN filter enabled (selective message acceptance)");
    #endif
    #else
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    #endif

    esp_err_t result = twai_driver_install(&g_config, &t_config, &f_config);
    if (result == ESP_OK) {
        #if ENABLE_DEBUG_SERIAL
        Serial.println("TWAI driver installed");
        #endif
    } else {
        canBusStatus = CanStatus::FAILED;
        canErrorMessage = "Driver install failed (error " + String(result) + "). Check GPIO pins.";
        #if ENABLE_DEBUG_SERIAL
        Serial.println(canErrorMessage);
        #endif
        return;
    }

    result = twai_start();
    if (result == ESP_OK) {
        #if ENABLE_DEBUG_SERIAL
        Serial.println("CAN bus started at 1 Mbps");
        #endif
        canBusStatus = CanStatus::RUNNING;
        canErrorMessage = "";
    } else {
        canBusStatus = CanStatus::FAILED;
        canErrorMessage = "Failed to start CAN bus (error " + String(result) + "). Check wiring and termination.";
        #if ENABLE_DEBUG_SERIAL
        Serial.println(canErrorMessage);
        #endif
    }
}

// ========== CAN Message Processing ==========
void processFrame(const twai_message_t &msg, VehicleState &state) {
    uint32_t now = millis();
    lastCanMessageTime = now;

    // Log frame for web interface
    frameLog[frameLogIndex].timestamp = now;
    frameLog[frameLogIndex].message = msg;
    frameLogIndex = (frameLogIndex + 1) % FRAME_LOG_SIZE;

    switch (msg.identifier) {
        case ID_THROTTLE:
            if (msg.data_length_code >= 1) {
                state.throttlePercent = constrain(msg.data[0], (uint8_t)0, (uint8_t)100);
            }
            break;

        case ID_PEDALS:
            if (msg.data_length_code >= 1) {
                state.brakePercent = constrain(msg.data[0], (uint8_t)0, (uint8_t)100);
            }
            if (msg.data_length_code >= 2) {
                state.handbrakePulled = constrain(msg.data[1], (uint8_t)0, (uint8_t)100);
            }
            if (msg.data_length_code >= 3) {
                state.clutchPercent = constrain(msg.data[2], (uint8_t)0, (uint8_t)100);
            }
            break;

        case ID_RPM:
            if (msg.data_length_code >= 2) {
                uint16_t rpm = msg.data[0] | (msg.data[1] << 8);
                state.rpm = rpm;
                state.engineRunning = (rpm > 300);
            }
            break;

        case ID_COOLANT:
            if (msg.data_length_code >= 2) {
                uint16_t coolant = msg.data[0] | (msg.data[1] << 8);
                state.coolant10x = coolant;
            }
            break;

        case ID_OIL_PRESSURE:
            if (msg.data_length_code >= 2) {
                uint16_t oilPres = msg.data[0] | (msg.data[1] << 8);
                state.oilPressure10kPa = oilPres;
            }
            break;

        case ID_FLAGS:
            if (msg.data_length_code >= 1) {
                state.revLimiter = (msg.data[0] & 0x01) != 0;
                state.alsActive = (msg.data[0] & 0x02) != 0;
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

    #if ENABLE_DEBUG_SERIAL
    char frameBuf[64];
    formatFrame(msg, frameBuf, sizeof(frameBuf));
    Serial.print("CAN: ");
    Serial.println(frameBuf);
    #endif
}

bool receiveAndProcessCan(VehicleState &state, uint8_t maxMessages) {
    #if ENABLE_DEMO_MODE
    simulateDemoData(state);
    return true;
    #endif

    if (canBusStatus != CanStatus::RUNNING) {
        return false;
    }

    twai_message_t msg;
    uint8_t messagesProcessed = 0;
    bool receivedAny = false;

    // First message: block for CAN_TIMEOUT_MS
    esp_err_t result = twai_receive(&msg, pdMS_TO_TICKS(CAN_TIMEOUT_MS));
    if (result == ESP_OK) {
        processFrame(msg, state);
        messagesProcessed++;
        receivedAny = true;
    }

    // Subsequent messages: non-blocking
    while (messagesProcessed < maxMessages) {
        result = twai_receive(&msg, 0);
        if (result == ESP_OK) {
            processFrame(msg, state);
            messagesProcessed++;
            receivedAny = true;
        } else {
            break;
        }
    }

    return receivedAny;
}

// ========== CAN Health Monitoring ==========
void monitorCanHealth() {
    #if ENABLE_DEMO_MODE
    return;
    #endif

    if (canBusStatus != CanStatus::RUNNING) {
        return;
    }

    uint32_t now = millis();
    if (now - lastCanHealthCheck < CAN_HEALTH_CHECK_INTERVAL) {
        return;
    }
    lastCanHealthCheck = now;

    twai_status_info_t status;
    esp_err_t result = twai_get_status_info(&status);
    if (result != ESP_OK) {
        #if ENABLE_DEBUG_SERIAL
        Serial.printf("ERROR: Failed to get CAN status info (error %d)\n", result);
        #endif
        return;
    }

    // Check for bus-off condition
    if (status.state == TWAI_STATE_BUS_OFF) {
        canBusStatus = CanStatus::BUS_OFF;
        canErrorMessage = "CAN bus in BUS-OFF. Check wiring and termination.";
        #if ENABLE_DEBUG_SERIAL
        Serial.println(canErrorMessage);
        #endif
        attemptCanRecovery();
    }
}

void attemptCanRecovery() {
    #if ENABLE_DEMO_MODE
    return;
    #endif

    uint32_t now = millis();
    if (now - canRestartAttemptTime < CAN_RESTART_COOLDOWN) {
        return;
    }
    canRestartAttemptTime = now;

    #if ENABLE_DEBUG_SERIAL
    Serial.println("Attempting CAN bus recovery...");
    #endif

    esp_err_t result = twai_initiate_recovery();
    if (result == ESP_OK) {
        canBusStatus = CanStatus::RECOVERING;
        canErrorMessage = "CAN bus recovery in progress...";
        #if ENABLE_DEBUG_SERIAL
        Serial.println("Recovery initiated successfully");
        #endif
    } else {
        #if ENABLE_DEBUG_SERIAL
        Serial.printf("ERROR: Failed to initiate recovery (error %d)\n", result);
        #endif
    }
}

// ========== Demo Mode ==========
#if ENABLE_DEMO_MODE
void simulateDemoData(VehicleState &state) {
    static float throttleSim = 0;
    static float rpmSim = 1000;
    static bool accelerating = true;
    static uint32_t lastUpdate = 0;
    static float brakeSim = 0;
    static float coolantSim = 600;

    uint32_t now = millis();
    if (now - lastUpdate < 50) {
        return; // Update at 20Hz
    }
    lastUpdate = now;

    // Simulate realistic driving
    if (accelerating) {
        throttleSim += DEMO_ACCEL_RATE;
        if (throttleSim >= 100) {
            throttleSim = 100;
            accelerating = false;
        }
    } else {
        throttleSim -= DEMO_ACCEL_RATE * 1.5f;
        if (throttleSim <= 0) {
            throttleSim = 0;
            accelerating = true;
        }
    }

    // Simulate RPM following throttle
    float targetRpm = 1000 + (throttleSim / 100.0f) * (state.rpmRedline - 1000);
    rpmSim += (targetRpm - rpmSim) * DEMO_RPM_SMOOTHING;

    // Update state
    state.throttlePercent = (uint8_t)throttleSim;
    state.rpm = (uint16_t)rpmSim;
    state.engineRunning = true;
    state.ignitionOn = true;

    // Simulate brake when decelerating
    brakeSim = accelerating ? 0 : (100 - throttleSim);
    state.brakePercent = (uint8_t)brakeSim;

    // Simulate warming engine
    if (coolantSim < 850) {
        coolantSim += 0.5f;
    }
    state.coolant10x = (uint16_t)coolantSim;

    // Simulate oil pressure
    state.oilPressure10kPa = 35 + (state.rpm / 200);

    // Simulate rev limiter
    state.revLimiter = (state.rpm >= state.rpmRedline - 100);
}
#endif

