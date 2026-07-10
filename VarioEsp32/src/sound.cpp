#include <Arduino.h>
#include "sound.h"

static int buzzerPin = 2;

enum ToneMode
{
    TONE_IDLE,
    TONE_CLIMB,
    TONE_SINK
};

static ToneMode toneMode = TONE_IDLE;

static SoundConfig g_cfg = {
    0.10f,
    0.05f,
    -0.22f,
    -0.10f,
    1100,
    280,
    240,
    70,
    true};

static float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static int clampi(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static void normalizeConfig(SoundConfig &cfg)
{
    cfg.climbStart_mps = clampf(cfg.climbStart_mps, 0.03f, 5.0f);
    cfg.climbStop_mps = clampf(cfg.climbStop_mps, 0.00f, cfg.climbStart_mps - 0.01f);
    cfg.sinkStart_mps = clampf(cfg.sinkStart_mps, -8.0f, -0.05f);
    cfg.sinkStop_mps = clampf(cfg.sinkStop_mps, cfg.sinkStart_mps, -0.01f);
    cfg.climbBaseHz = clampi(cfg.climbBaseHz, 400, 2500);
    cfg.climbGainHzPerMps = clampi(cfg.climbGainHzPerMps, 50, 1000);
    cfg.sinkBaseHz = clampi(cfg.sinkBaseHz, 150, 1200);
    cfg.volumePct = clampi(cfg.volumePct, 0, 100);
}

static void toneStop()
{
    noTone(buzzerPin);
}

static void toneStart(int freq)
{
    tone(buzzerPin, freq);
}

void soundBegin(int gpio)
{
    buzzerPin = gpio;
    pinMode(buzzerPin, OUTPUT);
    toneStop();
}

void soundSetConfig(const SoundConfig &cfg)
{
    g_cfg = cfg;
    normalizeConfig(g_cfg);
}

void soundUpdate(float vario)
{
    static unsigned long last = 0;
    static int currentFreq = 0;
    static bool toneOn = false;

    normalizeConfig(g_cfg);

    if (toneMode == TONE_IDLE)
    {
        if (vario >= g_cfg.climbStart_mps)
            toneMode = TONE_CLIMB;
        else if (g_cfg.sinkToneEnabled && vario <= g_cfg.sinkStart_mps)
            toneMode = TONE_SINK;
    }
    else if (toneMode == TONE_CLIMB)
    {
        if (vario <= g_cfg.climbStop_mps)
            toneMode = TONE_IDLE;
    }
    else if (toneMode == TONE_SINK)
    {
        if (vario >= g_cfg.sinkStop_mps)
            toneMode = TONE_IDLE;
    }

    if (toneMode == TONE_IDLE)
    {
        if (toneOn)
            toneStop();
        currentFreq = 0;
        toneOn = false;
        last = millis();
        return;
    }

    if (toneMode == TONE_CLIMB)
    {
        float climb = clampf(vario, 0.0f, 8.0f);
        int freq = g_cfg.climbBaseHz + (int)(climb * g_cfg.climbGainHzPerMps);

        unsigned long elapsed = millis() - last;
        unsigned long interval = (unsigned long)clampf(90.0f - climb * 14.0f, 35.0f, 90.0f);
        unsigned long duration = (unsigned long)clampf(22.0f + climb * 2.5f, 14.0f, interval - 6);

        if (!toneOn && elapsed >= interval)
        {
            toneStart(freq);
            toneOn = true;
            currentFreq = freq;
            last = millis();
        }
        else if (toneOn && elapsed >= duration)
        {
            toneStop();
            toneOn = false;
            last = millis();
        }

        return;
    }

    float sink = clampf(fabsf(vario), 0.0f, 8.0f);
    int freq = g_cfg.sinkBaseHz + (int)(sink * 40.0f);
    bool graveEnough = (freq <= 320) || (sink >= 1.6f && freq <= 360);
    unsigned long interval = (unsigned long)clampf(260.0f - sink * 18.0f, 120.0f, 320.0f);
    unsigned long duration = (unsigned long)clampf(interval * 0.86f, 60.0f, interval - 12);

    unsigned long elapsed = millis() - last;
    if (graveEnough)
    {
        if (!toneOn || abs(freq - currentFreq) >= 8 || elapsed >= 160)
        {
            toneStart(freq);
            toneOn = true;
            currentFreq = freq;
            last = millis();
        }
        return;
    }

    if (sink < 1.0f)
    {
        if (!toneOn && elapsed >= interval)
        {
            toneStart(freq);
            toneOn = true;
            currentFreq = freq;
            last = millis();
        }
        else if (toneOn && elapsed >= duration)
        {
            toneStop();
            toneOn = false;
            last = millis();
        }
    }
    else if (!toneOn || abs(freq - currentFreq) >= 10 || elapsed >= 140)
    {
        toneStart(freq);
        toneOn = true;
        currentFreq = freq;
        last = millis();
    }
}
