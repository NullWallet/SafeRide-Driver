#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "var.hpp"
#include "coms.hpp"

DriverData driverData;

const unsigned long WARMUP_MS = 15UL * 1000UL; // 15 seconds (MICS-5524 stabilization)
const float WEAR_THRESHOLD = 2.50;
const float HYSTERESIS = 0.20;

bool isHelmetWorn = false;

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

  driverData.helmetType = HelmetTypes::Driver;
  driverData.helmetOn = false;
  driverData.bacLevel = 0.0f;
  esp_now_register_send_cb(OnDataSent);

  // Power the MICS-5524 via 2N2222 low-side switch on GPIO 18
  pinMode(MICS_EN, OUTPUT);
  digitalWrite(MICS_EN, HIGH);

  Wire.begin(21, 22);

  if (!ads.begin())
  {
    Serial.println("Failed to initialize ADS1115! Check wiring.");
    while (1)
      ;
  }

  ads.setGain(GAIN_ONE);

  Serial.println("=========================================");
  Serial.println("MICS-5524 stabilization initiated...");

  unsigned long start = millis();
  while (millis() - start < WARMUP_MS)
  {
    unsigned long remaining = (WARMUP_MS - (millis() - start)) / 1000;
    Serial.print("Stabilizing: ");
    Serial.print(remaining);
    Serial.println("s remaining...");
    delay(2000);
  }

  Serial.println("=========================================");
  Serial.println("Starting 5-second BAC test...");

  // --- 5-SECOND BAC AVERAGING ---
  unsigned long testStart = millis();
  float bacSum = 0.0f;
  int sampleCount = 0;

  while (millis() - testStart < 5000)
  {
    float rs = readRs();
    float ratio = rs / R0;
    float currentBac = estimateBAC(ratio);

    bacSum += currentBac;
    sampleCount++;
    delay(100);
  }

  driverData.bacLevel = bacSum / sampleCount;
  Serial.print("Test Complete. Average BAC: ");
  Serial.println(driverData.bacLevel, 4);

  // --- CHECK INITIAL HELMET STATE ---
  int16_t fsr = ads.readADC_SingleEnded(1);
  float volts = ads.computeVolts(fsr);
  isHelmetWorn = (volts > WEAR_THRESHOLD);
  driverData.helmetOn = isHelmetWorn;

  // --- SEND INITIAL DATA VIA ESP-NOW ---
  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
  if (result == ESP_OK)
  {
    Serial.println("BAC Data sent via ESP-NOW successfully.");
  }

//   // --- TURN OFF MICS-5524 TO SAVE BATTERY ---
//   digitalWrite(MICS_EN, LOW);
//   Serial.println("MICS-5524 powered OFF.");
//   Serial.println("=========================================");
}

void loop() {
    testSensor();
}
// void loop()
// {
//   int16_t fsr = ads.readADC_SingleEnded(1);
//   float volts = ads.computeVolts(fsr);

//   if (volts < (WEAR_THRESHOLD - HYSTERESIS))
//   {
//     // --- HELMET IS REMOVED ---
//     if (isHelmetWorn)
//     {
//       isHelmetWorn = false;
//       driverData.helmetOn = false;
//       Serial.println("STATUS: Helmet REMOVED");

//       // Send the final state to the receiver
//       esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
//       delay(150); // Give the radio time to finish transmitting
//     }

//     // Enter True Deep Sleep. The ESP32 halts here completely.
//     Serial.println("Entering True Deep Sleep. Goodnight!");
//     Serial.flush();
//     esp_deep_sleep_start();
//   }
//   else
//   {
//     // --- HELMET IS STILL WORN ---
//     isHelmetWorn = true;

//     // Enter Light Sleep for 1 second.
//     // Turns off Wi-Fi and CPU to save battery, but retains memory to loop again.
//     Serial.flush();
//     esp_sleep_enable_timer_wakeup(1000000ULL); // 1,000,000 microseconds = 1 second
//     esp_light_sleep_start();
//   }
// }