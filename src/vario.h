#pragma once

bool varioBegin(float pressaoInicial_hPa);

void varioUpdate(float pressao_hPa);

void varioSetQnh(float qnh_hPa);
void varioSetAltitudeOffset(float offset_m);
void varioSetSensitivity(float sensitivity);
void varioSetDeadZone(float deadZone_mps);
void varioSetResponseFast(bool fast);
void varioSetAdaptiveFilter(bool enabled);
void varioAutoCalibrate(float pressao_hPa);

float varioGet();

float altitudeGet();

float pressureGet();
