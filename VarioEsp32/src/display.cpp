#include <Arduino.h>

#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>

#include "battery.h"
#include "ble.h"
#include "display.h"

static U8G2_SSD1306_72X40_ER_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE, 9, 8);
static bool g_oledReady = false;
static uint8_t g_oledAddr = 0x3C;

static bool probeOledAddress(uint8_t address)
{
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

static uint8_t findOledAddress()
{
    if (probeOledAddress(0x3C))
        return 0x3C;

    if (probeOledAddress(0x3D))
        return 0x3D;

    return 0;
}

void displayBegin()
{
    Serial.println("OLED: iniciando diagnostico");
    Wire.begin(8, 9);
    Wire.setClock(100000);

    g_oledAddr = findOledAddress();
    if (g_oledAddr == 0)
    {
        Serial.println("OLED: nao encontrado em 0x3C/0x3D");
        g_oledReady = false;
        return;
    }

    Serial.printf("OLED: endereco detectado 0x%02X\n", g_oledAddr);

    g_oled.begin();
    g_oled.setI2CAddress(g_oledAddr << 1);
    g_oled.setPowerSave(0);
    g_oled.setContrast(255);
    g_oled.setFont(u8g2_font_5x7_tf);
    g_oled.clearBuffer();
    g_oled.drawStr(0, 8, "OLED OK");
    g_oled.drawStr(0, 16, g_oledAddr == 0x3C ? "ADDR 3C" : "ADDR 3D");
    g_oled.sendBuffer();
    g_oledReady = true;
    Serial.printf("OLED: pronto addr=0x%02X\n", g_oledAddr);
}

void displayUpdate(float altitude, float vario)
{
    static unsigned long last = 0;
    if (!g_oledReady)
        return;

    if (millis() - last < 180)
        return;

    last = millis();

    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];

    snprintf(line1, sizeof(line1), "ALT %5.1fm", altitude);
    snprintf(line2, sizeof(line2), "VAR %+.2f", vario);
    snprintf(line3, sizeof(line3), "BAT %2d%% %1.2fV", batteryPercentGet(), batteryVoltageGet());

    const char *wifiState = (WiFi.getMode() == WIFI_MODE_STA) ? "STA" : "AP";
    const char *bleState = bleIsConnected() ? "BLE*" : (bleIsEnabled() ? "BLE" : "OFF");
    snprintf(line4, sizeof(line4), "%s %s", wifiState, bleState);

    g_oled.clearBuffer();
    g_oled.drawStr(0, 8, line1);
    g_oled.drawStr(0, 16, line2);
    g_oled.drawStr(0, 24, line3);
    g_oled.drawStr(0, 32, line4);
    g_oled.sendBuffer();
}
