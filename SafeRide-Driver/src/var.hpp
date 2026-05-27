#pragma once

#include <esp_now.h>
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ====== Pin map ======
#define MICS_EN 18   // EN pin on the MICS-5524 breakout (was MQ3_SWITCH)

// ====== Motor ESP MAC ======
const uint8_t receiverMacAddress[] = {0x14, 0x33, 0x5C, 0x04, 0x20, 0x70};

// ====== Wire packet — MUST match the motor's DriverData byte-for-byte ======
enum HelmetTypes
{
    Driver,
    Passenger
};

struct DriverData
{
    HelmetTypes helmetType;
    bool helmetOn;
    bool isSober;
};

// ====== ADS1115 (MICS-5524 on A0, FSR on A1) ======
Adafruit_ADS1115 ads;

// BAC threshold for sobriety (g/dL). PH professional-driver limit is 0.0,
// general 0.05 — keep 0.05 here unless you want a stricter test.
const float BAC_THRESHOLD = 0.05f;

// --- MICS-5524 hardware constants ---
const float V_CC          = 5.0;
const float R_L           = 10.0;    // load resistor on breakout (kOhm)
const float R_DIV1        = 10.0;    // voltage divider top
const float R_DIV2        = 20.0;    // voltage divider bottom
const float R_L_EFF       = (R_L * (R_DIV1 + R_DIV2)) / (R_L + R_DIV1 + R_DIV2);
const float V_RECONSTRUCT = (R_DIV1 + R_DIV2) / R_DIV2;   // 2.0
const float R0            = 742.50;   // ⚠ MUST CALIBRATE in clean air
const int   OVERSAMPLE_COUNT = 16;

// --- BAC estimation (MICS-5524 ethanol curve, 2100:1 partition) ---
inline float estimateBAC(float ratio) {
    float ppm = 1.30f * pow(ratio, -1.695f);
    float mgL = ppm * 0.001883f;
    return mgL * 0.21f;
}

inline float readRs() {
    float v_sum = 0;
    for (int s = 0; s < OVERSAMPLE_COUNT; s++) {
        int16_t raw = ads.readADC_SingleEnded(0);
        v_sum += ads.computeVolts(raw);
        delay(2);
    }
    float v_out = (v_sum / OVERSAMPLE_COUNT) * V_RECONSTRUCT;
    Serial.print("RAW ADS voltage: "); Serial.print(v_out, 3); Serial.println(" V");
    if (v_out < 0.05) v_out = 0.05;
    if (v_out >= V_CC) v_out = V_CC - 0.01;
    return R_L_EFF * ((V_CC - v_out) / v_out);
}