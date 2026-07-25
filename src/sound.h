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
void soundSetBootMuteMs(unsigned long muteMs);
void soundSetConfig(const SoundConfig &cfg);
void soundSetVolume(int pct);
void soundTriggerClimbTest(float climbMps, unsigned long durationMs);
void soundTriggerSinkTest(unsigned long durationMs);

// Slider da interface web: define vario simulado (-7..+7 m/s, 0 = desliga).
// O valor persiste por 600ms após o último comando (tempo para próximo slide).
void soundSetSimulatedVario(float mps);

void soundUpdate(float vario);
