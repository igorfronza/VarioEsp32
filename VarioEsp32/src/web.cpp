#include <Arduino.h>

#include <WebServer.h>
#include <WiFi.h>

#include "battery.h"
#include "ble.h"
#include "sound.h"
#include "storage.h"
#include "vario.h"
#include "web.h"

static WebServer g_server(80);
static bool g_webReady = false;
static bool g_usingSta = false;

static const char *AP_SSID = "VarioESP32";
static const char *AP_PASSWORD = "12345678";

static void applyConfig(const VarioConfig &cfg)
{
        SoundConfig snd = {
            cfg.climbStart_mps,
            cfg.climbStop_mps,
            cfg.sinkStart_mps,
            cfg.sinkStop_mps,
            cfg.climbBaseHz,
            cfg.climbGainHzPerMps,
            cfg.sinkBaseHz,
            cfg.volumePct,
            cfg.sinkToneEnabled};

        varioSetQnh(cfg.qnh_hPa);
        varioSetAltitudeOffset(cfg.altitudeOffset_m);
        varioSetSensitivity(cfg.sensitivity);
        varioSetDeadZone(cfg.deadZone_mps);
        varioSetResponseFast(cfg.fastResponse);
        varioSetAdaptiveFilter(cfg.adaptiveFilter);
        soundSetConfig(snd);
        batteryBegin(cfg.batteryAdcPin, cfg.batteryDivider, cfg.batteryMinV, cfg.batteryMaxV);
}

static String configJson(const VarioConfig &cfg)
{
        String json = "{";
        json += "\"qnh_hPa\":" + String(cfg.qnh_hPa, 2);
        json += ",\"altitudeOffset_m\":" + String(cfg.altitudeOffset_m, 1);
        json += ",\"sensitivity\":" + String(cfg.sensitivity, 2);
        json += ",\"deadZone_mps\":" + String(cfg.deadZone_mps, 2);
        json += ",\"fastResponse\":" + String(cfg.fastResponse ? "true" : "false");
        json += ",\"adaptiveFilter\":" + String(cfg.adaptiveFilter ? "true" : "false");
        json += ",\"autoCalibrateOnBoot\":" + String(cfg.autoCalibrateOnBoot ? "true" : "false");
        json += ",\"climbStart_mps\":" + String(cfg.climbStart_mps, 2);
        json += ",\"climbStop_mps\":" + String(cfg.climbStop_mps, 2);
        json += ",\"sinkStart_mps\":" + String(cfg.sinkStart_mps, 2);
        json += ",\"sinkStop_mps\":" + String(cfg.sinkStop_mps, 2);
        json += ",\"climbBaseHz\":" + String(cfg.climbBaseHz);
        json += ",\"climbGainHzPerMps\":" + String(cfg.climbGainHzPerMps);
        json += ",\"sinkBaseHz\":" + String(cfg.sinkBaseHz);
        json += ",\"volumePct\":" + String(cfg.volumePct);
        json += ",\"sinkToneEnabled\":" + String(cfg.sinkToneEnabled ? "true" : "false");
        json += ",\"bleEnabled\":" + String(cfg.bleEnabled ? "true" : "false");
        json += ",\"bleName\":\"" + cfg.bleName + "\"";
        json += ",\"wifiStaMode\":" + String(cfg.wifiStaMode ? "true" : "false");
        json += ",\"staSsid\":\"" + cfg.staSsid + "\"";
        json += ",\"batteryAdcPin\":" + String(cfg.batteryAdcPin);
        json += ",\"batteryDivider\":" + String(cfg.batteryDivider, 2);
        json += ",\"batteryMinV\":" + String(cfg.batteryMinV, 2);
        json += ",\"batteryMaxV\":" + String(cfg.batteryMaxV, 2);
        json += "}";
        return json;
}

static void updateConfigFromForm(VarioConfig &cfg)
{
        if (g_server.hasArg("qnh"))
                cfg.qnh_hPa = g_server.arg("qnh").toFloat();
        if (g_server.hasArg("offset"))
                cfg.altitudeOffset_m = g_server.arg("offset").toFloat();
        if (g_server.hasArg("sens"))
                cfg.sensitivity = g_server.arg("sens").toFloat();
        if (g_server.hasArg("dead"))
                cfg.deadZone_mps = g_server.arg("dead").toFloat();
        if (g_server.hasArg("fast"))
                cfg.fastResponse = g_server.arg("fast") == "1";
        if (g_server.hasArg("adapt"))
                cfg.adaptiveFilter = g_server.arg("adapt") == "1";
        if (g_server.hasArg("autozero"))
                cfg.autoCalibrateOnBoot = g_server.arg("autozero") == "1";
        if (g_server.hasArg("cstart"))
                cfg.climbStart_mps = g_server.arg("cstart").toFloat();
        if (g_server.hasArg("cstop"))
                cfg.climbStop_mps = g_server.arg("cstop").toFloat();
        if (g_server.hasArg("sstart"))
                cfg.sinkStart_mps = g_server.arg("sstart").toFloat();
        if (g_server.hasArg("sstop"))
                cfg.sinkStop_mps = g_server.arg("sstop").toFloat();
        if (g_server.hasArg("cbase"))
                cfg.climbBaseHz = g_server.arg("cbase").toInt();
        if (g_server.hasArg("cgain"))
                cfg.climbGainHzPerMps = g_server.arg("cgain").toInt();
        if (g_server.hasArg("sbase"))
                cfg.sinkBaseHz = g_server.arg("sbase").toInt();
        if (g_server.hasArg("vol"))
                cfg.volumePct = g_server.arg("vol").toInt();
        if (g_server.hasArg("sink_en"))
                cfg.sinkToneEnabled = g_server.arg("sink_en") == "1";
        if (g_server.hasArg("ble_en"))
                cfg.bleEnabled = g_server.arg("ble_en") == "1";
        if (g_server.hasArg("ble_name"))
                cfg.bleName = g_server.arg("ble_name");
        if (g_server.hasArg("wifi_mode"))
                cfg.wifiStaMode = g_server.arg("wifi_mode") == "sta";
        if (g_server.hasArg("sta_ssid"))
                cfg.staSsid = g_server.arg("sta_ssid");
        if (g_server.hasArg("sta_password"))
                cfg.staPassword = g_server.arg("sta_password");
        if (g_server.hasArg("bpin"))
                cfg.batteryAdcPin = g_server.arg("bpin").toInt();
        if (g_server.hasArg("bdiv"))
                cfg.batteryDivider = g_server.arg("bdiv").toFloat();
        if (g_server.hasArg("bmin"))
                cfg.batteryMinV = g_server.arg("bmin").toFloat();
        if (g_server.hasArg("bmax"))
                cfg.batteryMaxV = g_server.arg("bmax").toFloat();
}

static String htmlPage()
{
        String html = R"HTML(
<!doctype html>
<html lang="pt-br">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
                <title>Vario ESP32</title>
    <style>
                        :root { --bg:#f5f7f8; --panel:#ffffff; --ink:#172026; --muted:#5d6872; --line:#d7dee4; --sea:#0f766e; --blue:#2563eb; --amber:#b45309; --rose:#b91c1c; }
                        * { box-sizing:border-box; }
                        body { margin:0; padding:16px; background:var(--bg); color:var(--ink); font-family:system-ui,-apple-system,Segoe UI,sans-serif; }
                        .wrap { max-width:980px; margin:0 auto; display:grid; gap:16px; }
                        .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:18px; box-shadow:0 8px 18px rgba(20,35,50,.06); }
                        .hero { display:grid; gap:10px; }
                        .eyebrow { display:inline-block; width:max-content; padding:5px 10px; border-radius:6px; background:#e3f5f2; color:#0c5d57; font-size:.82rem; }
                        h1,h2 { margin:0; }
                        p { margin:0; }
                        .muted { color:var(--muted); }
                        .grid { display:grid; gap:12px; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); }
                        .metric { border:1px solid var(--line); border-radius:8px; padding:12px; background:#f9fbfc; }
                        .metric .label { color:var(--muted); font-size:.9rem; }
                        .metric strong { display:block; margin-top:6px; font-size:1.45rem; }
                        form { display:grid; gap:14px; }
                        .fields { display:grid; gap:12px; grid-template-columns:repeat(auto-fit,minmax(230px,1fr)); }
                        label { display:grid; gap:6px; font-size:.95rem; }
                        label span:first-child { font-weight:650; }
                        .help { color:var(--muted); font-size:.84rem; line-height:1.35; }
                        .section-title { margin-top:8px; padding-top:14px; border-top:1px solid var(--line); color:#24313a; font-size:1rem; }
                        .hint { border-left:4px solid var(--blue); background:#eef5ff; padding:10px 12px; border-radius:6px; color:#24313a; line-height:1.4; }
                        input, select, button { font:inherit; }
                        input, select { width:100%; padding:10px 11px; border-radius:6px; border:1px solid #c9d2db; background:#fff; color:var(--ink); }
                        .actions { display:flex; flex-wrap:wrap; gap:10px; }
                        button { border:0; border-radius:6px; padding:11px 16px; cursor:pointer; color:#fff; background:var(--sea); }
                        button.alt { background:var(--amber); }
                        button.warn { background:var(--rose); }
                        .status { min-height:1.2rem; color:var(--muted); }
                        .two { display:grid; gap:16px; grid-template-columns:1fr; }
    </style>
</head>
<body>
        <div class="wrap">
                <section class="panel hero">
                        <span class="eyebrow" id="mode-chip">Modo de rede</span>
                        <h1>Vario ESP32</h1>
                        <p class="muted">Painel local para diagnostico e configuracao.</p>
                        <p>Endereco atual <strong id="ip-now">--</strong></p>
                </section>

                <section class="panel">
                        <h2>Diagnostico ao vivo</h2>
                        <div class="grid" style="margin-top:12px;">
                                <div class="metric"><span class="label">Pressao</span><strong id="pressure">--</strong></div>
                                <div class="metric"><span class="label">Altitude</span><strong id="altitude">--</strong></div>
                                <div class="metric"><span class="label">Vario</span><strong id="vario">--</strong></div>
                                <div class="metric"><span class="label">Sinal Wi-Fi</span><strong id="rssi">--</strong></div>
                                <div class="metric"><span class="label">Bateria</span><strong id="bat">--</strong></div>
                                <div class="metric"><span class="label">BLE</span><strong id="ble">--</strong></div>
                                <div class="metric"><span class="label">Uptime</span><strong id="uptime">--</strong></div>
                        </div>
                </section>

                <section class="two">
                        <section class="panel">
                                <h2>Configuracao</h2>
                                <form id="config-form" style="margin-top:12px;">
                                        <div class="fields">
                                                <div class="hint" style="grid-column:1/-1;">Para resposta rapida sem ficar apitando parado: use Resposta = Rapida, Filtro adaptativo = Ligado, Zona morta em torno de 0.15 e Limiar de subida em torno de 0.30 m/s.</div>

                                                <h3 class="section-title" style="grid-column:1/-1;">Voo e altitude</h3>
                                                <label><span>QNH (hPa)</span><input name="qnh" type="number" step="0.01"><small class="help">Pressao ao nivel do mar usada para calcular altitude. Exemplo: se o app/metar local mostra 1015.20 hPa, coloque 1015.20.</small></label>
                                                <label><span>Offset de altitude (m)</span><input name="offset" type="number" step="0.1"><small class="help">Soma ou subtrai metros da altitude indicada. Exemplo: se marca 20 m a menos, use +20.</small></label>
                                                <label><span>Sensibilidade do vario</span><input name="sens" type="number" step="0.01"><small class="help">Multiplica a velocidade vertical calculada. Valor maior reage mais forte e pode apitar mais cedo, mas tambem aumenta ruido.</small></label>
                                                <label><span>Zona morta (m/s)</span><input name="dead" type="number" step="0.01"><small class="help">Movimentos menores que isso viram zero. Menor = mais sensivel, mas pode apitar parado. Sugestao: 0.15.</small></label>
                                                <label><span>Resposta do filtro</span><select name="fast"><option value="1">Rapida</option><option value="0">Lenta</option></select><small class="help">Rapida apita antes quando comeca a subir. Lenta fica mais suave, mas atrasa a resposta.</small></label>
                                                <label><span>Filtro adaptativo</span><select name="adapt"><option value="1">Ligado</option><option value="0">Desligado</option></select><small class="help">Quando ligado, filtra mais parado e responde mais rapido quando detecta movimento real.</small></label>
                                                <label><span>Autozero no boot</span><select name="autozero"><option value="1">Ligado</option><option value="0">Desligado</option></select><small class="help">Zera a altitude ao ligar. Bom para voo local; desligue se quer altitude baseada no QNH.</small></label>

                                                <h3 class="section-title" style="grid-column:1/-1;">Limiar dos apitos estilo FlySkyHy</h3>
                                                <label><span>Limiar de subida (m/s)</span><input name="cstart" type="number" step="0.01"><small class="help">Comeca o beep de subida quando o vario passa deste valor. Menor = apita mais cedo. Sugestao: 0.30.</small></label>
                                                <label><span>Parar beep de subida (m/s)</span><input name="cstop" type="number" step="0.01"><small class="help">Para o beep quando a subida cai abaixo deste valor. Deve ser menor que o limiar de subida. Sugestao: 0.12.</small></label>
                                                <label><span>Limiar de descendente (m/s)</span><input name="sstart" type="number" step="0.01"><small class="help">Comeca o som de descida quando o vario fica mais negativo que isso. Exemplo: -0.80 apita so em afundamento claro.</small></label>
                                                <label><span>Parar som de descida (m/s)</span><input name="sstop" type="number" step="0.01"><small class="help">Para o som de descida quando melhora acima deste valor. Exemplo: -0.30.</small></label>

                                                <h3 class="section-title" style="grid-column:1/-1;">Som do buzzer</h3>
                                                <label><span>Volume buzzer (%)</span><input name="vol" type="number" min="0" max="100" step="1"><small class="help">Intensidade desejada do buzzer. Em buzzer passivo simples pode ter pouco efeito.</small></label>
                                                <label><span>Tom base de subida (Hz)</span><input name="cbase" type="number" step="1"><small class="help">Frequencia inicial do beep de subida. Valor maior deixa o beep mais agudo.</small></label>
                                                <label><span>Ganho do tom de subida (Hz por m/s)</span><input name="cgain" type="number" step="1"><small class="help">Este e o parametro que faz o beep ficar mais agudo quanto maior a subida. Maior = muda mais rapido para agudo.</small></label>
                                                <label><span>Tom fixo de descida (Hz)</span><input name="sbase" type="number" step="1"><small class="help">Frequencia fixa do som bebe-bebe de descida. Valor menor deixa o som mais grave.</small></label>
                                                <label><span>Som de descida</span><select name="sink_en"><option value="1">Ligado</option><option value="0">Desligado</option></select><small class="help">Liga ou desliga o aviso sonoro quando estiver afundando.</small></label>

                                                <h3 class="section-title" style="grid-column:1/-1;">Bateria</h3>
                                                <label><span>Pino ADC bateria</span><input name="bpin" type="number" step="1"><small class="help">GPIO que recebe o meio do divisor resistivo da bateria. Exemplo: GPIO0 se voce ligar o divisor no GPIO0.</small></label>
                                                <label><span>Divisor bateria</span><input name="bdiv" type="number" step="0.01"><small class="help">Fator do divisor. Com 100k em cima e 100k embaixo, use 2.00.</small></label>
                                                <label><span>Bateria minima (V)</span><input name="bmin" type="number" step="0.01"><small class="help">Tensao considerada 0%. Para Li-ion 1S, 3.30 V e conservador.</small></label>
                                                <label><span>Bateria maxima (V)</span><input name="bmax" type="number" step="0.01"><small class="help">Tensao considerada 100%. Para Li-ion 1S carregada, use 4.20 V.</small></label>

                                                <h3 class="section-title" style="grid-column:1/-1;">BLE e Wi-Fi</h3>
                                                <label><span>BLE</span><select name="ble_en"><option value="1">Ligado</option><option value="0">Desligado</option></select><small class="help">Envia dados para apps compatíveis por Bluetooth Low Energy.</small></label>
                                                <label><span>Nome BLE</span><input name="ble_name" type="text" maxlength="20"><small class="help">Nome que aparece no celular ao procurar o variometro.</small></label>
                                                <label><span>Modo Wi-Fi</span><select name="wifi_mode"><option value="ap">AP (rede propria)</option><option value="sta">STA (entrar na sua rede)</option></select><small class="help">AP cria a rede VarioESP32. STA conecta na sua rede Wi-Fi.</small></label>
                                                <label><span>Wi-Fi SSID (STA)</span><input name="sta_ssid" type="text" maxlength="32"><small class="help">Nome da rede Wi-Fi usada no modo STA.</small></label>
                                                <label><span>Wi-Fi Senha (STA)</span><input name="sta_password" type="password" maxlength="64"><small class="help">Senha da rede Wi-Fi. Deixe em branco se nao for usar STA.</small></label>
                                        </div>
                                        <div class="actions">
                                                <button type="submit">Aplicar e salvar</button>
                                                <button class="alt" type="button" id="defaults-btn">Restaurar padrao</button>
                                                <button class="warn" type="button" id="restart-btn">Reiniciar</button>
                                        </div>
                                        <div class="status" id="save-status"></div>
                                </form>
                        </section>
                </section>
        </div>

        <script>
                const form = document.getElementById('config-form');
                const saveStatus = document.getElementById('save-status');

                function formatSeconds(total) {
                        const s = Math.floor(total % 60);
                        const m = Math.floor((total / 60) % 60);
                        const h = Math.floor(total / 3600);
                        return `${h}h ${m}m ${s}s`;
                }

                function setConfigForm(cfg) {
                        form.qnh.value = cfg.qnh_hPa.toFixed(2);
                        form.offset.value = cfg.altitudeOffset_m.toFixed(1);
                        form.sens.value = cfg.sensitivity.toFixed(2);
                        form.dead.value = cfg.deadZone_mps.toFixed(2);
                        form.fast.value = cfg.fastResponse ? '1' : '0';
                        form.adapt.value = cfg.adaptiveFilter ? '1' : '0';
                        form.autozero.value = cfg.autoCalibrateOnBoot ? '1' : '0';
                        form.cstart.value = Number(cfg.climbStart_mps).toFixed(2);
                        form.cstop.value = Number(cfg.climbStop_mps).toFixed(2);
                        form.sstart.value = Number(cfg.sinkStart_mps).toFixed(2);
                        form.sstop.value = Number(cfg.sinkStop_mps).toFixed(2);
                        form.cbase.value = cfg.climbBaseHz;
                        form.cgain.value = cfg.climbGainHzPerMps;
                        form.sbase.value = cfg.sinkBaseHz;
                        form.vol.value = cfg.volumePct;
                        form.sink_en.value = cfg.sinkToneEnabled ? '1' : '0';
                        form.ble_en.value = cfg.bleEnabled ? '1' : '0';
                        form.ble_name.value = cfg.bleName || '';
                        form.wifi_mode.value = cfg.wifiStaMode ? 'sta' : 'ap';
                        form.sta_ssid.value = cfg.staSsid || '';
                        form.sta_password.value = '';
                        form.bpin.value = cfg.batteryAdcPin;
                        form.bdiv.value = Number(cfg.batteryDivider).toFixed(2);
                        form.bmin.value = Number(cfg.batteryMinV).toFixed(2);
                        form.bmax.value = Number(cfg.batteryMaxV).toFixed(2);
                }

                async function loadConfig() {
                        const res = await fetch('/api/config');
                        const cfg = await res.json();
                        setConfigForm(cfg);
                }

                async function refreshStatus() {
                        try {
                                const res = await fetch('/api/status');
                                const data = await res.json();
                                document.getElementById('pressure').textContent = `${data.pressure_hPa.toFixed(2)} hPa`;
                                document.getElementById('altitude').textContent = `${data.altitude_m.toFixed(1)} m`;
                                document.getElementById('vario').textContent = `${data.vario_mps.toFixed(2)} m/s`;
                                document.getElementById('uptime').textContent = formatSeconds(data.uptime_s);
                                document.getElementById('mode-chip').textContent = data.wifi_mode === 'STA' ? 'Modo STA' : 'Modo AP';
                                document.getElementById('ip-now').textContent = data.ip;
                                document.getElementById('rssi').textContent = data.rssi_dbm <= -1 ? `${data.rssi_dbm} dBm` : 'n/a';
                                document.getElementById('bat').textContent = `${data.battery_pct}% ${data.battery_v.toFixed(2)} V`;
                                document.getElementById('ble').textContent = data.ble_connected ? 'Conectado' : (data.ble_enabled ? 'Aguardando' : 'Desligado');
                        } catch (err) {
                                console.log(err);
                        }
                }

                form.addEventListener('submit', async (event) => {
                        event.preventDefault();
                        saveStatus.textContent = 'Salvando...';
                        const body = new URLSearchParams(new FormData(form));
                        const res = await fetch('/api/config', { method: 'POST', body });
                        const cfg = await res.json();
                        setConfigForm(cfg);
                        saveStatus.textContent = 'Configuracao salva. Reinicie para aplicar modo AP/STA.';
                });

                document.getElementById('defaults-btn').addEventListener('click', async () => {
                        saveStatus.textContent = 'Restaurando defaults...';
                        const res = await fetch('/api/defaults', { method: 'POST' });
                        const cfg = await res.json();
                        setConfigForm(cfg);
                        saveStatus.textContent = 'Defaults restaurados.';
                });

                document.getElementById('restart-btn').addEventListener('click', async () => {
                        saveStatus.textContent = 'Reiniciando dispositivo...';
                        await fetch('/api/restart', { method: 'POST' });
                });

                loadConfig();
                refreshStatus();
                setInterval(refreshStatus, 1000);
        </script>
</body>
</html>
)HTML";
                return html;
}

static void handleRoot()
{
        g_server.send(200, "text/html; charset=utf-8", htmlPage());
}

static void handleStatus()
{
        String json = "{";
        json += "\"pressure_hPa\":" + String(pressureGet(), 2);
        json += ",\"altitude_m\":" + String(altitudeGet(), 2);
        json += ",\"vario_mps\":" + String(varioGet(), 2);
        json += ",\"battery_v\":" + String(batteryVoltageGet(), 2);
        json += ",\"battery_pct\":" + String(batteryPercentGet());
        json += ",\"ble_enabled\":" + String(bleIsEnabled() ? "true" : "false");
        json += ",\"ble_connected\":" + String(bleIsConnected() ? "true" : "false");
        json += ",\"uptime_s\":" + String(millis() / 1000UL);
        json += ",\"wifi_mode\":\"" + String(g_usingSta ? "STA" : "AP") + "\"";
        if (g_usingSta)
        {
                json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
                json += ",\"rssi_dbm\":" + String(WiFi.RSSI());
        }
        else
        {
                json += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
                json += ",\"rssi_dbm\":0";
        }
        json += "}";
        g_server.send(200, "application/json", json);
}

static void handleConfigGet()
{
        g_server.send(200, "application/json", configJson(storageGetConfig()));
}

static void handleConfigSave()
{
        VarioConfig cfg = storageGetConfig();
        updateConfigFromForm(cfg);
        storageSetConfig(cfg);
        applyConfig(storageGetConfig());
        storageSave();
        g_server.send(200, "application/json", configJson(storageGetConfig()));
}

static void handleDefaults()
{
    storageResetToDefaults();
    applyConfig(storageGetConfig());
    storageSave();
    g_server.send(200, "application/json", configJson(storageGetConfig()));
}

static void handleRestart()
{
        g_server.send(200, "text/plain", "Reiniciando");
        delay(100);
        ESP.restart();
}

static bool startSta(const VarioConfig &cfg)
{
        if (cfg.staSsid.length() == 0)
                return false;

        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg.staSsid.c_str(), cfg.staPassword.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < 12000)
                delay(250);

        if (WiFi.status() == WL_CONNECTED)
        {
                g_usingSta = true;
                Serial.println("WiFi STA OK");
                Serial.printf("SSID: %s\n", cfg.staSsid.c_str());
                Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
                return true;
        }

        WiFi.disconnect(true);
        return false;
}

static void startAp()
{
        WiFi.mode(WIFI_AP);
        bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
        g_usingSta = false;

        Serial.printf("WiFi AP %s\n", apOk ? "OK" : "FALHOU");
        Serial.printf("SSID: %s\n", AP_SSID);
        Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void webBegin()
{
        const VarioConfig &cfg = storageGetConfig();
        bool staOk = false;
        if (cfg.wifiStaMode)
                staOk = startSta(cfg);

        if (!staOk)
                startAp();

        g_server.on("/", HTTP_GET, handleRoot);
        g_server.on("/api/status", HTTP_GET, handleStatus);
        g_server.on("/api/config", HTTP_GET, handleConfigGet);
        g_server.on("/api/config", HTTP_POST, handleConfigSave);
        g_server.on("/api/defaults", HTTP_POST, handleDefaults);
        g_server.on("/api/restart", HTTP_POST, handleRestart);
        g_server.begin();

        g_webReady = true;
}

void webUpdate()
{
        if (!g_webReady)
                return;

        g_server.handleClient();
}
