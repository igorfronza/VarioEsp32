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
static ToneMode lastToneMode = TONE_IDLE;
static bool toneRunning = false;

static const uint8_t CLIMB_ENTER_SAMPLES = 4;
static const uint8_t SINK_ENTER_SAMPLES = 5;

static SoundConfig g_cfg = {
    0.20f,
    0.08f,
    -0.60f,
    -0.20f,
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
    cfg.climbStart_mps = clampf(cfg.climbStart_mps, 0.15f, 5.0f);
    cfg.climbStop_mps = clampf(cfg.climbStop_mps, 0.05f, cfg.climbStart_mps - 0.03f);
    cfg.sinkStart_mps = clampf(cfg.sinkStart_mps, -8.0f, -0.40f);
    cfg.sinkStop_mps = clampf(cfg.sinkStop_mps, cfg.sinkStart_mps, -0.01f);
    cfg.climbBaseHz = clampi(cfg.climbBaseHz, 400, 2500);
    cfg.climbGainHzPerMps = clampi(cfg.climbGainHzPerMps, 50, 1000);
    cfg.sinkBaseHz = clampi(cfg.sinkBaseHz, 150, 1200);
    cfg.volumePct = clampi(cfg.volumePct, 0, 100);
}

static void toneStop()
{
    if (toneRunning)
    {
        noTone(buzzerPin);
        toneRunning = false;
    }

    // Some ESP32 tone/noTone implementations detach LEDC from the pin.
    // Reassert GPIO mode before driving LOW to avoid __digitalWrite() errors.
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
}

static void toneStart(int freq)
{
    tone(buzzerPin, freq);
    toneRunning = true;
}

void soundBegin(int gpio)
{
    buzzerPin = gpio;
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
    toneStop();
}

void soundSetConfig(const SoundConfig &cfg)
{
    g_cfg = cfg;
    normalizeConfig(g_cfg);
}

void soundUpdate(float vario)
{
    static unsigned long segmentStart = 0;
    static unsigned long climbOnMs = 60;
    static unsigned long climbOffMs = 180;
    static int currentFreq = 0;
    static bool toneOn = false;
    static uint8_t sinkStep = 0;
    static uint8_t climbEntryCount = 0;
    static uint8_t sinkEntryCount = 0;

    normalizeConfig(g_cfg);

    unsigned long now = millis();

    if (toneMode == TONE_IDLE)
    {
        if (vario >= g_cfg.climbStart_mps)
        {
            if (climbEntryCount < 255)
                climbEntryCount++;
        }
        else
        {
            climbEntryCount = 0;
        }

        if (g_cfg.sinkToneEnabled && vario <= g_cfg.sinkStart_mps)
        {
            if (sinkEntryCount < 255)
                sinkEntryCount++;
        }
        else
        {
            sinkEntryCount = 0;
        }

        if (climbEntryCount >= CLIMB_ENTER_SAMPLES)
            toneMode = TONE_CLIMB;
        else if (sinkEntryCount >= SINK_ENTER_SAMPLES)
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

    if (toneMode != lastToneMode)
    {
        toneStop();
        toneOn = false;
        currentFreq = 0;
        sinkStep = 0;
        segmentStart = now;
        climbEntryCount = 0;
        sinkEntryCount = 0;

        if (toneMode == TONE_CLIMB)
        {
            // Start first climb beep immediately when mode engages.
            climbOffMs = 0;
        }

        lastToneMode = toneMode;
    }

    if (toneMode == TONE_IDLE)
    {
        if (toneOn)
            toneStop();
        currentFreq = 0;
        toneOn = false;
        return;
    }

    if (toneMode == TONE_CLIMB)
    {
        float climb = clampf(vario, 0.0f, 8.0f);
        int freq = g_cfg.climbBaseHz + (int)(climb * g_cfg.climbGainHzPerMps);

        unsigned long period = (unsigned long)clampf(360.0f - climb * 55.0f, 90.0f, 360.0f);
        unsigned long onMs = (unsigned long)clampf(55.0f + climb * 4.0f, 45.0f, 80.0f);
        unsigned long offMs = period > onMs ? (period - onMs) : 20;
        unsigned long elapsed = now - segmentStart;

        if (toneOn)
        {
            if (elapsed >= climbOnMs)
            {
                toneStop();
                toneOn = false;
                climbOffMs = offMs;
                segmentStart = now;
            }
        }
        else if (elapsed >= climbOffMs)
        {
            toneStart(freq);
            toneOn = true;
            currentFreq = freq;
            climbOnMs = onMs;
            segmentStart = now;
        }

        return;
    }

    const int sinkFreq = g_cfg.sinkBaseHz;
    const unsigned long onMs = 85;
    const unsigned long shortOffMs = 70;
    const unsigned long longOffMs = 260;
    unsigned long elapsed = now - segmentStart;

    if (sinkStep == 0)
    {
        if (!toneOn)
        {
            toneStart(sinkFreq);
            toneOn = true;
            currentFreq = sinkFreq;
            segmentStart = now;
        }
        else if (elapsed >= onMs)
        {
            toneStop();
            toneOn = false;
            sinkStep = 1;
            segmentStart = now;
        }
        return;
    }

    if (sinkStep == 1)
    {
        if (elapsed >= shortOffMs)
        {
            toneStart(sinkFreq);
            toneOn = true;
            currentFreq = sinkFreq;
            sinkStep = 2;
            segmentStart = now;
        }
        return;
    }

    if (sinkStep == 2)
    {
        if (elapsed >= onMs)
        {
            toneStop();
            toneOn = false;
            sinkStep = 3;
            segmentStart = now;
        }
        return;
    }

    if (elapsed >= longOffMs)
    {
        sinkStep = 0;
        segmentStart = now;
    }
}
