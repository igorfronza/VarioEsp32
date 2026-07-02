#include <Arduino.h>
#include <math.h>
#include "vario.h"

static float pressure = 1013.25f;
static float altitude = 0.0f;
static float lastAltitude = 0.0f;
static float vario = 0.0f;
static float filteredVario = 0.0f;
static unsigned long lastTime = 0;

static float g_qnh_hPa = 1013.25f;
static float g_altitudeOffset_m = 0.0f;
static float g_sensitivity = 1.0f;
static float g_deadZone_mps = 0.10f;
static bool g_fastResponse = true;
static bool g_adaptiveFilter = true;

static float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static float pressureToAltitude(float pressure_hPa, float qnh_hPa)
{
    return 44330.0f * (1.0f - powf(pressure_hPa / qnh_hPa, 0.1903f));
}

static float altitudeAlpha()
{
    return g_fastResponse ? 0.12f : 0.06f;
}

static float varioAlpha(float instantVario)
{
    float baseAlpha = g_fastResponse ? 0.16f : 0.07f;
    float maxAlpha = g_fastResponse ? 0.30f : 0.14f;

    if (!g_adaptiveFilter)
        return baseAlpha;

    float activity = clampf(fabsf(instantVario) / 3.0f, 0.0f, 1.0f);
    return baseAlpha + (maxAlpha - baseAlpha) * activity;
}

bool varioBegin(float pressaoInicial_hPa)
{
    pressure = pressaoInicial_hPa;
    float alt0 = pressureToAltitude(pressaoInicial_hPa, g_qnh_hPa) + g_altitudeOffset_m;
    altitude = alt0;
    lastAltitude = alt0;
    vario = 0.0f;
    filteredVario = 0.0f;
    lastTime = millis();
    return true;
}

void varioUpdate(float pressao_hPa)
{
    pressure = pressao_hPa;

    float alt = pressureToAltitude(pressao_hPa, g_qnh_hPa) + g_altitudeOffset_m;
    float altA = altitudeAlpha();
    altitude = altA * alt + (1.0f - altA) * altitude;

    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;

    if (dt < 0.05f)
        return;

    float inst = (altitude - lastAltitude) / dt;
    float vA = varioAlpha(inst);
    filteredVario = vA * inst + (1.0f - vA) * filteredVario;

    vario = filteredVario * g_sensitivity;
    if (fabsf(vario) < g_deadZone_mps)
        vario = 0.0f;

    lastAltitude = altitude;
    lastTime = now;
}

void varioSetQnh(float qnh_hPa)
{
    g_qnh_hPa = clampf(qnh_hPa, 850.0f, 1100.0f);
}

void varioSetAltitudeOffset(float offset_m)
{
    g_altitudeOffset_m = clampf(offset_m, -3000.0f, 3000.0f);
}

void varioSetSensitivity(float sensitivity)
{
    g_sensitivity = clampf(sensitivity, 0.2f, 4.0f);
}

void varioSetDeadZone(float deadZone_mps)
{
    g_deadZone_mps = clampf(deadZone_mps, 0.0f, 2.0f);
}

void varioSetResponseFast(bool fast)
{
    g_fastResponse = fast;
}

void varioSetAdaptiveFilter(bool enabled)
{
    g_adaptiveFilter = enabled;
}

void varioAutoCalibrate(float pressao_hPa)
{
    g_altitudeOffset_m = -pressureToAltitude(pressao_hPa, g_qnh_hPa);
}

float varioGet()
{
    return vario;
}

float altitudeGet()
{
    return altitude;
}

float pressureGet()
{
    return pressure;
}