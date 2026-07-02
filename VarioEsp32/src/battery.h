#pragma once

void batteryBegin(int adcPin, float divider, float minV, float maxV);
void batteryUpdate();

float batteryVoltageGet();
int batteryPercentGet();
bool batteryLowGet();
