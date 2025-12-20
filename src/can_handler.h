#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <Arduino.h>
#include "driver/twai.h"
#include "config.h"
#include "types.h"

// ========== CAN Frame Logging ==========
struct FrameLogEntry {
    uint32_t timestamp;
    twai_message_t message;
};

extern FrameLogEntry frameLog[FRAME_LOG_SIZE];
extern size_t frameLogIndex;

// ========== CAN Status Tracking ==========
extern CanStatus canBusStatus;
extern String canErrorMessage;
extern uint32_t lastCanMessageTime;
extern uint32_t lastCanHealthCheck;
extern uint32_t canRestartAttemptTime;

// ========== CAN Initialization ==========
void configureCan();
void startCan();

// ========== CAN Message Processing ==========
void processFrame(const twai_message_t &msg, VehicleState &state);
bool receiveAndProcessCan(VehicleState &state, uint8_t maxMessages = MAX_MESSAGES_PER_LOOP);

// ========== CAN Health Monitoring ==========
void monitorCanHealth();
void attemptCanRecovery();

// ========== Frame Formatting ==========
void byteToHex(uint8_t b, char* out);
void formatFrame(const twai_message_t &msg, char* buf, size_t bufSize);

// ========== Demo Mode ==========
#if ENABLE_DEMO_MODE
void simulateDemoData(VehicleState &state);
#endif

#endif // CAN_HANDLER_H
