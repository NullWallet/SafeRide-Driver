#include <WiFi.h>
#include <esp_now.h>
#include "variables.hpp"
#include "comms.hpp"

#define FSR_PIN 3

const int PRESSURE_THRESHOLD = 1000;

PassengerData passengerData;

void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return;
  }

  passengerData.helmetType = Passenger;
  esp_now_register_send_cb(OnDataSent);
}

void loop()
{
  // Read the raw analog voltage value (0 to 4095)
  int rawValue = analogRead(FSR_PIN);

  // Determine if the helmet is actually being worn
  if (rawValue > PRESSURE_THRESHOLD && passengerData.helmetOn == false)
  {
    passengerData.helmetOn = true;
    esp_now_send(receiverMacAddress, (uint8_t *)&passengerData, sizeof(PassengerData));
  }
  else if (rawValue <= PRESSURE_THRESHOLD && passengerData.helmetOn == true)
  {
    passengerData.helmetOn = false;
    esp_now_send(receiverMacAddress, (uint8_t *)&passengerData, sizeof(PassengerData));
  }

  delay(200); // Small delay to avoid flooding the serial monitor
}
