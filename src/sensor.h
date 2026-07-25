#pragma once

bool sensorBegin();

// Configura oversampling x1 (pressão e temp) para ~25Hz.
// Deve ser chamada após sensorBegin().
void sensorSetFastMode();

float sensorAltitude();

float sensorPressure();

float sensorTemperature();

float sensorHumidity();