#include <Arduino.h>

#include "battery.h"

static int g_adcPin = 0;
static float g_divider = 2.0f;
static float g_minV = 3.3f;
static float g_maxV = 4.2f;
static float g_voltage = 0.0f;
static int g_raw = 0;
static bool g_hasReading = false;
static float g_percentSmoothed = 0.0f;
static int g_percentDisplay = 0;
static bool g_hasPercent = false;
static unsigned long g_lastPercentStepMs = 0;

static const float VOLTAGE_ALPHA = 0.04f;
static const float PERCENT_ALPHA = 0.03f;
static const float PERCENT_HYSTERESIS = 2.0f;
static const unsigned long PERCENT_STEP_INTERVAL_MS = 2500;

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
    g_hasReading = false;

    const int candidates[] = {0, 1, 3, 4};
    Serial.printf("BAT cfg pin=%d divider=%.2f min=%.2f max=%.2f\n", g_adcPin, g_divider, g_minV, g_maxV);
    for (int pin : candidates)
        Serial.printf("BAT probe GPIO%d raw=%d\n", pin, analogRead(pin));
}

void batteryUpdate()
{
    const float adcMax = 4095.0f;
    const int sampleCount = 8;
    long sum = 0;
    for (int i = 0; i < sampleCount; ++i)
        sum += analogRead(g_adcPin);

    int raw = (int)(sum / sampleCount);
    g_raw = raw;
    float vSense = (raw / adcMax) * 3.3f;
    float vBat = vSense * g_divider;

    if (!g_hasReading)
    {
        g_voltage = vBat;
        g_hasReading = true;
    }
    else
    {
        // Filtro lento para estabilizar leitura em placa com ruido de ADC.
        g_voltage = VOLTAGE_ALPHA * vBat + (1.0f - VOLTAGE_ALPHA) * g_voltage;
    }

    float p = (g_voltage - g_minV) / (g_maxV - g_minV);
    p = clampf(p, 0.0f, 1.0f) * 100.0f;

    // On USB power many boards keep battery rail near charge voltage,
    // so reported percentage can stay close to 100% with little variation.

    if (!g_hasPercent)
    {
        g_percentSmoothed = p;
        g_percentDisplay = (int)(p + 0.5f);
        g_hasPercent = true;
        g_lastPercentStepMs = millis();
        return;
    }

    g_percentSmoothed = PERCENT_ALPHA * p + (1.0f - PERCENT_ALPHA) * g_percentSmoothed;

    unsigned long now = millis();
    if (now - g_lastPercentStepMs < PERCENT_STEP_INTERVAL_MS)
        return;

    // Histerese maior para evitar oscilacao de alguns pontos percentuais.
    if (g_percentSmoothed >= (float)g_percentDisplay + PERCENT_HYSTERESIS)
    {
        g_percentDisplay++;
        g_lastPercentStepMs = now;
    }
    else if (g_percentSmoothed <= (float)g_percentDisplay - PERCENT_HYSTERESIS)
    {
        g_percentDisplay--;
        g_lastPercentStepMs = now;
    }

    if (g_percentDisplay < 0)
        g_percentDisplay = 0;
    if (g_percentDisplay > 100)
        g_percentDisplay = 100;
}

float batteryVoltageGet()
{
    return g_voltage;
}

int batteryRawGet()
{
    return g_raw;
}

int batteryAdcPinGet()
{
    return g_adcPin;
}

int batteryPercentGet()
{
    return g_percentDisplay;
}

bool batteryLowGet()
{
    return batteryPercentGet() <= 15;
}
