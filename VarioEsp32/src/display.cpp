#include <Arduino.h>

#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <WiFi.h>

#include "battery.h"
#include "ble.h"
#include "display.h"

static SSD1306AsciiWire g_oled;
static bool g_oledReady = false;

void displayBegin()
{
    Wire.setClock(100000);

    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() != 0)
    {
        g_oledReady = false;
        return;
    }

    g_oled.begin(&Adafruit128x64, 0x3C);
    g_oled.setFont(System5x7);
    g_oled.clear();
    g_oled.println("VARIO ESP32");
    g_oledReady = true;
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

    snprintf(line1, sizeof(line1), "ALT %6.1fm", altitude);
    snprintf(line2, sizeof(line2), "V %+.2f m/s", vario);
    snprintf(line3, sizeof(line3), "BAT %2d%% %.2fV", batteryPercentGet(), batteryVoltageGet());

    const char *wifiState = (WiFi.getMode() == WIFI_MODE_STA) ? "STA" : "AP";
    const char *bleState = bleIsConnected() ? "BLE*" : (bleIsEnabled() ? "BLE" : "OFF");
    snprintf(line4, sizeof(line4), "%s %s", wifiState, bleState);

    g_oled.clear();
    g_oled.println(line1);
    g_oled.println(line2);
    g_oled.println(line3);
    g_oled.println(line4);
}
