#pragma once

#include <Arduino.h>
#include <esp_now.h>

inline void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.printf("[ESP-NOW] send to %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}