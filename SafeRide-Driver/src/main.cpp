#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "var.hpp"
#include "coms.hpp"

DriverData driverData;

const unsigned long WARMUP_MS = 15UL * 1000UL;
const float WEAR_THRESHOLD = 2.50f;
const float HYSTERESIS = 0.20f;
const unsigned long HELMET_OFF_DEEPSLEEP_MS = 5UL * 60UL * 1000UL;
const unsigned long HEARTBEAT_MS = 5UL * 1000UL;

bool isHelmetWorn = false;
unsigned long helmetOffSince = 0;
unsigned long lastSendMs = 0;

static void sendDriverData()
{
    esp_err_t res = esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
    lastSendMs = millis();
    if (res != ESP_OK)
        Serial.printf("esp_now_send error: %d\n", res);
}

// ─── Alcohol test ─────────────────────────────────────────────────────────
// Blocking: 15s warmup + 5s sampling.
// Only runs when testRequested is set by the motor (app triggered).
void runAlcoholTest()
{
    Serial.println("=========================================");
    Serial.println("MICS-5524 heater ON — warming up...");
    digitalWrite(MICS_EN, HIGH);

    unsigned long start = millis();
    while (millis() - start < WARMUP_MS)
    {
        Serial.printf("  Warm-up: %lus remaining...\n",
                      (WARMUP_MS - (millis() - start)) / 1000);
        delay(5000);
    }

    Serial.println("Starting 5-second sobriety test — blow now!");
    unsigned long testStart = millis();
    float bacSum = 0.0f;
    int sampleCount = 0;

    while (millis() - testStart < 5000)
    {
        float rs = readRs();
        float ratio = rs / R0;
        bacSum += estimateBAC(ratio);
        sampleCount++;
        delay(100);
    }

    float avgBac = (sampleCount > 0) ? (bacSum / sampleCount) : 0.0f;
    driverData.isSober = (avgBac < BAC_THRESHOLD);

    Serial.printf("Test complete. Avg BAC: %.4f g/dL -> %s\n",
                  avgBac, driverData.isSober ? "SOBER" : "NOT SOBER");

    // Send sobriety state for ignition control
    sendDriverData();

    // Send dedicated test result so motor can notify the app
    sendTestResultToMotor(driverData.isSober);

    Serial.println("MICS-5524 heater OFF.");
    Serial.println("=========================================");
}

// Replace the direct FSR read in loop() with this helper
static bool readFSR(Adafruit_ADS1115 &ads, float &volts)
{
    int16_t raw = ads.readADC_SingleEnded(1);

    // Wire.cpp error 263 means I2C timed out — check if bus is stuck
    if (raw == 0 || raw == -1)
    {
        Serial.println("[I2C] ADS1115 read failed — attempting bus recovery");
        Wire.end();
        delay(10);
        Wire.begin(21, 22);
        if (!ads.begin())
        {
            Serial.println("[I2C] ADS1115 recovery failed — skipping read");
            return false;
        }
        Serial.println("[I2C] ADS1115 recovered");
        raw = ads.readADC_SingleEnded(1);
    }

    volts = ads.computeVolts(raw);
    return true;
}

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

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    driverData.helmetType = HelmetTypes::Driver;
    driverData.helmetOn = false;
    driverData.isSober = false; // ignition stays locked until app runs a test

    pinMode(MICS_EN, OUTPUT);
    digitalWrite(MICS_EN, HIGH);

    Wire.begin(21, 22);
    if (!ads.begin())
    {
        Serial.println("Failed to initialize ADS1115!");
        while (1)
            ;
    }
    ads.setGain(GAIN_ONE);

    int16_t fsrRaw = ads.readADC_SingleEnded(1);
    float volts = ads.computeVolts(fsrRaw);
    isHelmetWorn = (volts > WEAR_THRESHOLD);
    driverData.helmetOn = isHelmetWorn;
    helmetOffSince = isHelmetWorn ? 0 : millis();

    // Tell motor we're online (isSober=false keeps ignition locked until test)
    sendDriverData();
    Serial.println("Driver helmet ready. Waiting for app to trigger test.");
}

void loop()
{
    // ── On-demand test triggered by motor (app pressed Begin Test) ────────
    if (testRequested)
    {
        testRequested = false;
        runAlcoholTest();
    }

    // ── FSR helmet detection with hysteresis ─────────────────────────────
    float volts = 0.0f;
    if (!readFSR(ads, volts))
    {
        // Bus recovery failed this tick — skip state update, try next loop
        delay(100);
        return;
    }

    bool nowWorn;
    if (isHelmetWorn)
    {
        nowWorn = volts > (WEAR_THRESHOLD - HYSTERESIS);
    }
    else
    {
        nowWorn = volts > (WEAR_THRESHOLD + HYSTERESIS);
    }

    if (nowWorn != isHelmetWorn)
    {
        isHelmetWorn = nowWorn;
        driverData.helmetOn = nowWorn;
        if (nowWorn)
        {
            Serial.println("STATUS: Helmet RE-WORN");
            helmetOffSince = 0;
        }
        else
        {
            Serial.println("STATUS: Helmet REMOVED");
            helmetOffSince = millis();
        }
        sendDriverData();
    }
    else if (millis() - lastSendMs >= HEARTBEAT_MS)
    {
        sendDriverData();
    }

    // ── 5-min off → deep sleep ────────────────────────────────────────────
    if (!isHelmetWorn && helmetOffSince != 0 && (millis() - helmetOffSince) >= HELMET_OFF_DEEPSLEEP_MS)
    {
        Serial.println("Helmet off 5 min — entering deep sleep.");
        Serial.flush();
        esp_deep_sleep_start();
    }

    delay(50);
    Serial.flush();
    delay(200);
}