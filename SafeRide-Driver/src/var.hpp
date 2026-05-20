#pragma once

#include <esp_now.h>
#include <Arduino.h>

#define MQ3_SWITCH 18

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

// --- Sensor Constants ---
const float V_CC         = 5.0;    // MQ3 operational voltage
const float R_L          = 10.0;   // On-board load resistor (kOhms)
const float R_DIV1       = 10.0;   // Voltage divider top resistor: AO → A0 (kOhms)
const float R_DIV2       = 10.0;   // Voltage divider bottom resistor: A0 → GND (kOhms)
const float AIR_FACTOR   = 60.0;   // Rs/R0 in clean air (MQ3 datasheet)
const float R_L_EFF      = (R_L * (R_DIV1 + R_DIV2)) / (R_L + R_DIV1 + R_DIV2); // 6.667k
const float V_RECONSTRUCT = (R_DIV1 + R_DIV2) / R_DIV2;                           // 2.0
const float R0           = 1.90;   // Calibrated in clean air (kOhms)
const int   OVERSAMPLE_COUNT = 16;

// --- BAC Estimation ---
// Power curve fitted from MQ3 datasheet sensitivity graph
// Returns estimated BAC in g/dL (e.g. 0.08 = legal limit in most countries)
float estimateBAC(float ratio) {
    float mgL = 0.4097 * pow(ratio, -1.4063);
    return mgL * 0.21;
}

float readRs() {
    float v_sum = 0;
    for (int s = 0; s < OVERSAMPLE_COUNT; s++) {
        int16_t raw = ads.readADC_SingleEnded(0);
        v_sum += ads.computeVolts(raw);
        delay(2);
    }
    float v_out = (v_sum / OVERSAMPLE_COUNT) * V_RECONSTRUCT;
    if (v_out < 0.1)   v_out = 0.1;
    if (v_out >= V_CC) v_out = V_CC - 0.01;

    return R_L_EFF * ((V_CC - v_out) / v_out);
}

void testSensor() {
    float rs   = readRs();
    float ratio = rs / R0;

    // Formula output is in mg/L (MQ3 datasheet curve uses mg/L, not ppm)
    float mgL  = 0.4097 * pow(ratio, -1.4063);

    // Convert mg/L → ppm (ethanol MW=46.07, molar vol at 25°C=24.465L)
    float ppm  = mgL * 531.0;

    // Convert mg/L breath → BAC g/dL blood (2100:1 partition ratio)
    float bac  = mgL * 0.21;

    Serial.print("Rs: ");       Serial.print(rs, 2);    Serial.print(" kOhms");
    Serial.print(" | Rs/R0: "); Serial.print(ratio, 3);
    Serial.print(" | PPM: ");   Serial.print(ppm, 1);
    Serial.print(" | BAC: ~");  Serial.print(bac, 3);   Serial.println(" g/dL");
    Serial.print(bac > 0.05 ? ">> WARNING: Above legal limit! <<" : "Within legal limit.");
    Serial.println("\n");
}