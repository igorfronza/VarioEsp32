#include <Arduino.h>

#include "battery.h"

static int g_adcPin = 0;
static float g_divider = 2.0f;
static float g_minV = 3.3f;
static float g_maxV = 4.2f;
static float g_voltage = 0.0f;

static float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

void batteryBegin(int adcPin, float divider, float minV, float maxV)
{
    g_adcPin = adcPin;
    g_divider = clampf(divider, 1.0f, 8.0f);
    g_minV = clampf(minV, 2.5f, 4.0f);
    g_maxV = clampf(maxV, g_minV + 0.1f, 4.5f);

    analogReadResolution(12);
    analogSetPinAttenuation(g_adcPin, ADC_11db);
}

void batteryUpdate()
{
    const float adcMax = 4095.0f;
    int raw = analogRead(g_adcPin);
    float vSense = (raw / adcMax) * 3.3f;
    float vBat = vSense * g_divider;

    // Low-pass para reduzir ruido da leitura ADC.
    g_voltage = 0.12f * vBat + 0.88f * g_voltage;
}

float batteryVoltageGet()
{
    return g_voltage;
}

int batteryPercentGet()
{
    float p = (g_voltage - g_minV) / (g_maxV - g_minV);
    p = clampf(p, 0.0f, 1.0f);
    return (int)(p * 100.0f + 0.5f);
}

bool batteryLowGet()
{
    return batteryPercentGet() <= 15;
}
