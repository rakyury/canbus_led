#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "config.h"
#include "types.h"
#include "can_handler.h"

// ========== Server Objects ==========
extern WebServer server;
extern bool apStarted;
extern bool serverStarted;

#if ENABLE_WEBSOCKET
extern WebSocketsServer webSocket;
extern uint32_t lastWsBroadcast;
#endif

// ========== Preferences ==========
extern Preferences preferences;

// ========== Configuration Management ==========
void loadConfig(UserConfig &config, VehicleState &state, LookupTables &tables);
void saveConfig(const UserConfig &config);

// ========== Network Setup ==========
void ensureAccessPoint();
void setupServer();

// ========== OTA Updates ==========
void setupOTA();

// ========== HTTP Handlers ==========
void handleRoot();
void handleApiState();
void handleApiStats();
void handleApiResetStats();
void handleApiConfig();
void handleApiExportCsv();

// ========== WebSocket ==========
#if ENABLE_WEBSOCKET
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void broadcastWebSocketData();
#endif

// ========== Utility Functions ==========
const char *boolWord(bool v);
String formatTenths(uint16_t value10);
String formatHundredths(uint16_t value100);
void activeModes(char* buf, size_t bufSize, const VehicleState &state);

#endif // WEB_SERVER_H
