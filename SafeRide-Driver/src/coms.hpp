#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include "var.hpp"

// ── Must match motor's MotorData exactly ──────────────────────────────────
enum MotorCommand : uint8_t { CMD_START_TEST = 1 };
struct MotorData  { MotorCommand command; };
struct BacData    { float bac; };

volatile bool testRequested = false;

inline void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.printf("[ESP-NOW] send to %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

inline void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(MotorData)) {
        MotorData incoming;
        memcpy(&incoming, incomingData, sizeof(incoming));
        if (incoming.command == CMD_START_TEST) {
            Serial.println("[ESP-NOW] START_TEST received from motor");
            testRequested = true;
        }
    }
}

inline void sendBacToMotor(float bac) {
    BacData data;
    data.bac = bac;
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&data, sizeof(data));
    Serial.printf("[ESP-NOW] BAC %.4f sent to motor — %s\n", bac,
                  result == ESP_OK ? "OK" : "FAILED");
}