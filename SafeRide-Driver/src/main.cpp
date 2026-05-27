#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "var.hpp"
#include "coms.hpp"

DriverData driverData;

// --- Timing / thresholds ---
const unsigned long WARMUP_MS              = 15UL * 1000UL;        // MICS-5524 heater warm-up
const float         WEAR_THRESHOLD         = 2.50f;                // volts (FSR on ADS A1)
const float         HYSTERESIS             = 0.20f;
const unsigned long HELMET_OFF_DEEPSLEEP_MS = 5UL * 60UL * 1000UL;  // 5 min off → deep sleep
const unsigned long HEARTBEAT_MS           = 5UL * 1000UL;          // re-send state every 5s
const unsigned long SAMPLE_INTERVAL_MS     = 1000UL;                // FSR poll cadence

bool          isHelmetWorn   = false;
unsigned long helmetOffSince = 0;   // millis() when helmet went off; 0 means currently on
unsigned long lastSendMs     = 0;

static void sendDriverData() {
    esp_err_t res = esp_now_send(receiverMacAddress, (uint8_t *)&driverData, sizeof(driverData));
    lastSendMs = millis();
    if (res != ESP_OK) {
        Serial.printf("esp_now_send error: %d\n", res);
    }
}

void setup()
{
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

    driverData.helmetType = HelmetTypes::Driver;
    driverData.helmetOn   = false;
    driverData.isSober    = false;   // fail-safe default

    // MICS-5524 heater on for warm-up + test
    pinMode(MICS_EN, OUTPUT);
    digitalWrite(MICS_EN, HIGH);

    Wire.begin(21, 22);
    if (!ads.begin()) {
        Serial.println("Failed to initialize ADS1115! Check wiring.");
        while (1);
    }
    ads.setGain(GAIN_ONE);

    Serial.println("=========================================");
    Serial.println("MICS-5524 warm-up initiated...");
    unsigned long start = millis();
    while (millis() - start < WARMUP_MS) {
        Serial.printf("Warm-up: %lus remaining...\n",
                      (WARMUP_MS - (millis() - start)) / 1000);
        delay(5000);
    }

    Serial.println("=========================================");
    Serial.println("Starting 5-second sobriety test...");
    unsigned long testStart = millis();
    float bacSum = 0.0f;
    int   sampleCount = 0;
    while (millis() - testStart < 5000) {
        float rs    = readRs();
        float ratio = rs / R0;
        bacSum += estimateBAC(ratio);
        sampleCount++;
        delay(100);
    }
    float avgBac = (sampleCount > 0) ? (bacSum / sampleCount) : 0.0f;
    driverData.isSober = (avgBac < BAC_THRESHOLD);
    Serial.printf("Test complete. Avg BAC (internal): %.4f g/dL -> %s\n",
                  avgBac, driverData.isSober ? "SOBER" : "NOT SOBER");

    // Initial helmet state
    int16_t fsrRaw = ads.readADC_SingleEnded(1);
    float   volts  = ads.computeVolts(fsrRaw);
    isHelmetWorn   = (volts > WEAR_THRESHOLD);
    driverData.helmetOn = isHelmetWorn;
    helmetOffSince = isHelmetWorn ? 0 : millis();

    sendDriverData();
    Serial.println("Initial state sent to motor.");

    // Turn off heater to save battery
    digitalWrite(MICS_EN, LOW);
    Serial.println("MICS-5524 heater OFF.");
    Serial.println("=========================================");
}

void loop()
{
    // --- Sample FSR with hysteresis (avoid chatter near threshold) ---
    int16_t fsrRaw = ads.readADC_SingleEnded(1);
    float   volts  = ads.computeVolts(fsrRaw);

    bool nowWorn;
    if (isHelmetWorn) {
        // currently worn → only consider it OFF if we drop clearly below threshold
        nowWorn = volts > (WEAR_THRESHOLD - HYSTERESIS);
    } else {
        // currently off → only consider it ON if we rise clearly above threshold
        nowWorn = volts > (WEAR_THRESHOLD + HYSTERESIS);
    }

    // --- On state change: notify motor immediately ---
    if (nowWorn != isHelmetWorn) {
        isHelmetWorn        = nowWorn;
        driverData.helmetOn = nowWorn;
        if (nowWorn) {
            Serial.println("STATUS: Helmet RE-WORN");
            helmetOffSince = 0;
        } else {
            Serial.println("STATUS: Helmet REMOVED");
            helmetOffSince = millis();
        }
        sendDriverData();
    }
    // --- Heartbeat: keep motor in sync even if a packet was lost ---
    else if (millis() - lastSendMs >= HEARTBEAT_MS) {
        sendDriverData();
    }

    // --- 5-min off timer → true deep sleep (requires power cycle / reset to wake) ---
    if (!isHelmetWorn && helmetOffSince != 0
        && (millis() - helmetOffSince) >= HELMET_OFF_DEEPSLEEP_MS) {
        Serial.println("Helmet has been off for 5 minutes. Entering DEEP SLEEP.");
        Serial.println("Power-cycle the helmet to wake and re-run the sobriety test.");
        Serial.flush();
        // (FSR is behind the ADS1115 so we can't EXT0-wake from it directly.
        //  If you wire a parallel FSR signal to a wakeup-capable GPIO later,
        //  call esp_sleep_enable_ext0_wakeup(...) here.)
        esp_deep_sleep_start();
    }

    // --- Light sleep between samples for battery; ESP-NOW state survives ---
    delay(50);                              // let radio drain the last send
    Serial.flush();
    esp_sleep_enable_timer_wakeup(SAMPLE_INTERVAL_MS * 1000ULL);
    esp_light_sleep_start();
}