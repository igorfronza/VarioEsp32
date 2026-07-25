#include <Arduino.h>

#include <stdlib.h>

#include "sensor.h"
#include "storage.h"
#include "vario.h"
#include "sound.h"
#include "battery.h"
#include "ble.h"
#include "display.h"
#include "web.h"

static const float kFixedDeadZoneMps = 0.12f;
static const bool kFixedFastResponse = true;
static const bool kFixedAdaptiveFilter = true;
static const int kFixedBatteryAdcPin = 0;
static const float kFixedBatteryDivider = 2.0f;
static const float kFixedBatteryMinV = 3.30f;
static const float kFixedBatteryMaxV = 4.20f;

static float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static void applyConfig(const VarioConfig &cfg)
{
    const float climbStart = clampf(cfg.climbStart_mps, 0.15f, 5.0f);
    const float sinkStart = clampf(cfg.sinkStart_mps, -8.0f, -0.40f);
    const float climbStop = clampf(climbStart * 0.45f, 0.05f, climbStart - 0.03f);
    const float sinkStop = clampf(sinkStart * 0.45f, sinkStart, -0.05f);

    SoundConfig snd = {
        climbStart,
        climbStop,
        sinkStart,
        sinkStop,
        cfg.climbBaseHz,
        cfg.climbGainHzPerMps,
        cfg.sinkBaseHz,
        cfg.volumePct,
        true};

    varioSetQnh(cfg.qnh_hPa);
    varioSetAltitudeOffset(cfg.altitudeOffset_m);
    varioSetSensitivity(cfg.sensitivity);
    varioSetDeadZone(kFixedDeadZoneMps);
    varioSetResponseFast(kFixedFastResponse);
    varioSetAdaptiveFilter(kFixedAdaptiveFilter);
    soundSetConfig(snd);
}

static void printConfig(const VarioConfig &cfg)
{
    Serial.println("CFG");
    Serial.printf("  qnh=%.2f\n", cfg.qnh_hPa);
    Serial.printf("  offset=%.2f\n", cfg.altitudeOffset_m);
    Serial.printf("  sens=%.2f\n", cfg.sensitivity);
    Serial.printf("  climb_start=%.2f\n", cfg.climbStart_mps);
    Serial.printf("  sink_start=%.2f\n", cfg.sinkStart_mps);
    Serial.printf("  ble=%d\n", cfg.bleEnabled ? 1 : 0);
    Serial.printf("  wifi_sta=%d\n", cfg.wifiStaMode ? 1 : 0);
    Serial.printf("  ap_pass=%s\n", cfg.apPassword.c_str());
}

static void printHelp()
{
    Serial.println("Comandos:");
    Serial.println("  show");
    Serial.println("  save");
    Serial.println("  defaults");
    Serial.println("  set qnh <hPa>");
    Serial.println("  set offset <m>");
    Serial.println("  set sens <x>");
    Serial.println("  set cstart <mps>");
    Serial.println("  set sstart <mps>");
    Serial.println("  set password <senha>");
}

static void handleSetCommand(const String &key, const String &value)
{
    VarioConfig cfg = storageGetConfig();

    if (key == "qnh")
        cfg.qnh_hPa = value.toFloat();
    else if (key == "offset")
        cfg.altitudeOffset_m = value.toFloat();
    else if (key == "sens")
        cfg.sensitivity = value.toFloat();
    else if (key == "cstart")
        cfg.climbStart_mps = value.toFloat();
    else if (key == "sstart")
        cfg.sinkStart_mps = value.toFloat();
    else if (key == "password")
        cfg.apPassword = value;
    else
    {
        Serial.println("Parametro desconhecido.");
        return;
    }

    storageSetConfig(cfg);
    applyConfig(storageGetConfig());
    Serial.println("Parametro atualizado.");
    printConfig(storageGetConfig());
}

static void handleSerialCommands()
{
    if (!Serial.available())
        return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
        return;

    if (line == "help")
    {
        printHelp();
        return;
    }

    if (line == "show")
    {
        printConfig(storageGetConfig());
        return;
    }

    if (line == "save")
    {
        Serial.println(storageSave() ? "Configuracao salva." : "Falha ao salvar.");
        return;
    }

    if (line == "defaults")
    {
        storageResetToDefaults();
        applyConfig(storageGetConfig());
        Serial.println("Defaults carregados em RAM. Use save para persistir.");
        printConfig(storageGetConfig());
        return;
    }

    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    if (firstSpace > 0 && secondSpace > firstSpace)
    {
        String cmd = line.substring(0, firstSpace);
        String key = line.substring(firstSpace + 1, secondSpace);
        String value = line.substring(secondSpace + 1);

        cmd.trim();
        key.trim();
        value.trim();

        if (cmd == "set" && value.length() > 0)
        {
            handleSetCommand(key, value);
            return;
        }
    }

    Serial.println("Comando invalido. Digite help.");
}

void setup()
{
    const bool kRadioDiagOff = false;
    const int kBuzzerPin = 4;

    Serial.begin(115200);
    // Drive buzzer pin low as early as possible during boot.
    soundBegin(kBuzzerPin);
    delay(1000);

    Serial.println();
    Serial.println("===== VARIO ESP32 =====");

    displayBegin();

    if (!sensorBegin())
    {
        Serial.println("BME280 nao encontrado.");
        displayMessage("BME280 ERRO", "SDA 8 SCL 9", "ADDR 76/77");
        while (true)
            delay(100);
    }

    Serial.println("Sensor OK");
    sensorSetFastMode();
    Serial.println("Sensor modo rapido (25Hz)");
    displayMessage("BME280 OK", "Iniciando", nullptr);

    if (storageBegin())
    {
        storageLoad();
        Serial.println("Storage OK");
    }
    else
    {
        Serial.println("Storage indisponivel, usando defaults.");
    }

    const VarioConfig &cfg = storageGetConfig();

    applyConfig(cfg);

    float pressure_hPa = sensorPressure();

    varioBegin(pressure_hPa);
    batteryBegin(kFixedBatteryAdcPin, kFixedBatteryDivider, kFixedBatteryMinV, kFixedBatteryMaxV);
    if (!kRadioDiagOff)
    {
        bleBegin(cfg.bleEnabled, cfg.bleName.c_str());
        webBegin();
    }

    Serial.printf(
        "QNH %.2f hPa   RESP FAST FIXO   ADAPT ON FIXO\n",
        cfg.qnh_hPa);
    printHelp();
}

void loop()
{
    static unsigned long lastPrint = 0;
    static unsigned long lastPressureMs = 0;
    static bool hasPressureSample = false;
    static float pressureSample_hPa = 1013.25f;

    handleSerialCommands();

    unsigned long now = millis();
    bool newPressureSample = false;
    if (!hasPressureSample || (now - lastPressureMs) >= 40)
    {
        pressureSample_hPa = sensorPressure();
        hasPressureSample = true;
        lastPressureMs = now;
        newPressureSample = true;
    }

    if (newPressureSample)
        varioUpdate(pressureSample_hPa);
    batteryUpdate();
    soundUpdate(varioGet());
    bleUpdate();
    displayUpdate(altitudeGet(), varioGet());
    webUpdate();

    if (millis() - lastPrint >= 2000)
    {
        Serial.printf(
            "P %.2f hPa   ALT %.2f m   VARIO %.2f m/s\n",
            pressureGet(),
            altitudeGet(),
            varioGet());
        Serial.printf(
            "BAT pin %d raw %d   %.2f V   %d%%\n",
            batteryAdcPinGet(),
            batteryRawGet(),
            batteryVoltageGet(),
            batteryPercentGet());
        lastPrint = millis();
    }

    delay(5);
}
