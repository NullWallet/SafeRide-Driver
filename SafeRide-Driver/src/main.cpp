#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "var.hpp"
#include "coms.hpp"

DriverData driverData;

const unsigned long WARMUP_MS = 15UL * 1000UL;
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
  driverData.isSober = false; // default to NOT sober until proven otherwise
  esp_now_register_send_cb(OnDataSent);

  // Power the MQ3 heater
  pinMode(MICS_EN, OUTPUT);
  digitalWrite(MICS_EN, HIGH);

  Wire.begin(21, 22);

  if (!ads.begin())
  {
    Serial.println("Failed to initialize ADS1115! Check wiring.");
    while (1);
  }

  ads.setGain(GAIN_ONE);

  Serial.println("=========================================");
  Serial.println("MQ3 Sensor Warm-up initiated...");

  unsigned long start = millis();
  while (millis() - start < WARMUP_MS)
  {
    unsigned long remaining = (WARMUP_MS - (millis() - start)) / 1000;
    Serial.print("Warm-up: ");
    Serial.print(remaining);
    Serial.println("s remaining...");
    delay(5000);
  }

  Serial.println("=========================================");
  Serial.println("Starting 5-second sobriety test...");

  // --- 5-SECOND BAC AVERAGING (internal only) ---
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

  float avgBac = (sampleCount > 0) ? (bacSum / sampleCount) : 0.0f;
  driverData.isSober = (avgBac < BAC_THRESHOLD);

  Serial.print("Test Complete. Avg BAC (internal): ");
  Serial.print(avgBac, 4);
  Serial.print(" g/dL -> Status: ");
  Serial.println(driverData.isSober ? "SOBER" : "NOT SOBER");

  // --- CHECK INITIAL HELMET STATE ---
  int16_t fsr = ads.readADC_SingleEnded(1);
  float volts = ads.computeVolts(fsr);
  isHelmetWorn = (volts > WEAR_THRESHOLD);
  driverData.helmetOn = isHelmetWorn;

  // --- SEND INITIAL DATA VIA ESP-NOW ---
  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
  if (result == ESP_OK) {
    Serial.println("Sobriety status sent via ESP-NOW successfully.");
  }

  // --- TURN OFF MQ3 HEATER TO SAVE BATTERY ---
  digitalWrite(MICS_EN, LOW);
  Serial.println("MQ3 Heater turned OFF.");
  Serial.println("=========================================");
}

void loop()
{
  int16_t fsr = ads.readADC_SingleEnded(1);
  float volts = ads.computeVolts(fsr);

  if (volts < (WEAR_THRESHOLD - HYSTERESIS))
  {
    // --- HELMET REMOVED ---
    if (isHelmetWorn)
    {
      isHelmetWorn = false;
      driverData.helmetOn = false;
      Serial.println("STATUS: Helmet REMOVED");

      esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
      delay(150);
    }

    Serial.println("Entering True Deep Sleep. Goodnight!");
    Serial.flush();
    esp_deep_sleep_start();
  }
  else
  {
    // --- HELMET STILL WORN ---
    isHelmetWorn = true;
    Serial.flush();
    esp_sleep_enable_timer_wakeup(1000000ULL);
    esp_light_sleep_start();
  }
}