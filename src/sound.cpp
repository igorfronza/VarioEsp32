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
static const unsigned long SIM_VARIO_TIMEOUT_MS = 5000;

// Rampa de teste: sobe/desce suavemente em 1 segundo (simula entrada real em térmica).
static const unsigned long TEST_RAMP_MS = 1000;

static const uint8_t CLIMB_ENTER_SAMPLES = 4;
static const uint8_t SINK_ENTER_SAMPLES = 5;
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

// Curva de cadência estilo FlySkyHy/XC Tracer: perto do limiar o bipe é curto
// e esparso (duty baixo), e vai ficando mais cheio/rápido conforme a subida ou
// afundamento aumenta, até soar quase contínuo em valores fortes.
// Cada linha é {taxa_absoluta_mps, periodo_ms, duty_%}; interpola linearmente
// entre pontos e satura fora da tabela.
struct ToneBreakpoint
{
    float rate;
    float periodMs;
    float dutyPct;
};

static const ToneBreakpoint CLIMB_CURVE[] = {
    {0.0f, 550.0f, 20.0f},
    {0.5f, 480.0f, 28.0f},
    {1.0f, 400.0f, 38.0f},
    {2.0f, 300.0f, 55.0f},
    {3.0f, 230.0f, 68.0f},
    {4.0f, 180.0f, 78.0f},
    {5.0f, 140.0f, 88.0f},
};

static const ToneBreakpoint SINK_CURVE[] = {
    {0.0f, 400.0f, 35.0f},
    {1.0f, 320.0f, 45.0f},
    {2.0f, 260.0f, 55.0f},
    {4.0f, 200.0f, 68.0f},
    {6.0f, 160.0f, 80.0f},
};

static void toneCadence(const ToneBreakpoint *curve, int count, float rate, unsigned long &periodMs, unsigned long &onMs)
{
    if (rate <= curve[0].rate)
    {
        periodMs = (unsigned long)curve[0].periodMs;
        onMs = (unsigned long)(curve[0].periodMs * curve[0].dutyPct / 100.0f);
        return;
    }

    for (int i = 0; i < count - 1; i++)
    {
        const ToneBreakpoint &a = curve[i];
        const ToneBreakpoint &b = curve[i + 1];
        if (rate <= b.rate)
        {
            float t = (rate - a.rate) / (b.rate - a.rate);
            float period = a.periodMs + (b.periodMs - a.periodMs) * t;
            float duty = a.dutyPct + (b.dutyPct - a.dutyPct) * t;
            periodMs = (unsigned long)period;
            onMs = (unsigned long)(period * duty / 100.0f);
            return;
        }
    }

    const ToneBreakpoint &last = curve[count - 1];
    periodMs = (unsigned long)last.periodMs;
    onMs = (unsigned long)(last.periodMs * last.dutyPct / 100.0f);
}

static bool ledcAttached = false;

// Mantém o canal LEDC sempre anexado e apenas alterna duty/freq entre bipes.
// Fazer attach/detach a cada bipe (varias vezes por segundo) reconfigura o
// timer do zero e gera cliques/instabilidade audível — o silêncio entre
// bipes é obtido com duty=0, não desanexando o pino.
static void ledcEnsureAttached(int freq)
{
    if (!ledcAttached)
    {
        ledcAttach(buzzerPin, freq, BUZZER_LEDC_RESOLUTION);
        ledcAttached = true;
    }
}

static void toneStop()
{
    ledcEnsureAttached(g_cfg.climbBaseHz);
    ledcWrite(buzzerPin, 0);
    toneRunning = false;
}

static void toneStart(int freq)
{
    // Volume: mapeia 0-100% para duty 0-50% (meia onda é o máximo útil em buzzer passivo).
    int duty = map(g_cfg.volumePct, 0, 100, 0, 512);
    if (duty < 1)
    {
        toneRunning = false;
        return; // volume 0 = mudo
    }

    ledcEnsureAttached(freq);
    ledcChangeFrequency(buzzerPin, freq, BUZZER_LEDC_RESOLUTION);
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
    // Apenas GPIO simples aqui: silencia o pino cedo, sem anexar o LEDC ainda.
    // displayBegin() roda em seguida e sua varredura de pinos do OLED também
    // testa o pino do buzzer como candidato de I2C via pinMode/digitalWrite —
    // se o LEDC já estivesse anexado nesse momento, essa varredura rouba o
    // roteamento do pino e, como agora só anexamos uma vez (sem re-attach a
    // cada bipe), o buzzer ficaria mudo pro resto da sessão. O attach real do
    // LEDC é adiado (lazy, em toneStart/toneStop) até o primeiro bipe em
    // soundUpdate(), que só ocorre depois que setup() (e a varredura) termina.
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
    toneRunning = false;
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
    static unsigned long nextBeepMs = 0;       // quando iniciar o próximo bip
    static unsigned long beepStopMs = 0;       // quando desligar o bip atual
    static int currentFreq = 0;
    static uint8_t climbEntryCount = 0;
    static uint8_t sinkEntryCount = 0;

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
        currentFreq = 0;
        climbEntryCount = 0;
        sinkEntryCount = 0;
        nextBeepMs = 0;
        beepStopMs = 0;
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
        currentFreq = 0;
        segmentStart = now;
        climbEntryCount = 0;
        sinkEntryCount = 0;
        nextBeepMs = 0;
        beepStopMs = 0;

        lastToneMode = toneMode;
    }

    if (toneMode == TONE_SILENT)
    {
        if (toneRunning)
            toneStop();
        currentFreq = 0;
        return;
    }

    if (toneMode == TONE_CLIMB)
    {
        // --- SUBIDA: cadência estilo FlySkyHy/XC Tracer ---
        // Perto do limiar o bipe é curto e espaçado; conforme a subida cresce,
        // o período encurta e o duty cycle aumenta até soar quase contínuo.
        float climb = clampf(effectiveVario, 0.0f, 8.0f);

        if (!toneRunning && now >= nextBeepMs)
        {
            int freq = g_cfg.climbBaseHz + (int)(climb * g_cfg.climbGainHzPerMps);

            unsigned long period, onMs;
            toneCadence(CLIMB_CURVE, sizeof(CLIMB_CURVE) / sizeof(CLIMB_CURVE[0]), climb, period, onMs);

            toneStart(freq);
            currentFreq = freq;
            beepStopMs = now + onMs;
            nextBeepMs = now + period;
        }

        if (toneRunning && now >= beepStopMs)
        {
            toneStop();
        }

        return;
    }

    // --- DESCIDA: mesma ideia, curva própria (mais grave, menos esparsa) ---
    {
        float sinkMag = clampf(-effectiveVario, 0.0f, 8.0f);

        if (!toneRunning && now >= nextBeepMs)
        {
            int freq = g_cfg.sinkBaseHz - (int)(sinkMag * 30.0f);
            if (freq < 120) freq = 120;

            unsigned long period, onMs;
            toneCadence(SINK_CURVE, sizeof(SINK_CURVE) / sizeof(SINK_CURVE[0]), sinkMag, period, onMs);

            toneStart(freq);
            currentFreq = freq;
            beepStopMs = now + onMs;
            nextBeepMs = now + period;
        }

        if (toneRunning && now >= beepStopMs)
        {
            toneStop();
        }
    }
}
