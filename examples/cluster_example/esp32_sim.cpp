#include <WiFi.h>
#include <WebServer.h>

#define SDV_TELEMETRY_VERSION 3u
#define SDV_TELEMETRY_MAGIC 0xA5u

#define UART_FRAME_START 0xAAu
#define UART_FRAME_TYPE_TELEMETRY_ALL 0x45u

#define SDV_FAULT_VSS (1u << 0)
#define SDV_FAULT_BME280 (1u << 1)
#define SDV_FAULT_GPS (1u << 2)
#define SDV_GPS_FIX_VALID (1u << 3)

#define SDV_VEH_RAIN (1u << 0)
#define SDV_VEH_DOOR_OPEN (1u << 1)
#define SDV_VEH_DOOR_LOCKED (1u << 2)
#define SDV_VEH_HEADLIGHT (1u << 3)
#define SDV_VEH_WIPER_ON (1u << 4)
#define SDV_VEH_INDICATOR_L (1u << 5)
#define SDV_VEH_INDICATOR_R (1u << 6)
#define SDV_VEH_CRASH (1u << 7)
#define SDV_VEH_AIRBAG (1u << 8)
#define SDV_VEH_SEATBELT_WARN (1u << 9)
#define SDV_VEH_SEAT_OCCUPIED (1u << 10)
#define SDV_VEH_HIGH_SPEED (1u << 11)
#define SDV_VEH_HARSH_BRAKING (1u << 12)
#define SDV_VEH_BUZZER (1u << 13)
#define SDV_VEH_INTERIOR_LIGHT (1u << 14)

#pragma pack(push, 1)
typedef struct
{
    uint16_t resp_max_x10us;
    uint16_t resp_avg_x10us;
    uint16_t exec_count;
    uint16_t deadline_misses;
} sdv_task_stats_t;

typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t seq;
    uint8_t sched_mode;
    uint16_t run_id;
    uint8_t num_tasks;
    uint8_t fault_flags;
    uint32_t uptime_ms;
    uint16_t cpu_load_x100;
    uint16_t total_misses;
    uint16_t veh_flags;
    uint16_t veh_speed_x10;
    int16_t veh_accel_x100;
    uint8_t bcm_wiper_speed;
    uint16_t acm_throttle_x100;
    uint16_t acm_brake_pa;
    uint16_t acm_fsr_raw;
    uint8_t acm_status;
    int16_t seat_position_deg;
    uint8_t seat_profile;
    int16_t env_temp_x10;
    uint16_t env_hum_x100;
    uint32_t env_press_pa;
    int32_t gps_lat_x1e6;
    int32_t gps_lon_x1e6;
    int16_t gps_alt_m;
    uint8_t gps_satellites;
    sdv_task_stats_t task[4];
} sdv_telemetry_t;
#pragma pack(pop)

static_assert(sizeof(sdv_telemetry_t) <= 255, "sdv_telemetry_t exceeds 255 bytes");

sdv_telemetry_t telemetry;
uint8_t frame_seq = 0;
uint32_t start_time;
bool auto_increment = false;
int update_interval_ms = 100;

const char *ssid = "SDV_Telemetry_Simulator";
const char *password = "sdv12345";
WebServer server(80);

static uint16_t crc16_ibm(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000u;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void send_telemetry_frame()
{
    uint8_t header[4];
    header[0] = UART_FRAME_START;
    header[1] = SDV_TELEMETRY_VERSION;
    header[2] = UART_FRAME_TYPE_TELEMETRY_ALL;
    header[3] = (uint8_t)sizeof(sdv_telemetry_t);

    telemetry.magic = SDV_TELEMETRY_MAGIC;
    telemetry.version = SDV_TELEMETRY_VERSION;
    telemetry.seq = frame_seq++;
    telemetry.uptime_ms = millis() - start_time;
    telemetry.run_id++;

    if (auto_increment)
    {
        telemetry.cpu_load_x100 = (uint16_t)(4000 + random(1000));
        telemetry.total_misses += random(2);
    }

    uint16_t crc_header = crc16_ibm(header, sizeof(header));
    uint16_t crc_payload = crc16_ibm((uint8_t *)&telemetry, sizeof(telemetry));
    uint16_t crc = crc_header ^ crc_payload;

    Serial.write(header, sizeof(header));
    Serial.write((uint8_t *)&telemetry, sizeof(telemetry));
    Serial.write((uint8_t)(crc & 0xFF));
    Serial.write((uint8_t)((crc >> 8) & 0xFF));
}

const char *get_html_page()
{
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SDV Telemetry Simulator</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif; background: #0a0e27; color: #e0e0e0; padding: 15px; }
h1 { text-align: center; color: #00d4ff; margin-bottom: 20px; font-size: 1.5em; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 15px; }
.card { background: #141b3d; border-radius: 10px; padding: 15px; border: 1px solid #2a3370; }
.card h2 { color: #00d4ff; font-size: 1.1em; margin-bottom: 10px; border-bottom: 1px solid #2a3370; padding-bottom: 5px; }
.row { display: flex; align-items: center; margin: 8px 0; }
.row label { flex: 1; font-size: 0.9em; }
.row input, .row select { width: 130px; padding: 6px; background: #0d1130; color: #e0e0e0; border: 1px solid #3a4370; border-radius: 5px; font-size: 0.9em; }
.row input[type="range"] { width: 100px; cursor: pointer; }
.row .val { width: 50px; text-align: right; font-size: 0.85em; color: #8ecae6; }
.row input[type="checkbox"] { width: auto; margin-left: auto; }
.control-bar { display: flex; justify-content: center; gap: 10px; margin: 15px 0; flex-wrap: wrap; }
.btn { padding: 10px 20px; border: none; border-radius: 6px; cursor: pointer; font-size: 0.95em; font-weight: 600; color: white; transition: 0.2s; }
.btn:hover { opacity: 0.85; transform: translateY(-1px); }
.btn-send { background: #00b4d8; }
.btn-auto { background: #06d6a0; }
.btn-auto.stop { background: #ef476f; }
.btn-reset { background: #ffd166; color: #1a1a2e; }
.stats { text-align: center; margin: 10px 0; font-size: 0.9em; color: #8ecae6; }
.stats span { margin: 0 10px; }
@media (max-width: 600px) {
    .row label { font-size: 0.8em; }
    .row input, .row select { width: 100px; }
}
</style>
</head>
<body>
<h1>SDV Telemetry Simulator</h1>

<div class="stats">
    <span>Seq: <b id="seq">0</b></span>
    <span>Frames: <b id="frames">0</b></span>
    <span>Interval: <b id="interval">100ms</b></span>
</div>

<div class="control-bar">
    <button class="btn btn-send" onclick="sendFrame()">Send Single Frame</button>
    <button class="btn btn-auto" id="autoBtn" onclick="toggleAuto()">Auto Send</button>
    <button class="btn btn-reset" onclick="resetAll()">Reset All</button>
</div>

<div class="grid">
    <div class="card">
        <h2>Scheduler & System</h2>
        <div class="row"><label>Mode</label>
            <select id="sched_mode">
                <option value="0">RM</option><option value="1">EDF</option><option value="2">CBS</option>
            </select></div>
        <div class="row"><label>CPU Load (x100)</label>
            <input type="range" id="cpu_load" min="0" max="10000" value="4500" oninput="updateSlider(this)">
            <span class="val" id="cpu_load_val">45%</span></div>
        <div class="row"><label>Total Misses</label>
            <input type="number" id="total_misses" value="0"></div>
        <div class="row"><label>Fault Flags</label>
            <select id="fault_flags">
                <option value="0">None</option><option value="1">VSS</option><option value="2">BME280</option>
                <option value="4">GPS</option><option value="8">GPS Fix Valid</option>
            </select></div>
    </div>

    <div class="card">
        <h2>Vehicle Dynamics</h2>
        <div class="row"><label>Speed (km/h x10)</label>
            <input type="range" id="veh_speed" min="0" max="2500" value="600" oninput="updateSlider(this)">
            <span class="val" id="veh_speed_val">60.0</span></div>
        <div class="row"><label>Accel (m/s² x100)</label>
            <input type="range" id="veh_accel" min="-1500" max="1500" value="0" oninput="updateSlider(this)">
            <span class="val" id="veh_accel_val">0.00</span></div>
        <div class="row"><label>Throttle (x100)</label>
            <input type="range" id="acm_throttle" min="0" max="10000" value="2500" oninput="updateSlider(this)">
            <span class="val" id="acm_throttle_val">25%</span></div>
        <div class="row"><label>Brake (Pa)</label>
            <input type="range" id="acm_brake" min="0" max="20000" value="0" oninput="updateSlider(this)">
            <span class="val" id="acm_brake_val">0</span></div>
    </div>

    <div class="card">
        <h2>Vehicle States</h2>
        <div class="row"><label>Rain</label><input type="checkbox" id="flag_rain" onchange="toggleFlag('RAIN', this)"></div>
        <div class="row"><label>Door Open</label><input type="checkbox" id="flag_door_open" onchange="toggleFlag('DOOR_OPEN', this)"></div>
        <div class="row"><label>Door Locked</label><input type="checkbox" id="flag_door_locked" checked onchange="toggleFlag('DOOR_LOCKED', this)"></div>
        <div class="row"><label>Headlight</label><input type="checkbox" id="flag_headlight" onchange="toggleFlag('HEADLIGHT', this)"></div>
        <div class="row"><label>Wiper On</label><input type="checkbox" id="flag_wiper" onchange="toggleFlag('WIPER_ON', this)"></div>
        <div class="row"><label>Indicator L</label><input type="checkbox" id="flag_ind_l" onchange="toggleFlag('INDICATOR_L', this)"></div>
        <div class="row"><label>Indicator R</label><input type="checkbox" id="flag_ind_r" onchange="toggleFlag('INDICATOR_R', this)"></div>
        <div class="row"><label>Crash</label><input type="checkbox" id="flag_crash" onchange="toggleFlag('CRASH', this)"></div>
        <div class="row"><label>Airbag</label><input type="checkbox" id="flag_airbag" onchange="toggleFlag('AIRBAG', this)"></div>
        <div class="row"><label>Seatbelt Warn</label><input type="checkbox" id="flag_seatbelt" onchange="toggleFlag('SEATBELT_WARN', this)"></div>
        <div class="row"><label>Seat Occupied</label><input type="checkbox" id="flag_seat" checked onchange="toggleFlag('SEAT_OCCUPIED', this)"></div>
        <div class="row"><label>Wiper Speed</label>
            <select id="wiper_speed">
                <option value="0">Off</option><option value="1">Slow</option><option value="2">Fast</option>
            </select></div>
    </div>

    <div class="card">
        <h2>Environment (BME280)</h2>
        <div class="row"><label>Temp (°C x10)</label>
            <input type="range" id="env_temp" min="-400" max="850" value="250" oninput="updateSlider(this)">
            <span class="val" id="env_temp_val">25.0°C</span></div>
        <div class="row"><label>Humidity (% x100)</label>
            <input type="range" id="env_hum" min="0" max="10000" value="5500" oninput="updateSlider(this)">
            <span class="val" id="env_hum_val">55%</span></div>
        <div class="row"><label>Pressure (Pa)</label>
            <input type="number" id="env_press" value="101325"></div>
    </div>

    <div class="card">
        <h2>GPS</h2>
        <div class="row"><label>Lat (x1e6)</label>
            <input type="number" id="gps_lat" value="37422390"></div>
        <div class="row"><label>Lon (x1e6)</label>
            <input type="number" id="gps_lon" value="-12208402"></div>
        <div class="row"><label>Alt (m)</label>
            <input type="number" id="gps_alt" value="20"></div>
        <div class="row"><label>Satellites</label>
            <input type="range" id="gps_sats" min="0" max="32" value="12" oninput="updateSlider(this)">
            <span class="val" id="gps_sats_val">12</span></div>
    </div>

    <div class="card">
        <h2>Seat & ACM</h2>
        <div class="row"><label>Seat Position (°)</label>
            <input type="range" id="seat_pos" min="-45" max="90" value="15" oninput="updateSlider(this)">
            <span class="val" id="seat_pos_val">15°</span></div>
        <div class="row"><label>Seat Profile</label>
            <select id="seat_profile">
                <option value="0">Manual</option><option value="1">Profile 1</option>
                <option value="2">Profile 2</option><option value="3">Profile 3</option>
            </select></div>
        <div class="row"><label>FSR Raw</label>
            <input type="range" id="acm_fsr" min="0" max="4095" value="512" oninput="updateSlider(this)">
            <span class="val" id="acm_fsr_val">512</span></div>
        <div class="row"><label>ACM Status</label>
            <select id="acm_status">
                <option value="0">OK</option><option value="1">Crash</option><option value="2">Safe Mode</option>
            </select></div>
    </div>
</div>

<script>
let autoTimer = null;
let frameCount = 0;
let seq = 0;

const FLAGS = {
    RAIN: 0, DOOR_OPEN: 1, DOOR_LOCKED: 2, HEADLIGHT: 3, WIPER_ON: 4,
    INDICATOR_L: 5, INDICATOR_R: 6, CRASH: 7, AIRBAG: 8, SEATBELT_WARN: 9,
    SEAT_OCCUPIED: 10
};
let vehFlags = (1 << 2) | (1 << 10);

function getEl(id) {
    return document.getElementById(id);
}

function updateSlider(slider) {
    let valId = slider.id + '_val';
    let valEl = getEl(valId);
    if (!valEl) return;
    let v = parseInt(slider.value);
    switch(slider.id) {
        case 'veh_speed': valEl.textContent = (v/10).toFixed(1) + ' km/h'; break;
        case 'veh_accel': valEl.textContent = (v/100).toFixed(2) + ' m/s²'; break;
        case 'acm_throttle': valEl.textContent = (v/100).toFixed(0) + '%'; break;
        case 'acm_brake': valEl.textContent = v + ' Pa'; break;
        case 'cpu_load': valEl.textContent = (v/100).toFixed(0) + '%'; break;
        case 'env_temp': valEl.textContent = (v/10).toFixed(1) + '°C'; break;
        case 'env_hum': valEl.textContent = (v/100).toFixed(0) + '%'; break;
        case 'gps_sats': valEl.textContent = v; break;
        case 'seat_pos': valEl.textContent = v + '°'; break;
        case 'acm_fsr': valEl.textContent = v; break;
    }
}

function toggleFlag(name, cb) {
    if (cb.checked) vehFlags |= (1 << FLAGS[name]);
    else vehFlags &= ~(1 << FLAGS[name]);
}

function collectData() {
    return {
        sched_mode: parseInt(getEl('sched_mode').value),
        cpu_load: parseInt(getEl('cpu_load').value),
        total_misses: parseInt(getEl('total_misses').value),
        fault_flags: parseInt(getEl('fault_flags').value),
        veh_speed: parseInt(getEl('veh_speed').value),
        veh_accel: parseInt(getEl('veh_accel').value),
        acm_throttle: parseInt(getEl('acm_throttle').value),
        acm_brake: parseInt(getEl('acm_brake').value),
        veh_flags: vehFlags,
        wiper_speed: parseInt(getEl('wiper_speed').value),
        env_temp: parseInt(getEl('env_temp').value),
        env_hum: parseInt(getEl('env_hum').value),
        env_press: parseInt(getEl('env_press').value),
        gps_lat: parseInt(getEl('gps_lat').value),
        gps_lon: parseInt(getEl('gps_lon').value),
        gps_alt: parseInt(getEl('gps_alt').value),
        gps_sats: parseInt(getEl('gps_sats').value),
        seat_pos: parseInt(getEl('seat_pos').value),
        seat_profile: parseInt(getEl('seat_profile').value),
        acm_fsr: parseInt(getEl('acm_fsr').value),
        acm_status: parseInt(getEl('acm_status').value)
    };
}

function sendFrame() {
    fetch('/update', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(collectData())
    }).then(r => r.json()).then(data => {
        frameCount++;
        seq = data.seq;
        getEl('seq').textContent = seq;
        getEl('frames').textContent = frameCount;
    });
}

function toggleAuto() {
    let btn = getEl('autoBtn');
    if (autoTimer) {
        clearInterval(autoTimer);
        autoTimer = null;
        btn.textContent = 'Auto Send';
        btn.classList.remove('stop');
        fetch('/auto?enable=0');
    } else {
        sendFrame();
        autoTimer = setInterval(sendFrame, 100);
        btn.textContent = 'Stop Auto';
        btn.classList.add('stop');
        fetch('/auto?enable=1');
    }
}

function resetAll() {
    fetch('/reset', {method: 'POST'}).then(() => location.reload());
}

document.querySelectorAll('input[type="range"]').forEach(s => updateSlider(s));
</script>
</body>
</html>
)rawliteral";
}

void handleRoot()
{
    server.send(200, "text/html", get_html_page());
}

void handleUpdate()
{
    if (server.hasArg("plain"))
    {
        String json = server.arg("plain");

        auto getInt = [&](const char *key, int def = 0) -> int
        {
            String search = "\"" + String(key) + "\":";
            int pos = json.indexOf(search);
            if (pos < 0)
                return def;
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
                pos++;
            return json.substring(pos).toInt();
        };

        telemetry.sched_mode = getInt("sched_mode", 0);
        telemetry.cpu_load_x100 = getInt("cpu_load", 4500);
        telemetry.total_misses = getInt("total_misses", 0);
        telemetry.fault_flags = getInt("fault_flags", 0);
        telemetry.veh_speed_x10 = getInt("veh_speed", 600);
        telemetry.veh_accel_x100 = getInt("veh_accel", 0);
        telemetry.acm_throttle_x100 = getInt("acm_throttle", 2500);
        telemetry.acm_brake_pa = getInt("acm_brake", 0);
        telemetry.veh_flags = getInt("veh_flags", 0);
        telemetry.bcm_wiper_speed = getInt("wiper_speed", 0);
        telemetry.env_temp_x10 = getInt("env_temp", 250);
        telemetry.env_hum_x100 = getInt("env_hum", 5500);
        telemetry.env_press_pa = getInt("env_press", 101325);
        telemetry.gps_lat_x1e6 = getInt("gps_lat", 37422390);
        telemetry.gps_lon_x1e6 = getInt("gps_lon", -12208402);
        telemetry.gps_alt_m = getInt("gps_alt", 20);
        telemetry.gps_satellites = getInt("gps_sats", 12);
        telemetry.seat_position_deg = getInt("seat_pos", 15);
        telemetry.seat_profile = getInt("seat_profile", 0);
        telemetry.acm_fsr_raw = getInt("acm_fsr", 512);
        telemetry.acm_status = getInt("acm_status", 0);
    }

    send_telemetry_frame();

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"seq\":%u,\"status\":\"ok\"}", telemetry.seq);
    server.send(200, "application/json", resp);
}

void handleAuto()
{
    if (server.hasArg("enable"))
    {
        auto_increment = (server.arg("enable") == "1");
    }
    server.send(200, "text/plain", "ok");
}

void handleReset()
{
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.magic = SDV_TELEMETRY_MAGIC;
    telemetry.version = SDV_TELEMETRY_VERSION;
    telemetry.num_tasks = 4;
    telemetry.veh_flags = SDV_VEH_DOOR_LOCKED | SDV_VEH_SEAT_OCCUPIED;
    telemetry.env_press_pa = 101325;
    telemetry.env_temp_x10 = 250;
    telemetry.env_hum_x100 = 5500;
    telemetry.gps_lat_x1e6 = 37422390;
    telemetry.gps_lon_x1e6 = -12208402;
    telemetry.gps_alt_m = 20;
    telemetry.gps_satellites = 12;
    telemetry.cpu_load_x100 = 4500;
    telemetry.seat_position_deg = 15;
    telemetry.acm_fsr_raw = 512;
    telemetry.acm_throttle_x100 = 2500;
    telemetry.sched_mode = 0;
    frame_seq = 0;
    start_time = millis();
    server.send(200, "text/plain", "ok");
}

void setup()
{
    Serial.begin(115200);

    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.magic = SDV_TELEMETRY_MAGIC;
    telemetry.version = SDV_TELEMETRY_VERSION;
    telemetry.num_tasks = 4;
    telemetry.veh_flags = SDV_VEH_DOOR_LOCKED | SDV_VEH_SEAT_OCCUPIED;
    telemetry.env_press_pa = 101325;
    telemetry.env_temp_x10 = 250;
    telemetry.env_hum_x100 = 5500;
    telemetry.gps_lat_x1e6 = 37422390;
    telemetry.gps_lon_x1e6 = -12208402;
    telemetry.gps_alt_m = 20;
    telemetry.gps_satellites = 12;
    telemetry.cpu_load_x100 = 4500;
    telemetry.seat_position_deg = 15;
    telemetry.acm_fsr_raw = 512;
    telemetry.acm_throttle_x100 = 2500;

    for (int i = 0; i < 4; i++)
    {
        telemetry.task[i].resp_max_x10us = 1000 + i * 500;
        telemetry.task[i].resp_avg_x10us = 500 + i * 200;
        telemetry.task[i].exec_count = 1000 * (i + 1);
        telemetry.task[i].deadline_misses = i;
    }

    start_time = millis();

    WiFi.softAP(ssid, password);

    server.on("/", handleRoot);
    server.on("/update", HTTP_POST, handleUpdate);
    server.on("/auto", handleAuto);
    server.on("/reset", HTTP_POST, handleReset);

    server.begin();

    pinMode(2, OUTPUT);
}

void loop()
{
    server.handleClient();

    static unsigned long last_send = 0;
    if (auto_increment && (millis() - last_send >= update_interval_ms))
    {
        send_telemetry_frame();
        last_send = millis();
        digitalWrite(2, !digitalRead(2));
    }
}