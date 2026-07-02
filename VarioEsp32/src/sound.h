#pragma once

struct SoundConfig
{
	float climbStart_mps;
	float climbStop_mps;
	float sinkStart_mps;
	float sinkStop_mps;
	int climbBaseHz;
	int climbGainHzPerMps;
	int sinkBaseHz;
	int volumePct;
	bool sinkToneEnabled;
};

void soundBegin(int gpio);
void soundSetConfig(const SoundConfig &cfg);

void soundUpdate(float vario);
