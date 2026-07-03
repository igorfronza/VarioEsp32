#include "storage.h"

#include <Preferences.h>

static Preferences g_prefs;
static bool g_storageReady = false;

static const char *NAMESPACE_NAME = "vario";
static const uint32_t STORAGE_VERSION = 5;

static const VarioConfig DEFAULT_CFG = {
    1013.25f,
    0.0f,
    1.0f,
    0.10f,
    true,
    true,
    false,
    0.10f,
    0.05f,
    -0.22f,
    -0.10f,
    1100,
    280,
    240,
    70,
    true,
    true,
    "VarioESP32",
    false,
    "",
    "",
    0,
    2.0f,
    3.30f,
    4.20f};

static VarioConfig g_cfg = DEFAULT_CFG;

static float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static int clampi(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static void normalizeConfig(VarioConfig &cfg)
{
    cfg.qnh_hPa = clampf(cfg.qnh_hPa, 850.0f, 1100.0f);
    cfg.altitudeOffset_m = clampf(cfg.altitudeOffset_m, -3000.0f, 3000.0f);
    cfg.sensitivity = clampf(cfg.sensitivity, 0.2f, 4.0f);
    cfg.deadZone_mps = clampf(cfg.deadZone_mps, 0.0f, 2.0f);
    cfg.climbStart_mps = clampf(cfg.climbStart_mps, 0.08f, 5.0f);
    cfg.climbStop_mps = clampf(cfg.climbStop_mps, 0.02f, cfg.climbStart_mps);
    cfg.sinkStart_mps = clampf(cfg.sinkStart_mps, -8.0f, -0.18f);
    cfg.sinkStop_mps = clampf(cfg.sinkStop_mps, cfg.sinkStart_mps, -0.01f);
    cfg.climbBaseHz = clampi(cfg.climbBaseHz, 400, 2500);
    cfg.climbGainHzPerMps = clampi(cfg.climbGainHzPerMps, 50, 1000);
    cfg.sinkBaseHz = clampi(cfg.sinkBaseHz, 150, 1200);
    cfg.volumePct = clampi(cfg.volumePct, 0, 100);
    cfg.batteryAdcPin = clampi(cfg.batteryAdcPin, 0, 10);
    cfg.batteryDivider = clampf(cfg.batteryDivider, 1.0f, 8.0f);
    cfg.batteryMinV = clampf(cfg.batteryMinV, 2.5f, 4.0f);
    cfg.batteryMaxV = clampf(cfg.batteryMaxV, cfg.batteryMinV + 0.1f, 4.5f);
    if (cfg.bleName.length() == 0)
        cfg.bleName = "VarioESP32";
}

bool storageBegin()
{
    g_storageReady = g_prefs.begin(NAMESPACE_NAME, false);
    return g_storageReady;
}

void storageLoad()
{
    if (!g_storageReady)
        return;

    uint32_t version = g_prefs.getUInt("ver", 0);
    if (version != STORAGE_VERSION)
    {
        storageSave();
        return;
    }

    g_cfg.qnh_hPa = g_prefs.getFloat("qnh", g_cfg.qnh_hPa);
    g_cfg.altitudeOffset_m = g_prefs.getFloat("alt_off", g_cfg.altitudeOffset_m);
    g_cfg.sensitivity = g_prefs.getFloat("sens", g_cfg.sensitivity);
    g_cfg.deadZone_mps = g_prefs.getFloat("dead", g_cfg.deadZone_mps);
    g_cfg.fastResponse = g_prefs.getBool("fast", g_cfg.fastResponse);
    g_cfg.adaptiveFilter = g_prefs.getBool("adapt", g_cfg.adaptiveFilter);
    g_cfg.autoCalibrateOnBoot = g_prefs.getBool("acal", g_cfg.autoCalibrateOnBoot);
    g_cfg.climbStart_mps = g_prefs.getFloat("cst", g_cfg.climbStart_mps);
    g_cfg.climbStop_mps = g_prefs.getFloat("csp", g_cfg.climbStop_mps);
    g_cfg.sinkStart_mps = g_prefs.getFloat("sst", g_cfg.sinkStart_mps);
    g_cfg.sinkStop_mps = g_prefs.getFloat("ssp", g_cfg.sinkStop_mps);
    g_cfg.climbBaseHz = g_prefs.getInt("cbh", g_cfg.climbBaseHz);
    g_cfg.climbGainHzPerMps = g_prefs.getInt("cgh", g_cfg.climbGainHzPerMps);
    g_cfg.sinkBaseHz = g_prefs.getInt("sbh", g_cfg.sinkBaseHz);
    g_cfg.volumePct = g_prefs.getInt("vol", g_cfg.volumePct);
    g_cfg.sinkToneEnabled = g_prefs.getBool("sink", g_cfg.sinkToneEnabled);
    g_cfg.bleEnabled = g_prefs.getBool("ble", g_cfg.bleEnabled);
    g_cfg.bleName = g_prefs.getString("blenm", g_cfg.bleName);
    g_cfg.wifiStaMode = g_prefs.getBool("wsta", g_cfg.wifiStaMode);
    g_cfg.staSsid = g_prefs.getString("ssid", g_cfg.staSsid);
    g_cfg.staPassword = g_prefs.getString("spass", g_cfg.staPassword);
    g_cfg.batteryAdcPin = g_prefs.getInt("bpin", g_cfg.batteryAdcPin);
    g_cfg.batteryDivider = g_prefs.getFloat("bdiv", g_cfg.batteryDivider);
    g_cfg.batteryMinV = g_prefs.getFloat("bmin", g_cfg.batteryMinV);
    g_cfg.batteryMaxV = g_prefs.getFloat("bmax", g_cfg.batteryMaxV);

    normalizeConfig(g_cfg);
}

void storageResetToDefaults()
{
    g_cfg = DEFAULT_CFG;
    normalizeConfig(g_cfg);
}

bool storageSave()
{
    if (!g_storageReady)
        return false;

    normalizeConfig(g_cfg);

    bool ok = true;
    ok = ok && g_prefs.putUInt("ver", STORAGE_VERSION) > 0;
    ok = ok && g_prefs.putFloat("qnh", g_cfg.qnh_hPa) > 0;
    ok = ok && g_prefs.putFloat("alt_off", g_cfg.altitudeOffset_m) > 0;
    ok = ok && g_prefs.putFloat("sens", g_cfg.sensitivity) > 0;
    ok = ok && g_prefs.putFloat("dead", g_cfg.deadZone_mps) > 0;
    ok = ok && g_prefs.putBool("fast", g_cfg.fastResponse) > 0;
    ok = ok && g_prefs.putBool("adapt", g_cfg.adaptiveFilter) > 0;
    ok = ok && g_prefs.putBool("acal", g_cfg.autoCalibrateOnBoot) > 0;
    ok = ok && g_prefs.putFloat("cst", g_cfg.climbStart_mps) > 0;
    ok = ok && g_prefs.putFloat("csp", g_cfg.climbStop_mps) > 0;
    ok = ok && g_prefs.putFloat("sst", g_cfg.sinkStart_mps) > 0;
    ok = ok && g_prefs.putFloat("ssp", g_cfg.sinkStop_mps) > 0;
    ok = ok && g_prefs.putInt("cbh", g_cfg.climbBaseHz) > 0;
    ok = ok && g_prefs.putInt("cgh", g_cfg.climbGainHzPerMps) > 0;
    ok = ok && g_prefs.putInt("sbh", g_cfg.sinkBaseHz) > 0;
    ok = ok && g_prefs.putInt("vol", g_cfg.volumePct) > 0;
    ok = ok && g_prefs.putBool("sink", g_cfg.sinkToneEnabled) > 0;
    ok = ok && g_prefs.putBool("ble", g_cfg.bleEnabled) > 0;
    ok = ok && g_prefs.putInt("bpin", g_cfg.batteryAdcPin) > 0;
    ok = ok && g_prefs.putFloat("bdiv", g_cfg.batteryDivider) > 0;
    ok = ok && g_prefs.putFloat("bmin", g_cfg.batteryMinV) > 0;
    ok = ok && g_prefs.putFloat("bmax", g_cfg.batteryMaxV) > 0;
    ok = ok && g_prefs.putBool("wsta", g_cfg.wifiStaMode) > 0;
    g_prefs.putString("blenm", g_cfg.bleName);
    g_prefs.putString("ssid", g_cfg.staSsid);
    g_prefs.putString("spass", g_cfg.staPassword);
    return ok;
}

const VarioConfig &storageGetConfig()
{
    return g_cfg;
}

void storageSetConfig(const VarioConfig &cfg)
{
    g_cfg = cfg;
    normalizeConfig(g_cfg);
}
