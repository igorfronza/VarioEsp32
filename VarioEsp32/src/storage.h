#pragma once

#include <Arduino.h>

struct VarioConfig
{
	float qnh_hPa;
	float altitudeOffset_m;
	float sensitivity;
	float deadZone_mps;
	bool fastResponse;
	bool adaptiveFilter;
	bool autoCalibrateOnBoot;
	float climbStart_mps;
	float climbStop_mps;
	float sinkStart_mps;
	float sinkStop_mps;
	int climbBaseHz;
	int climbGainHzPerMps;
	int sinkBaseHz;
	int volumePct;
	bool sinkToneEnabled;
	bool bleEnabled;
	String bleName;
	bool wifiStaMode;
	String staSsid;
	String staPassword;
	int batteryAdcPin;
	float batteryDivider;
	float batteryMinV;
	float batteryMaxV;
};

bool storageBegin();
bool storageSave();
void storageLoad();
void storageResetToDefaults();

const VarioConfig &storageGetConfig();
void storageSetConfig(const VarioConfig &cfg);
