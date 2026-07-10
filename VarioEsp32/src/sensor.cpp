#include "sensor.h"

#include <Wire.h>
#include <Adafruit_BME280.h>

#define SDA_PIN 8
#define SCL_PIN 9

Adafruit_BME280 bme;

bool sensorBegin()
{
    Wire.begin(SDA_PIN, SCL_PIN);

    return bme.begin(0x76) || bme.begin(0x77);
}

float sensorAltitude()
{
    return bme.readAltitude(1013.25);
}

float sensorPressure()
{
    return bme.readPressure() / 100.0;
}

float sensorTemperature()
{
    return bme.readTemperature();
}

float sensorHumidity()
{
    return bme.readHumidity();
}
