#include <Arduino.h>

#include <NimBLEDevice.h>

#include "battery.h"
#include "ble.h"
#include "sensor.h"
#include "vario.h"

static bool g_bleEnabled = false;
static bool g_bleConnected = false;
static NimBLECharacteristic *g_tx = nullptr;

static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

class VarioServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        (void)pServer;
        (void)connInfo;
        g_bleConnected = true;
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)connInfo;
        (void)reason;
        g_bleConnected = false;
        NimBLEDevice::startAdvertising();
    }
};

static String makeLk8Sentence()
{
    int pressurePa = (int)(pressureGet() * 100.0f + 0.5f);
    int altCm = (int)(altitudeGet() * 100.0f);
    int varioCms = (int)(varioGet() * 100.0f);
    int tempC = (int)(sensorTemperature() + 0.5f);
    int battMv = (int)(batteryVoltageGet() * 1000.0f + 0.5f);

    String payload = "LK8EX1," + String(pressurePa) + "," + String(altCm) + "," + String(varioCms) + "," + String(tempC) + "," + String(battMv);

    uint8_t cs = 0;
    for (size_t i = 0; i < payload.length(); i++)
        cs ^= (uint8_t)payload[i];

    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", cs);
    return "$" + payload + "*" + String(hex) + "\r\n";
}

void bleBegin(bool enabled, const char *deviceName)
{
    g_bleEnabled = enabled;
    if (!g_bleEnabled)
        return;

    NimBLEDevice::init(deviceName);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new VarioServerCallbacks());

    NimBLEService *service = server->createService(SERVICE_UUID);
    g_tx = service->createCharacteristic(CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

    service->start();
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->start();
}

void bleUpdate()
{
    static unsigned long last = 0;

    if (!g_bleEnabled || !g_bleConnected || g_tx == nullptr)
        return;

    if (millis() - last < 200)
        return;

    String sentence = makeLk8Sentence();
    g_tx->setValue((uint8_t *)sentence.c_str(), sentence.length());
    g_tx->notify();
    last = millis();
}

bool bleIsEnabled()
{
    return g_bleEnabled;
}

bool bleIsConnected()
{
    return g_bleConnected;
}
