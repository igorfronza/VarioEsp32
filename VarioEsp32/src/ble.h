#pragma once

void bleBegin(bool enabled, const char *deviceName);
void bleUpdate();

bool bleIsEnabled();
bool bleIsConnected();
