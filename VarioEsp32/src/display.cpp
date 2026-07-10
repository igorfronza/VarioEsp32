#include <Arduino.h>

#include <U8g2lib.h>
#include <WiFi.h>

#include "battery.h"
#include "ble.h"
#include "display.h"

// Keep the BME280 alone on hardware Wire GPIO 8/9. The built-in 0.42" OLED on
// many ESP32-C3 OLED boards is a 72x40 SSD1306-compatible display on SDA=5/SCL=6.
static U8G2 *g_oled = nullptr;
static bool g_oledReady = false;
static uint8_t g_oledAddr = 0x3C;
static uint8_t g_oledSda = 5;
static uint8_t g_oledScl = 6;

struct I2cPins
{
    uint8_t sda;
    uint8_t scl;
};

static const I2cPins kOledCandidates[] = {
    {5, 6},
    {6, 5},
    {3, 4},
    {4, 3},
    {0, 1},
    {1, 0},
};

static void i2cDelay()
{
    delayMicroseconds(5);
}

static void driveLow(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

static void releasePin(uint8_t pin)
{
    pinMode(pin, INPUT_PULLUP);
}

static bool readPinHigh(uint8_t pin)
{
    return digitalRead(pin) != LOW;
}

static bool bitbangI2cProbe(uint8_t sda, uint8_t scl, uint8_t address)
{
    releasePin(sda);
    releasePin(scl);
    i2cDelay();
    if (!readPinHigh(sda) || !readPinHigh(scl))
        return false;

    driveLow(sda);
    i2cDelay();
    driveLow(scl);

    uint8_t value = address << 1;
    for (uint8_t mask = 0x80; mask; mask >>= 1)
    {
        if (value & mask)
            releasePin(sda);
        else
            driveLow(sda);

        i2cDelay();
        releasePin(scl);
        i2cDelay();
        driveLow(scl);
        i2cDelay();
    }

    releasePin(sda);
    i2cDelay();
    releasePin(scl);
    i2cDelay();
    bool ack = !readPinHigh(sda);
    driveLow(scl);
    i2cDelay();

    driveLow(sda);
    i2cDelay();
    releasePin(scl);
    i2cDelay();
    releasePin(sda);
    i2cDelay();
    return ack;
}

static bool bitbangI2cWriteByte(uint8_t sda, uint8_t scl, uint8_t value)
{
    for (uint8_t mask = 0x80; mask; mask >>= 1)
    {
        if (value & mask)
            releasePin(sda);
        else
            driveLow(sda);

        i2cDelay();
        releasePin(scl);
        i2cDelay();
        driveLow(scl);
        i2cDelay();
    }

    releasePin(sda);
    i2cDelay();
    releasePin(scl);
    i2cDelay();
    bool ack = !readPinHigh(sda);
    driveLow(scl);
    i2cDelay();
    return ack;
}

static bool rawOledCommand(uint8_t command)
{
    releasePin(g_oledSda);
    releasePin(g_oledScl);
    i2cDelay();
    driveLow(g_oledSda);
    i2cDelay();
    driveLow(g_oledScl);

    bool ok = bitbangI2cWriteByte(g_oledSda, g_oledScl, g_oledAddr << 1);
    ok = bitbangI2cWriteByte(g_oledSda, g_oledScl, 0x00) && ok;
    ok = bitbangI2cWriteByte(g_oledSda, g_oledScl, command) && ok;

    driveLow(g_oledSda);
    i2cDelay();
    releasePin(g_oledScl);
    i2cDelay();
    releasePin(g_oledSda);
    i2cDelay();
    return ok;
}

static void detectOledPins()
{
    Serial.println("OLED: scanning candidate pins");

    for (const I2cPins &pins : kOledCandidates)
    {
        bool found3c = bitbangI2cProbe(pins.sda, pins.scl, 0x3C);
        bool found3d = bitbangI2cProbe(pins.sda, pins.scl, 0x3D);
        Serial.printf("OLED scan SDA=%u SCL=%u -> 0x3C=%d 0x3D=%d\n",
                      pins.sda,
                      pins.scl,
                      found3c ? 1 : 0,
                      found3d ? 1 : 0);

        if (found3c || found3d)
        {
            g_oledSda = pins.sda;
            g_oledScl = pins.scl;
            g_oledAddr = found3c ? 0x3C : 0x3D;
            return;
        }
    }

    Serial.println("OLED: no ACK found, falling back to SDA=5 SCL=6 addr 0x3C");
}

static void beginOled()
{
    delete g_oled;
    g_oled = new U8G2_SSD1306_72X40_ER_F_SW_I2C(U8G2_R0, g_oledScl, g_oledSda, U8X8_PIN_NONE);
    g_oled->setI2CAddress(g_oledAddr << 1);
    g_oled->begin();
    g_oled->setPowerSave(0);
    g_oled->setContrast(255);
    g_oled->setFont(u8g2_font_5x7_tf);
}

static void drawMessage(const char *line1, const char *line2, const char *line3)
{
    if (!g_oled)
        return;

    g_oled->clearBuffer();
    if (line1)
        g_oled->drawStr(0, 8, line1);
    if (line2)
        g_oled->drawStr(0, 18, line2);
    if (line3)
        g_oled->drawStr(0, 28, line3);
    g_oled->sendBuffer();
}

void displayBegin()
{
    Serial.println("OLED: init");
    detectOledPins();
    beginOled();
    g_oledReady = true;
    displayMessage("VARIO ESP32", "OLED OK", "BOOT...");
    Serial.printf("OLED: ok SSD1306 addr 0x%02X SDA=%u SCL=%u\n", g_oledAddr, g_oledSda, g_oledScl);
}

void displayMessage(const char *line1, const char *line2, const char *line3)
{
    if (!g_oledReady)
        return;

    drawMessage(line1, line2, line3);
}

void displayUpdate(float altitude, float vario)
{
    static unsigned long last = 0;
    if (!g_oledReady)
        return;

    if (millis() - last < 500)
        return;

    last = millis();

    int bat = batteryPercentGet();
    float volts = batteryVoltageGet();
    int fill = map(bat, 0, 100, 0, 58);
    if (fill < 0)
        fill = 0;
    if (fill > 58)
        fill = 58;

    char percent[8];
    snprintf(percent, sizeof(percent), "%d%%", bat);

    char voltage[16];
    snprintf(voltage, sizeof(voltage), "%.2fV", volts);

    if (!g_oled)
        return;

    g_oled->clearBuffer();
    g_oled->setFont(u8g2_font_logisoso16_tf);
    g_oled->drawStr(0, 18, percent);

    g_oled->setFont(u8g2_font_5x7_tf);
    g_oled->drawStr(48, 8, "BAT");
    g_oled->drawStr(48, 18, voltage);

    g_oled->drawFrame(0, 25, 64, 11);
    g_oled->drawBox(2, 27, fill, 7);
    g_oled->drawBox(65, 28, 3, 5);
    g_oled->sendBuffer();
}
