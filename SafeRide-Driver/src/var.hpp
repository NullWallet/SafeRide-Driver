#pragma once

#include <esp_now.h>
#include <Arduino.h>

#define MICS_EN 18   // EN pin on MICS-5524 breakout (was MQ3_SWITCH)

typedef unsigned char uint8_t;

const uint8_t receiverMacAddress[] = {0x14, 0x33, 0x5C, 0x04, 0x20, 0x70};

enum HelmetTypes
{
    Driver,
    Passenger
};

struct DriverData
{
    HelmetTypes helmetType;
    bool helmetOn;
    float bacLevel;
};

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

// --- Sensor Constants (MICS-5524 on Adafruit breakout #3199) ---
const float V_CC          = 5.0;     // Breakout VCC
const float R_L           = 10.0;    // Onboard load resistor on MICS-5524 breakout (kOhms)
const float R_DIV1        = 10.0;    // Voltage divider top:    AOUT → ADS A0
const float R_DIV2        = 20.0;    // Voltage divider bottom: A0   → GND
const float R_L_EFF       = (R_L * (R_DIV1 + R_DIV2)) / (R_L + R_DIV1 + R_DIV2);
const float V_RECONSTRUCT = (R_DIV1 + R_DIV2) / R_DIV2;   // 2.0
const float R0            = 742.50;   // ⚠ MUST CALIBRATE in clean air. Typical: 100–1500 kOhm
const int   OVERSAMPLE_COUNT = 16;

// --- BAC Estimation for MICS-5524 ---
// Power curve fitted from SGX MICS-5524 datasheet ethanol sensitivity graph:
//   Rs/R0 ≈ 1.17 * ppm^(-0.59)   →   ppm ≈ 1.30 * (Rs/R0)^(-1.695)
// Then ppm → mg/L → BAC g/dL using 2100:1 partition ratio.
float estimateBAC(float ratio) {
    float ppm = 1.30f * pow(ratio, -1.695f);
    float mgL = ppm * 0.001883f;   // ppm ethanol → mg/L (MW 46.07, molar vol 24.465 L)
    return mgL * 0.21f;            // breath mg/L → blood BAC g/dL
}

float readRs() {
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

    // MICS-5524 wiring: VCC → Rs → AOUT → R_L → GND
    // Rs = R_L_EFF * (V_CC - V_out) / V_out
    return R_L_EFF * ((V_CC - v_out) / v_out);
}

void testSensor() {
    float rs    = readRs();
    float ratio = rs / R0;
    float ppm   = 1.30f * pow(ratio, -1.695f);
    float mgL   = ppm * 0.001883f;
    float bac   = mgL * 0.21f;

    Serial.print("Rs: ");       Serial.print(rs, 2);    Serial.print(" kOhms");
    Serial.print(" | Rs/R0: "); Serial.print(ratio, 3);
    Serial.print(" | PPM: ");   Serial.print(ppm, 1);
    Serial.print(" | BAC: ~");  Serial.print(bac, 3);   Serial.println(" g/dL");
    Serial.println(bac > 0.05 ? ">> WARNING: Above legal limit! <<" : "Within legal limit.");
    Serial.println();
}