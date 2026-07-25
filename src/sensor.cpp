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

void sensorSetFastMode()
{
    // Forced mode, x1 oversampling (pressão e temp), sem umidade.
    // ~26 leituras/segundo.
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // pressão
                    Adafruit_BME280::SAMPLING_X1,   // temperatura
                    Adafruit_BME280::SAMPLING_NONE, // umidade
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_0_5);
}

float sensorAltitude()
{
    bme.takeForcedMeasurement();
    return bme.readAltitude(1013.25);
}

float sensorPressure()
{
    bme.takeForcedMeasurement();
    return bme.readPressure() / 100.0;
}

float sensorTemperature()
{
    bme.takeForcedMeasurement();
    return bme.readTemperature();
}

float sensorHumidity()
{
    bme.takeForcedMeasurement();
    return bme.readHumidity();
}
