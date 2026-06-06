#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "variables.hpp"
#include "comms.hpp"

HelmetData    helmetData;
bool          isHelmetWorn = false;
unsigned long lastSendMs   = 0;

static void sendHelmetData() {
    esp_err_t res = esp_now_send(receiverMacAddress,
                                 (uint8_t *)&helmetData,
                                 sizeof(helmetData));
    lastSendMs = millis();
    if (res != ESP_OK) Serial.printf("esp_now_send error: %d\n", res);
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
    esp_now_register_send_cb(OnDataSent);

    helmetData.helmetType = HelmetTypes::Passenger;
    helmetData.helmetOn   = false;
    helmetData.isSober    = false;   // unused for passenger

    pinMode(FSR_PIN, INPUT);

    int raw = analogRead(FSR_PIN);
    isHelmetWorn        = (raw > WEAR_THRESHOLD);
    helmetData.helmetOn = isHelmetWorn;
    Serial.printf("Initial FSR raw: %d -> helmet %s\n",
                  raw, isHelmetWorn ? "ON" : "OFF");

    sendHelmetData();
    Serial.println("Passenger helmet ready.");
}

void loop() {
    int  raw = analogRead(FSR_PIN);
    bool nowWorn = isHelmetWorn
        ? raw > (WEAR_THRESHOLD - HYSTERESIS)
        : raw > (WEAR_THRESHOLD + HYSTERESIS);

    if (nowWorn != isHelmetWorn) {
        isHelmetWorn        = nowWorn;
        helmetData.helmetOn = nowWorn;
        Serial.printf("STATUS: Helmet %s (raw=%d)\n",
                      nowWorn ? "RE-WORN" : "REMOVED", raw);
        sendHelmetData();
    }
    else if (millis() - lastSendMs >= HEARTBEAT_MS) {
        sendHelmetData();
    }

    delay(100);
}