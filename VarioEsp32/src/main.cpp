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

static void applyConfig(const VarioConfig &cfg)
{
    SoundConfig snd = {
        cfg.climbStart_mps,
        cfg.climbStop_mps,
        cfg.sinkStart_mps,
        cfg.sinkStop_mps,
        cfg.climbBaseHz,
        cfg.climbGainHzPerMps,
        cfg.sinkBaseHz,
        cfg.volumePct,
        cfg.sinkToneEnabled};

    varioSetQnh(cfg.qnh_hPa);
    varioSetAltitudeOffset(cfg.altitudeOffset_m);
    varioSetSensitivity(cfg.sensitivity);
    varioSetDeadZone(cfg.deadZone_mps);
    varioSetResponseFast(cfg.fastResponse);
    varioSetAdaptiveFilter(cfg.adaptiveFilter);
    soundSetConfig(snd);
}

static void printConfig(const VarioConfig &cfg)
{
    Serial.println("CFG");
    Serial.printf("  qnh=%.2f\n", cfg.qnh_hPa);
    Serial.printf("  offset=%.2f\n", cfg.altitudeOffset_m);
    Serial.printf("  sens=%.2f\n", cfg.sensitivity);
    Serial.printf("  dead=%.2f\n", cfg.deadZone_mps);
    Serial.printf("  fast=%d\n", cfg.fastResponse ? 1 : 0);
    Serial.printf("  adapt=%d\n", cfg.adaptiveFilter ? 1 : 0);
    Serial.printf("  autozero=%d\n", cfg.autoCalibrateOnBoot ? 1 : 0);
    Serial.printf("  ble=%d\n", cfg.bleEnabled ? 1 : 0);
    Serial.printf("  wifi_sta=%d\n", cfg.wifiStaMode ? 1 : 0);
    Serial.printf("  battery_pin=%d\n", cfg.batteryAdcPin);
    Serial.printf("  battery_divider=%.2f\n", cfg.batteryDivider);
    Serial.printf("  battery_min=%.2f\n", cfg.batteryMinV);
    Serial.printf("  battery_max=%.2f\n", cfg.batteryMaxV);
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
    Serial.println("  set dead <mps>");
    Serial.println("  set fast <0|1>");
    Serial.println("  set adapt <0|1>");
    Serial.println("  set autozero <0|1>");
    Serial.println("  set bpin <gpio>");
    Serial.println("  set bdiv <ratio>");
    Serial.println("  set bmin <V>");
    Serial.println("  set bmax <V>");
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
    else if (key == "dead")
        cfg.deadZone_mps = value.toFloat();
    else if (key == "fast")
        cfg.fastResponse = value.toInt() != 0;
    else if (key == "adapt")
        cfg.adaptiveFilter = value.toInt() != 0;
    else if (key == "autozero")
        cfg.autoCalibrateOnBoot = value.toInt() != 0;
    else if (key == "bpin")
        cfg.batteryAdcPin = value.toInt();
    else if (key == "bdiv")
        cfg.batteryDivider = value.toFloat();
    else if (key == "bmin")
        cfg.batteryMinV = value.toFloat();
    else if (key == "bmax")
        cfg.batteryMaxV = value.toFloat();
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
    Serial.begin(115200);
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
    if (cfg.autoCalibrateOnBoot)
        varioAutoCalibrate(pressure_hPa);

    varioBegin(pressure_hPa);
    soundBegin(2);
    batteryBegin(cfg.batteryAdcPin, cfg.batteryDivider, cfg.batteryMinV, cfg.batteryMaxV);
    bleBegin(cfg.bleEnabled, cfg.bleName.c_str());
    webBegin();

    Serial.printf(
        "QNH %.2f hPa   RESP %s   ADAPT %s   AUTOZERO %s\n",
        cfg.qnh_hPa,
        cfg.fastResponse ? "FAST" : "SLOW",
        cfg.adaptiveFilter ? "ON" : "OFF",
        cfg.autoCalibrateOnBoot ? "ON" : "OFF");
    printHelp();
}

void loop()
{
    static unsigned long lastPrint = 0;

    handleSerialCommands();
    varioUpdate(sensorPressure());
    batteryUpdate();
    soundUpdate(varioGet());
    bleUpdate();
    displayUpdate(altitudeGet(), varioGet());
    webUpdate();

    if (millis() - lastPrint >= 500)
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

    delay(50);
}
