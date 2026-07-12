#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t resp_max_x10us;
    uint16_t resp_avg_x10us;
    uint16_t exec_count;
    uint16_t deadline_misses;
} sdv_task_stats_t;

typedef struct {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  seq;
    uint8_t  sched_mode;
    uint16_t run_id;
    uint8_t  num_tasks;
    uint8_t  fault_flags;
    uint32_t uptime_ms;
    uint16_t cpu_load_x100;
    uint16_t total_misses;
    uint16_t veh_flags;
    uint16_t veh_speed_x10;
    int16_t  veh_accel_x100;
    uint8_t  bcm_wiper_speed;
    uint16_t acm_throttle_x100;
    uint16_t acm_brake_pa;
    uint16_t acm_fsr_raw;
    uint8_t  acm_status;
    int16_t  seat_position_deg;
    uint8_t  seat_profile;
    int16_t  env_temp_x10;
    uint16_t env_hum_x100;
    uint32_t env_press_pa;
    int32_t  gps_lat_x1e6;
    int32_t  gps_lon_x1e6;
    int16_t  gps_alt_m;
    uint8_t  gps_satellites;
    sdv_task_stats_t task[4];
} sdv_telemetry_t;
#pragma pack(pop)

#define MAGIC 0xA5

const char *AP_SSID = "SDV_Telemetry";

WebServer server(80);

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

sdv_telemetry_t pkt;
uint8_t seq_counter = 0;
uint32_t cycle_counter = 0;

void collect_dummy_data() {
    pkt.magic       = MAGIC;
    pkt.version     = 1;
    pkt.seq         = seq_counter++;
    pkt.sched_mode  = (uint8_t)(cycle_counter % 3);
    pkt.run_id      = 1234;
    pkt.num_tasks   = 4;
    pkt.fault_flags = (cycle_counter % 50 == 0) ? 0x01 : 0x00;
    pkt.uptime_ms      = millis();
    pkt.cpu_load_x100  = (uint16_t)(2000 + (cycle_counter % 60) * 50);
    pkt.total_misses   = (uint16_t)(cycle_counter / 20);
    pkt.veh_flags      = (uint16_t)(0x0001 << (cycle_counter % 4));
    pkt.veh_speed_x10  = (uint16_t)(300 + (cycle_counter % 400));
    pkt.veh_accel_x100 = (int16_t)(((cycle_counter % 40) - 20) * 5);
    pkt.bcm_wiper_speed = (uint8_t)(cycle_counter % 3);
    pkt.acm_throttle_x100 = (uint16_t)((cycle_counter % 100) * 100);
    pkt.acm_brake_pa      = (uint16_t)((cycle_counter % 20) * 500);
    pkt.acm_fsr_raw       = (uint16_t)(300 + (cycle_counter % 400));
    pkt.acm_status        = (uint8_t)(1 + (cycle_counter % 2));
    pkt.seat_position_deg = (int16_t)(((cycle_counter % 60) - 30));
    pkt.seat_profile      = (uint8_t)(cycle_counter % 3);
    pkt.env_temp_x10 = (int16_t)(180 + (cycle_counter % 100));
    pkt.env_hum_x100 = (uint16_t)(3000 + (cycle_counter % 4000));
    pkt.env_press_pa = (uint32_t)(100000 + (cycle_counter % 3000));
    pkt.gps_lat_x1e6 = 37774900 + (int32_t)(cycle_counter % 1000);
    pkt.gps_lon_x1e6 = -122419400 + (int32_t)(cycle_counter % 1000);
    pkt.gps_alt_m    = (int16_t)(10 + (cycle_counter % 50));
    pkt.gps_satellites = (uint8_t)(4 + (cycle_counter % 9));

    for (int i = 0; i < 4; i++) {
        pkt.task[i].resp_max_x10us    = (uint16_t)(100 + i + (cycle_counter % 30));
        pkt.task[i].resp_avg_x10us    = (uint16_t)(50 + i + (cycle_counter % 15));
        pkt.task[i].exec_count        = (uint16_t)(1000 + i + cycle_counter);
        pkt.task[i].deadline_misses   = (uint16_t)((cycle_counter / 25) % 5);
    }

    cycle_counter++;
}

void send_packet() {
    uint8_t crc = crc8((uint8_t*)&pkt, sizeof(pkt));
    Serial.write((uint8_t*)&pkt, sizeof(pkt));
    Serial.write(&crc, 1);
}

String arg_or(const String &name, const String &fallback) {
    if (server.hasArg(name)) return server.arg(name);
    return fallback;
}

void handle_root() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>";
    html += "body{background:#f5f5f7;font-family:Roboto,Arial,sans-serif;margin:0;padding:24px;color:#202124}";
    html += "h1{font-size:20px;font-weight:500;margin:0 0 20px 0}";
    html += ".card{background:#fff;border-radius:8px;box-shadow:0 1px 3px rgba(0,0,0,0.12),0 1px 2px rgba(0,0,0,0.08);padding:20px;margin-bottom:16px}";
    html += ".card h2{font-size:14px;font-weight:500;color:#5f6368;margin:0 0 16px 0;text-transform:uppercase;letter-spacing:0.05em}";
    html += ".row{display:flex;align-items:center;margin-bottom:14px}";
    html += ".row label{flex:0 0 220px;font-size:14px;color:#3c4043}";
    html += ".row input{flex:1;padding:8px 10px;border:1px solid #dadce0;border-radius:4px;font-size:14px;font-family:inherit}";
    html += ".row input:focus{outline:none;border-color:#3f51b5;box-shadow:0 0 0 2px rgba(63,81,181,0.15)}";
    html += "button{background:#3f51b5;color:#fff;border:none;padding:12px 28px;border-radius:4px;font-size:14px;font-weight:500;cursor:pointer;letter-spacing:0.03em}";
    html += "button:hover{background:#3849a8}";
    html += ".status{padding:12px 16px;border-radius:4px;font-size:14px;margin-bottom:16px;display:none}";
    html += ".status.ok{background:#e6f4ea;color:#137333;display:block}";
    html += "</style></head><body>";
    html += "<h1>SDV Telemetry Sender</h1>";
    html += "<div class='status ok' id='status'></div>";
    html += "<form action='/send' method='POST'>";

    html += "<div class='card'><h2>Frame</h2>";
    html += "<div class='row'><label>version</label><input type='number' name='version' value='1'></div>";
    html += "<div class='row'><label>seq</label><input type='number' name='seq' value='0'></div>";
    html += "<div class='row'><label>sched_mode</label><input type='number' name='sched_mode' value='0'></div>";
    html += "<div class='row'><label>run_id</label><input type='number' name='run_id' value='1234'></div>";
    html += "<div class='row'><label>num_tasks</label><input type='number' name='num_tasks' value='4'></div>";
    html += "<div class='row'><label>fault_flags</label><input type='number' name='fault_flags' value='0'></div>";
    html += "<div class='row'><label>uptime_ms</label><input type='number' name='uptime_ms' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>System</h2>";
    html += "<div class='row'><label>cpu_load_x100</label><input type='number' name='cpu_load_x100' value='2500'></div>";
    html += "<div class='row'><label>total_misses</label><input type='number' name='total_misses' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>Vehicle</h2>";
    html += "<div class='row'><label>veh_flags</label><input type='number' name='veh_flags' value='1'></div>";
    html += "<div class='row'><label>veh_speed_x10</label><input type='number' name='veh_speed_x10' value='500'></div>";
    html += "<div class='row'><label>veh_accel_x100</label><input type='number' name='veh_accel_x100' value='0'></div>";
    html += "<div class='row'><label>bcm_wiper_speed</label><input type='number' name='bcm_wiper_speed' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>ACM</h2>";
    html += "<div class='row'><label>acm_throttle_x100</label><input type='number' name='acm_throttle_x100' value='0'></div>";
    html += "<div class='row'><label>acm_brake_pa</label><input type='number' name='acm_brake_pa' value='0'></div>";
    html += "<div class='row'><label>acm_fsr_raw</label><input type='number' name='acm_fsr_raw' value='0'></div>";
    html += "<div class='row'><label>acm_status</label><input type='number' name='acm_status' value='1'></div>";
    html += "</div>";

    html += "<div class='card'><h2>Seat</h2>";
    html += "<div class='row'><label>seat_position_deg</label><input type='number' name='seat_position_deg' value='0'></div>";
    html += "<div class='row'><label>seat_profile</label><input type='number' name='seat_profile' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>Environment</h2>";
    html += "<div class='row'><label>env_temp_x10</label><input type='number' name='env_temp_x10' value='220'></div>";
    html += "<div class='row'><label>env_hum_x100</label><input type='number' name='env_hum_x100' value='4500'></div>";
    html += "<div class='row'><label>env_press_pa</label><input type='number' name='env_press_pa' value='101325'></div>";
    html += "</div>";

    html += "<div class='card'><h2>GPS</h2>";
    html += "<div class='row'><label>gps_lat_x1e6</label><input type='number' name='gps_lat_x1e6' value='37774900'></div>";
    html += "<div class='row'><label>gps_lon_x1e6</label><input type='number' name='gps_lon_x1e6' value='-122419400'></div>";
    html += "<div class='row'><label>gps_alt_m</label><input type='number' name='gps_alt_m' value='10'></div>";
    html += "<div class='row'><label>gps_satellites</label><input type='number' name='gps_satellites' value='8'></div>";
    html += "</div>";

    for (int i = 0; i < 4; i++) {
        html += "<div class='card'><h2>Task " + String(i) + "</h2>";
        html += "<div class='row'><label>resp_max_x10us</label><input type='number' name='t" + String(i) + "_resp_max' value='100'></div>";
        html += "<div class='row'><label>resp_avg_x10us</label><input type='number' name='t" + String(i) + "_resp_avg' value='50'></div>";
        html += "<div class='row'><label>exec_count</label><input type='number' name='t" + String(i) + "_exec_count' value='1000'></div>";
        html += "<div class='row'><label>deadline_misses</label><input type='number' name='t" + String(i) + "_deadline_misses' value='0'></div>";
        html += "</div>";
    }

    html += "<button type='submit'>Send Packet</button>";
    html += "</form></body></html>";

    server.send(200, "text/html", html);
}

void handle_send() {
    pkt.magic       = MAGIC;
    pkt.version     = (uint8_t)arg_or("version", "1").toInt();
    pkt.seq         = (uint8_t)arg_or("seq", "0").toInt();
    pkt.sched_mode  = (uint8_t)arg_or("sched_mode", "0").toInt();
    pkt.run_id      = (uint16_t)arg_or("run_id", "0").toInt();
    pkt.num_tasks   = (uint8_t)arg_or("num_tasks", "4").toInt();
    pkt.fault_flags = (uint8_t)arg_or("fault_flags", "0").toInt();
    pkt.uptime_ms   = (uint32_t)arg_or("uptime_ms", "0").toInt();

    pkt.cpu_load_x100 = (uint16_t)arg_or("cpu_load_x100", "0").toInt();
    pkt.total_misses  = (uint16_t)arg_or("total_misses", "0").toInt();

    pkt.veh_flags      = (uint16_t)arg_or("veh_flags", "0").toInt();
    pkt.veh_speed_x10  = (uint16_t)arg_or("veh_speed_x10", "0").toInt();
    pkt.veh_accel_x100 = (int16_t)arg_or("veh_accel_x100", "0").toInt();
    pkt.bcm_wiper_speed = (uint8_t)arg_or("bcm_wiper_speed", "0").toInt();

    pkt.acm_throttle_x100 = (uint16_t)arg_or("acm_throttle_x100", "0").toInt();
    pkt.acm_brake_pa      = (uint16_t)arg_or("acm_brake_pa", "0").toInt();
    pkt.acm_fsr_raw       = (uint16_t)arg_or("acm_fsr_raw", "0").toInt();
    pkt.acm_status        = (uint8_t)arg_or("acm_status", "0").toInt();

    pkt.seat_position_deg = (int16_t)arg_or("seat_position_deg", "0").toInt();
    pkt.seat_profile      = (uint8_t)arg_or("seat_profile", "0").toInt();

    pkt.env_temp_x10 = (int16_t)arg_or("env_temp_x10", "0").toInt();
    pkt.env_hum_x100 = (uint16_t)arg_or("env_hum_x100", "0").toInt();
    pkt.env_press_pa = (uint32_t)arg_or("env_press_pa", "0").toInt();

    pkt.gps_lat_x1e6 = (int32_t)arg_or("gps_lat_x1e6", "0").toInt();
    pkt.gps_lon_x1e6 = (int32_t)arg_or("gps_lon_x1e6", "0").toInt();
    pkt.gps_alt_m    = (int16_t)arg_or("gps_alt_m", "0").toInt();
    pkt.gps_satellites = (uint8_t)arg_or("gps_satellites", "0").toInt();

    for (int i = 0; i < 4; i++) {
        String p = "t" + String(i) + "_";
        pkt.task[i].resp_max_x10us  = (uint16_t)arg_or(p + "resp_max", "0").toInt();
        pkt.task[i].resp_avg_x10us  = (uint16_t)arg_or(p + "resp_avg", "0").toInt();
        pkt.task[i].exec_count      = (uint16_t)arg_or(p + "exec_count", "0").toInt();
        pkt.task[i].deadline_misses = (uint16_t)arg_or(p + "deadline_misses", "0").toInt();
    }

    send_packet();
    seq_counter = pkt.seq + 1;

    server.sendHeader("Location", "/");
    server.send(303);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    memset(&pkt, 0, sizeof(pkt));

    WiFi.softAP(AP_SSID);

    server.on("/", HTTP_GET, handle_root);
    server.on("/send", HTTP_POST, handle_send);
    server.begin();
}

void loop() {
    server.handleClient();
}