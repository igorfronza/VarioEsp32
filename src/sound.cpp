#include <Arduino.h>
#include "sound.h"

static int buzzerPin = 2;

// LEDC PWM para controle de volume no buzzer passivo.
// duty cycle varia com volumePct → 0% = mudo, 100% = 50% duty (meia onda).
#define BUZZER_LEDC_RESOLUTION 10 // 10-bit: 0-1023

enum ToneMode
{
    TONE_SILENT,
    TONE_CLIMB,
    TONE_SINK
};

static ToneMode toneMode = TONE_SILENT;
static ToneMode lastToneMode = TONE_SILENT;
static bool toneRunning = false;
static unsigned long g_muteUntilMs = 0;
static unsigned long g_forceClimbUntilMs = 0;
static float g_forceClimbMps = 1.0f;
static unsigned long g_forceClimbStartMs = 0;
static unsigned long g_forceSinkUntilMs = 0;
static unsigned long g_forceSinkStartMs = 0;

// Slider da interface web: vario simulado contínuo, sem rampa nem debounce.
static float g_simVarioMps = 0.0f;
static unsigned long g_simVarioUntilMs = 0;
static const unsigned long SIM_VARIO_TIMEOUT_MS = 600;

// Rampa de teste: sobe/desce suavemente em 1 segundo (simula entrada real em térmica).
static const unsigned long TEST_RAMP_MS = 1000;

static const uint8_t CLIMB_ENTER_SAMPLES = 4;
static const uint8_t SINK_ENTER_SAMPLES = 5;
static const float BEEP_VELOCITY_SENSITIVITY = 0.10f;
static const unsigned long MODE_EVAL_INTERVAL_MS = 180;

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
        ledcDetach(buzzerPin);
        toneRunning = false;
    }

    // Keep a strong low level while idle to better shunt coupled noise.
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
}

static void toneStart(int freq)
{
    // Volume: mapeia 0-100% para duty 0-50% (meia onda é o máximo útil em buzzer passivo).
    int duty = map(g_cfg.volumePct, 0, 100, 0, 512);
    if (duty < 1)
        return; // volume 0 = mudo

    ledcAttach(buzzerPin, freq, BUZZER_LEDC_RESOLUTION);
    ledcWrite(buzzerPin, duty);
    toneRunning = true;
}

void soundSetBootMuteMs(unsigned long muteMs)
{
    g_muteUntilMs = millis() + muteMs;
}

void soundBegin(int gpio)
{
    buzzerPin = gpio;
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
    toneStop();
    soundSetBootMuteMs(3000);
}

void soundSetConfig(const SoundConfig &cfg)
{
    g_cfg = cfg;
    normalizeConfig(g_cfg);
}

void soundSetVolume(int pct)
{
    g_cfg.volumePct = clampi(pct, 0, 100);
}

void soundTriggerClimbTest(float climbMps, unsigned long durationMs)
{
    const unsigned long minMs = 500;
    const unsigned long maxMs = 15000;
    if (durationMs < minMs)
        durationMs = minMs;
    if (durationMs > maxMs)
        durationMs = maxMs;

    g_forceClimbMps = clampf(climbMps, 1.0f, 7.0f);
    g_forceClimbUntilMs = millis() + durationMs;
    g_forceClimbStartMs = millis();
}

void soundTriggerSinkTest(unsigned long durationMs)
{
    const unsigned long minMs = 500;
    const unsigned long maxMs = 15000;
    if (durationMs < minMs)
        durationMs = minMs;
    if (durationMs > maxMs)
        durationMs = maxMs;

    g_forceSinkUntilMs = millis() + durationMs;
    g_forceSinkStartMs = millis();
}

void soundSetSimulatedVario(float mps)
{
    g_simVarioMps = clampf(mps, -7.0f, 7.0f);
    if (fabsf(g_simVarioMps) < 0.05f)
    {
        // Desliga simulação
        g_simVarioMps = 0.0f;
        g_simVarioUntilMs = 0;
    }
    else
    {
        g_simVarioUntilMs = millis() + SIM_VARIO_TIMEOUT_MS;
    }
}

void soundUpdate(float vario)
{
    static unsigned long segmentStart = 0;
    static unsigned long lastModeEvalMs = 0;
    static unsigned long climbOnMs = 60;
    static unsigned long climbOffMs = 180;
    static unsigned long sinkOnMs = 260;
    static unsigned long sinkOffMs = 180;
    static int currentFreq = 0;
    static bool toneOn = false;
    static uint8_t climbEntryCount = 0;
    static uint8_t sinkEntryCount = 0;
    static float beepVelocity = 0.0f;

    normalizeConfig(g_cfg);

    unsigned long now = millis();
    float effectiveVario = vario;

    // --- Slider da interface web: prioridade máxima, sem rampa ---
    bool isSim = (now < g_simVarioUntilMs && fabsf(g_simVarioMps) > 0.04f);
    if (isSim)
    {
        effectiveVario = g_simVarioMps;
    }

    // --- Modo teste com rampa suave (simula entrada real em térmica/sink) ---
    bool isTestClimb = (now < g_forceClimbUntilMs);
    bool isTestSink = (now < g_forceSinkUntilMs);
    bool isTest = isTestClimb || isTestSink;

    // Debounce: bypass durante slider (isSim) e testes (isTest)
    bool bypassDebounce = isSim || isTest;

    if (!isSim && isTestClimb)
    {
        unsigned long rampElapsed = now - g_forceClimbStartMs;
        float ramp = clampf((float)rampElapsed / (float)TEST_RAMP_MS, 0.0f, 1.0f);
        effectiveVario = g_forceClimbMps * ramp;
    }
    else if (!isSim && isTestSink)
    {
        unsigned long rampElapsed = now - g_forceSinkStartMs;
        float ramp = clampf((float)rampElapsed / (float)TEST_RAMP_MS, 0.0f, 1.0f);
        float sinkTarget = g_cfg.sinkStart_mps - 0.40f;
        effectiveVario = sinkTarget * ramp;
    }

    if (now < g_muteUntilMs)
    {
        toneStop();
        toneMode = TONE_SILENT;
        lastToneMode = TONE_SILENT;
        toneOn = false;
        currentFreq = 0;
        climbEntryCount = 0;
        sinkEntryCount = 0;
        beepVelocity = 0.0f;
        segmentStart = now;
        return;
    }

    bool evalDue = (now - lastModeEvalMs) >= MODE_EVAL_INTERVAL_MS;
    if (evalDue)
        lastModeEvalMs = now;

    if (toneMode == TONE_SILENT)
    {
        if (evalDue || bypassDebounce)
        {
            float climbTrigger = g_cfg.climbStart_mps;
            if (effectiveVario >= climbTrigger)
            {
                if (climbEntryCount < 255)
                    climbEntryCount++;
            }
            else
            {
                climbEntryCount = 0;
            }

            if (g_cfg.sinkToneEnabled && effectiveVario <= g_cfg.sinkStart_mps)
            {
                if (sinkEntryCount < 255)
                    sinkEntryCount++;
            }
            else
            {
                sinkEntryCount = 0;
            }

            // Em modo slider/teste, basta 1 amostra (resposta imediata).
            uint8_t requiredSamples = bypassDebounce ? 1 : CLIMB_ENTER_SAMPLES;

            if (climbEntryCount >= requiredSamples)
                toneMode = TONE_CLIMB;
            else if (sinkEntryCount >= requiredSamples)
                toneMode = TONE_SINK;
        }
    }
    else if (toneMode == TONE_CLIMB)
    {
        if (effectiveVario <= g_cfg.climbStop_mps)
            toneMode = TONE_SILENT;
    }
    else if (toneMode == TONE_SINK)
    {
        if (effectiveVario >= g_cfg.sinkStop_mps)
            toneMode = TONE_SILENT;
    }

    if (toneMode != lastToneMode)
    {
        toneStop();
        toneOn = false;
        currentFreq = 0;
        segmentStart = now;
        climbEntryCount = 0;
        sinkEntryCount = 0;
        beepVelocity = effectiveVario;

        if (toneMode == TONE_CLIMB)
        {
            // Start first climb beep immediately when mode engages.
            climbOffMs = 0;
        }
        else if (toneMode == TONE_SINK)
        {
            // Start first sink beep immediately when mode engages.
            sinkOffMs = 0;
        }

        lastToneMode = toneMode;
    }

    if (toneMode == TONE_SILENT)
    {
        if (toneOn)
            toneStop();
        currentFreq = 0;
        toneOn = false;
        return;
    }

    if (toneMode == TONE_CLIMB)
    {
        // --- SUBIDA: cadência e frequência proporcionais à taxa de subida ---
        // Referência: lógica clássica de variômetros (BlueFly, XC Tracer, FlySkyHy).
        float climb = clampf(effectiveVario, 0.0f, 8.0f);
        if (fabsf(climb - beepVelocity) >= BEEP_VELOCITY_SENSITIVITY)
            beepVelocity = climb;

        // Frequência: base + ganho * m/s  (tom mais agudo = mais lift)
        int freq = g_cfg.climbBaseHz + (int)(beepVelocity * g_cfg.climbGainHzPerMps);

        // Cadência: escala não-linear agressiva.
        //  0.2 m/s → ~420 ms  (~2.4 bips/s)
        //  1.0 m/s → ~230 ms  (~4.3 bips/s)
        //  3.0 m/s → ~120 ms  (~8.3 bips/s)
        //  5.0 m/s → ~100 ms  (10 bips/s, quase contínuo)
        unsigned long period = (unsigned long)clampf(480.0f / (0.40f + beepVelocity * 0.85f), 85.0f, 520.0f);

        // Duração do bip: ~70-88 ms, ligeiramente mais longo em lift forte.
        unsigned long onMs = (unsigned long)clampf(64.0f + beepVelocity * 4.5f, 60.0f, 90.0f);
        unsigned long offMs = period > onMs ? (period - onMs) : 25;
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

    // --- DESCIDA: frequência proporcional ao sink, padrão pulsado ---
    // Tom mais grave = afundando mais rápido.
    float sink = clampf(-effectiveVario, 0.0f, 8.0f); // magnitude positiva do sink
    int sinkFreq = g_cfg.sinkBaseHz - (int)(sink * 28.0f);
    if (sinkFreq < 130)
        sinkFreq = 130;

    // Cadência de sink: pulsos mais frequentes conforme sink aumenta.
    //  0.6 m/s → período ~550 ms
    //  2.0 m/s → período ~350 ms
    //  5.0 m/s → período ~240 ms
    unsigned long sinkPeriod = (unsigned long)clampf(640.0f - sink * 82.0f, 200.0f, 640.0f);

    // Duração do pulso de sink proporcional ao sink.
    unsigned long sOnMs = (unsigned long)clampf(100.0f + sink * 32.0f, 90.0f, 260.0f);
    unsigned long sOffMs = sinkPeriod > sOnMs ? (sinkPeriod - sOnMs) : 60;
    unsigned long sElapsed = now - segmentStart;

    if (toneOn)
    {
        if (sElapsed >= sinkOnMs)
        {
            toneStop();
            toneOn = false;
            segmentStart = now;
            sinkOffMs = sOffMs;
        }
    }
    else if (sElapsed >= sinkOffMs)
    {
        toneStart(sinkFreq);
        toneOn = true;
        currentFreq = sinkFreq;
        sinkOnMs = sOnMs;
        segmentStart = now;
    }
}
