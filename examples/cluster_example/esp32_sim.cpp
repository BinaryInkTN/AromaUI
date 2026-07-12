#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "scenarios.h"

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

// ---------------------------------------------------------------------------
// Scenario runtime state
// ---------------------------------------------------------------------------
ScenarioId active_scenario = SCN_IDLE;
uint32_t   scenario_start_ms = 0;
uint32_t   scenario_variant_counter = 0;
uint32_t   last_scenario_emit_ms = 0;
const uint32_t SCENARIO_EMIT_INTERVAL_MS = 100;

String last_form_error = "";

const char *scenario_descriptions[SCN_COUNT] = {
    "Nominal cruise, no faults",
    "Aggressive acceleration ramp, throttle and speed rise",
    "Aggressive deceleration, brake pressure spikes, harsh-braking flag set",
    "ACM sensor or actuator fault, alternates fault kind per run",
    "Scheduler overload: CPU load and deadline misses climb",
    "Environment/GPS sensor fault: implausible readings, satellite loss",
    "Turn signals: alternating left/right indicators",
    "Hazard lights: both indicators flashing",
    "Door open: vehicle parked with door ajar",
    "Seatbelt warning: driving without seatbelt fastened",
    "Crash sequence: impact detection and airbag deployment",
    "Rain wipers: cycling through wiper speeds",
    "Engine fault: misfire, rough running, overheating",
    "ABS fault: braking system malfunction, instability",
    "Low fuel: fuel level critical, gradual power loss",
    "Battery warning: electrical system voltage sag, sensor glitches",
    "Combined: normal driving with signals and light rain"
};

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

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------
bool validate_range_i32(const String &field, long val, long lo, long hi, String &err) {
    if (val < lo || val > hi) {
        err += field + " must be between " + String(lo) + " and " + String(hi) + " (got " + String(val) + ").<br>";
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// HTML page shell
// -----------------------------------------------------------------------
String page_style() {
    String css = "<style>";
    css += "body{background:#f5f5f7;font-family:Roboto,Arial,sans-serif;margin:0;padding:24px;color:#202124}";
    css += "h1{font-size:20px;font-weight:500;margin:0 0 4px 0}";
    css += ".subtitle{color:#5f6368;font-size:13px;margin:0 0 20px 0}";
    css += ".tabs{display:flex;gap:8px;margin-bottom:20px;border-bottom:1px solid #dadce0}";
    css += ".tab{padding:10px 16px;font-size:14px;color:#5f6368;text-decoration:none;border-bottom:2px solid transparent}";
    css += ".tab.active{color:#3f51b5;border-bottom-color:#3f51b5;font-weight:500}";
    css += ".card{background:#fff;border-radius:8px;box-shadow:0 1px 3px rgba(0,0,0,0.12),0 1px 2px rgba(0,0,0,0.08);padding:20px;margin-bottom:16px}";
    css += ".card h2{font-size:14px;font-weight:500;color:#5f6368;margin:0 0 16px 0;text-transform:uppercase;letter-spacing:0.05em}";
    css += ".row{display:flex;align-items:center;margin-bottom:14px}";
    css += ".row label{flex:0 0 220px;font-size:14px;color:#3c4043}";
    css += ".row input[type=number]{flex:1;padding:8px 10px;border:1px solid #dadce0;border-radius:4px;font-size:14px;font-family:inherit}";
    css += ".row input[type=number]:focus{outline:none;border-color:#3f51b5;box-shadow:0 0 0 2px rgba(63,81,181,0.15)}";
    css += ".row input[type=range]{flex:1;height:4px;border-radius:2px;background:#dadce0;outline:none;-webkit-appearance:none;appearance:none;accent-color:#3f51b5}";
    css += ".row input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:18px;height:18px;border-radius:50%;background:#3f51b5;cursor:pointer;box-shadow:0 1px 3px rgba(0,0,0,0.3)}";
    css += ".row input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#3f51b5;cursor:pointer;border:none;box-shadow:0 1px 3px rgba(0,0,0,0.3)}";
    css += ".row .slider-readout{flex:0 0 76px;text-align:right;font-family:'Roboto Mono',monospace;font-size:13px;color:#3f51b5;font-weight:500;margin-left:12px}";
    css += ".row .slider-unit{color:#5f6368;font-weight:400;margin-left:2px}";
    css += "button{background:#3f51b5;color:#fff;border:none;padding:12px 28px;border-radius:4px;font-size:14px;font-weight:500;cursor:pointer;letter-spacing:0.03em}";
    css += "button:hover{background:#3849a8}";
    css += "button.secondary{background:#fff;color:#3f51b5;border:1px solid #3f51b5}";
    css += "button.secondary:hover{background:#f0f1fb}";
    css += "button.danger{background:#c5221f}";
    css += "button.danger:hover{background:#a91d1a}";
    css += ".status{padding:12px 16px;border-radius:4px;font-size:14px;margin-bottom:16px;display:none}";
    css += ".status.ok{background:#e6f4ea;color:#137333;display:block}";
    css += ".status.err{background:#fce8e6;color:#c5221f;display:block}";
    css += ".scenario-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}";
    css += ".scenario-btn{background:#fff;border:1px solid #dadce0;border-radius:8px;padding:18px;text-align:left;cursor:pointer;font-family:inherit}";
    css += ".scenario-btn:hover{border-color:#3f51b5;box-shadow:0 1px 3px rgba(0,0,0,0.1)}";
    css += ".scenario-btn .name{font-size:15px;font-weight:500;color:#202124;display:block;margin-bottom:4px}";
    css += ".scenario-btn .desc{font-size:12px;color:#5f6368}";
    css += ".scenario-btn.active-scn{border-color:#3f51b5;background:#f0f1fb}";
    css += ".live-badge{display:inline-block;background:#137333;color:#fff;font-size:11px;padding:2px 8px;border-radius:10px;margin-left:8px;vertical-align:middle}";
    css += ".mono{font-family:'Roboto Mono',monospace;font-size:13px;color:#3c4043}";
    css += "</style>";
    return css;
}

String page_header(const String &active_tab, const String &status_html) {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += page_style();
    html += "</head><body>";
    html += "<h1>SDV Telemetry Test Bench</h1>";
    html += "<div class='subtitle'>Serial frame generator &middot; run_id " + String(pkt.run_id) + "</div>";
    html += "<div class='tabs'>";
    html += "<a class='tab" + String(active_tab == "scenarios" ? " active" : "") + "' href='/'>Scenarios</a>";
    html += "<a class='tab" + String(active_tab == "manual" ? " active" : "") + "' href='/manual'>Manual Frame</a>";
    html += "</div>";
    if (status_html.length() > 0) html += status_html;
    return html;
}

// -----------------------------------------------------------------------
// Scenario page
// -----------------------------------------------------------------------
void handle_root() {
    String status_html = "";
    bool is_active = (active_scenario != SCN_IDLE);
    if (is_active) {
        status_html = "<div class='status ok'>Streaming <b>" + String(scenario_name(active_scenario)) +
                       "</b> &middot; elapsed " + String((millis() - scenario_start_ms)) + " ms</div>";
    }

    String html = page_header("scenarios", status_html);

    html += "<div class='card'><h2>Scenarios";
    if (is_active) html += "<span class='live-badge'>LIVE</span>";
    html += "</h2>";
    html += "<div class='scenario-grid'>";
    for (int i = 0; i < SCN_COUNT; i++) {
        ScenarioId id = (ScenarioId)i;
        bool this_active = (active_scenario == id);
        html += "<form action='/scenario/start' method='POST' style='margin:0'>";
        html += "<input type='hidden' name='type' value='" + String(scenario_name(id)) + "'>";
        html += "<button type='submit' class='scenario-btn" + String(this_active ? " active-scn" : "") + "'>";
        html += "<span class='name'>" + String(scenario_name(id)) + "</span>";
        html += "<span class='desc'>" + String(scenario_descriptions[i]) + "</span>";
        html += "</button></form>";
    }
    html += "</div></div>";

    html += "<div class='card'><h2>Control</h2>";
    html += "<form action='/scenario/stop' method='POST' style='display:inline'>";
    html += "<button type='submit' class='danger'" + String(is_active ? "" : " disabled") + ">Stop streaming</button>";
    html += "</form>&nbsp;&nbsp;";
    html += "<span class='mono'>Emit interval: " + String(SCENARIO_EMIT_INTERVAL_MS) + " ms &middot; seq: " + String(seq_counter) + "</span>";
    html += "</div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handle_scenario_start() {
    String type = arg_or("type", "");
    ScenarioId id;
    if (!scenario_from_name(type, &id)) {
        String status_html = "<div class='status err'>Unknown scenario '" + type + "'. No change made.</div>";
        String html = page_header("scenarios", status_html);
        html += "</body></html>";
        server.send(400, "text/html", html);
        return;
    }
    active_scenario         = id;
    scenario_start_ms       = millis();
    scenario_variant_counter++;
    last_scenario_emit_ms   = 0;

    server.sendHeader("Location", "/");
    server.send(303);
}

void handle_scenario_stop() {
    active_scenario = SCN_IDLE;
    server.sendHeader("Location", "/");
    server.send(303);
}

// -----------------------------------------------------------------------
// Manual field-by-field form
// -----------------------------------------------------------------------
String slider_row(const String &label, const String &name, long min_v, long max_v, long step, long default_v,
                   float display_divisor = 1.0f, const String &unit = "",
                   const char **status_labels = nullptr, int label_count = 0) {
    String id = "slider_" + name;
    String html = "<div class='row'><label>" + label + "</label>";
    html += "<input type='range' id='" + id + "' name='" + name + "' min='" + String(min_v) +
            "' max='" + String(max_v) + "' step='" + String(step) + "' value='" + String(default_v) + "'";
    html += " oninput=\"document.getElementById('" + id + "_out').textContent = ";
    if (status_labels != nullptr && label_count == (max_v - min_v + 1)) {
        String arr = "[";
        for (int i = 0; i < label_count; i++) {
            if (i > 0) arr += ",";
            arr += "'" + String(status_labels[i]) + "'";
        }
        arr += "]";
        html += arr + "[this.value - (" + String(min_v) + ")]";
    } else if (display_divisor != 1.0f) {
        html += "(this.value / " + String(display_divisor, 0) + ").toFixed(1) + '" + unit + "'";
    } else {
        html += "this.value + '" + unit + "'";
    }
    html += "\">";
    html += "<span class='slider-readout' id='" + id + "_out'>";
    if (status_labels != nullptr && label_count == (max_v - min_v + 1)) {
        html += String(status_labels[default_v - min_v]);
    } else if (display_divisor != 1.0f) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", (float)default_v / display_divisor);
        html += String(buf) + unit;
    } else {
        html += String(default_v) + unit;
    }
    html += "</span></div>";
    return html;
}

const char *acm_status_labels[4]     = {"nominal", "degraded", "sensor_fault", "actuator_fault"};
const char *wiper_speed_labels[3]    = {"off", "low", "high"};

void handle_manual_form() {
    String status_html = "";
    if (last_form_error.length() > 0) {
        status_html = "<div class='status err'>" + last_form_error + "</div>";
        last_form_error = "";
    }

    String html = page_header("manual", status_html);
    html += "<form action='/manual/send' method='POST'>";

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
    html += slider_row("cpu_load_x100 (%)", "cpu_load_x100", 0, 10000, 100, 2500, 100.0f, "%");
    html += "<div class='row'><label>total_misses</label><input type='number' name='total_misses' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>Vehicle</h2>";
    html += "<div class='row'><label>veh_flags</label><input type='number' name='veh_flags' value='1'></div>";
    html += slider_row("veh_speed_x10 (km/h)", "veh_speed_x10", 0, VEH_SPEED_X10_MAX, 1, VEH_CRUISE_SPEED_X10, 10.0f, " km/h");
    html += slider_row("veh_accel_x100 (m/s\u00b2)", "veh_accel_x100", VEH_ACCEL_X100_MIN, VEH_ACCEL_X100_MAX, 5, 0, 100.0f, " m/s\u00b2");
    html += slider_row("bcm_wiper_speed", "bcm_wiper_speed", 0, 2, 1, 0, 1.0f, "", wiper_speed_labels, 3);
    html += "</div>";

    html += "<div class='card'><h2>ACM</h2>";
    html += slider_row("acm_throttle_x100 (%)", "acm_throttle_x100", 0, ACM_THROTTLE_X100_MAX, 25, 0, 100.0f, "%");
    html += slider_row("acm_brake_pa (kPa)", "acm_brake_pa", 0, ACM_BRAKE_PA_MAX, 50, 0, 1000.0f, " kPa");
    html += slider_row("acm_fsr_raw", "acm_fsr_raw", 0, ACM_FSR_RAW_MAX, 1, 0, 1.0f, "");
    html += slider_row("acm_status", "acm_status", 1, 4, 1, 1, 1.0f, "", acm_status_labels, 4);
    html += "</div>";

    html += "<div class='card'><h2>Seat</h2>";
    html += slider_row("seat_position_deg", "seat_position_deg", -900, 900, 10, 0, 10.0f, "\u00b0");
    html += "<div class='row'><label>seat_profile</label><input type='number' name='seat_profile' value='0'></div>";
    html += "</div>";

    html += "<div class='card'><h2>Environment</h2>";
    html += slider_row("env_temp_x10 (\u00b0C)", "env_temp_x10", -400, 850, 5, 220, 10.0f, "\u00b0C");
    html += slider_row("env_hum_x100 (%)", "env_hum_x100", 0, 10000, 100, 4500, 100.0f, "%");
    html += "<div class='row'><label>env_press_pa</label><input type='number' name='env_press_pa' value='101325'></div>";
    html += "</div>";

    html += "<div class='card'><h2>GPS</h2>";
    html += "<div class='row'><label>gps_lat_x1e6</label><input type='number' name='gps_lat_x1e6' value='37774900'></div>";
    html += "<div class='row'><label>gps_lon_x1e6</label><input type='number' name='gps_lon_x1e6' value='-122419400'></div>";
    html += "<div class='row'><label>gps_alt_m</label><input type='number' name='gps_alt_m' value='10'></div>";
    html += slider_row("gps_satellites", "gps_satellites", 0, 32, 1, 8, 1.0f, "");
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

void handle_manual_send() {
    active_scenario = SCN_IDLE;

    String err = "";
    long version    = arg_or("version", "1").toInt();
    long seq        = arg_or("seq", "0").toInt();
    long sched_mode = arg_or("sched_mode", "0").toInt();
    long run_id     = arg_or("run_id", "0").toInt();
    long num_tasks  = arg_or("num_tasks", "4").toInt();
    long fault_flags = arg_or("fault_flags", "0").toInt();
    long uptime_ms  = arg_or("uptime_ms", "0").toInt();
    long cpu_load_x100 = arg_or("cpu_load_x100", "0").toInt();
    long total_misses  = arg_or("total_misses", "0").toInt();
    long veh_flags      = arg_or("veh_flags", "0").toInt();
    long veh_speed_x10  = arg_or("veh_speed_x10", "0").toInt();
    long veh_accel_x100 = arg_or("veh_accel_x100", "0").toInt();
    long bcm_wiper_speed = arg_or("bcm_wiper_speed", "0").toInt();
    long acm_throttle_x100 = arg_or("acm_throttle_x100", "0").toInt();
    long acm_brake_pa      = arg_or("acm_brake_pa", "0").toInt();
    long acm_fsr_raw       = arg_or("acm_fsr_raw", "0").toInt();
    long acm_status        = arg_or("acm_status", "0").toInt();
    long seat_position_deg = arg_or("seat_position_deg", "0").toInt();
    long seat_profile      = arg_or("seat_profile", "0").toInt();
    long env_temp_x10 = arg_or("env_temp_x10", "0").toInt();
    long env_hum_x100 = arg_or("env_hum_x100", "0").toInt();
    long env_press_pa = arg_or("env_press_pa", "0").toInt();
    long gps_lat_x1e6 = arg_or("gps_lat_x1e6", "0").toInt();
    long gps_lon_x1e6 = arg_or("gps_lon_x1e6", "0").toInt();
    long gps_alt_m    = arg_or("gps_alt_m", "0").toInt();
    long gps_satellites = arg_or("gps_satellites", "0").toInt();

    validate_range_i32("version", version, 0, 255, err);
    validate_range_i32("seq", seq, 0, 255, err);
    validate_range_i32("sched_mode", sched_mode, 0, 255, err);
    validate_range_i32("run_id", run_id, 0, 65535, err);
    validate_range_i32("num_tasks", num_tasks, 0, 4, err);
    validate_range_i32("fault_flags", fault_flags, 0, 255, err);
    validate_range_i32("cpu_load_x100", cpu_load_x100, 0, 65535, err);
    validate_range_i32("total_misses", total_misses, 0, 65535, err);
    validate_range_i32("veh_flags", veh_flags, 0, 65535, err);
    validate_range_i32("veh_speed_x10", veh_speed_x10, 0, 65535, err);
    validate_range_i32("veh_accel_x100", veh_accel_x100, -32768, 32767, err);
    validate_range_i32("bcm_wiper_speed", bcm_wiper_speed, 0, 255, err);
    validate_range_i32("acm_throttle_x100", acm_throttle_x100, 0, 65535, err);
    validate_range_i32("acm_brake_pa", acm_brake_pa, 0, 65535, err);
    validate_range_i32("acm_fsr_raw", acm_fsr_raw, 0, 65535, err);
    validate_range_i32("acm_status", acm_status, 0, 255, err);
    validate_range_i32("seat_position_deg", seat_position_deg, -32768, 32767, err);
    validate_range_i32("seat_profile", seat_profile, 0, 255, err);
    validate_range_i32("env_temp_x10", env_temp_x10, -32768, 32767, err);
    validate_range_i32("env_hum_x100", env_hum_x100, 0, 65535, err);
    validate_range_i32("gps_alt_m", gps_alt_m, -32768, 32767, err);
    validate_range_i32("gps_satellites", gps_satellites, 0, 255, err);

    long task_resp_max[4], task_resp_avg[4], task_exec_count[4], task_deadline_misses[4];
    for (int i = 0; i < 4; i++) {
        String p = "t" + String(i) + "_";
        task_resp_max[i]        = arg_or(p + "resp_max", "0").toInt();
        task_resp_avg[i]        = arg_or(p + "resp_avg", "0").toInt();
        task_exec_count[i]      = arg_or(p + "exec_count", "0").toInt();
        task_deadline_misses[i] = arg_or(p + "deadline_misses", "0").toInt();
        validate_range_i32("t" + String(i) + "_resp_max", task_resp_max[i], 0, 65535, err);
        validate_range_i32("t" + String(i) + "_resp_avg", task_resp_avg[i], 0, 65535, err);
        validate_range_i32("t" + String(i) + "_exec_count", task_exec_count[i], 0, 65535, err);
        validate_range_i32("t" + String(i) + "_deadline_misses", task_deadline_misses[i], 0, 65535, err);
    }

    if (err.length() > 0) {
        last_form_error = err;
        server.sendHeader("Location", "/manual");
        server.send(303);
        return;
    }

    pkt.magic       = MAGIC;
    pkt.version     = (uint8_t)version;
    pkt.seq         = (uint8_t)seq;
    pkt.sched_mode  = (uint8_t)sched_mode;
    pkt.run_id      = (uint16_t)run_id;
    pkt.num_tasks   = (uint8_t)num_tasks;
    pkt.fault_flags = (uint8_t)fault_flags;
    pkt.uptime_ms   = (uint32_t)uptime_ms;

    pkt.cpu_load_x100 = (uint16_t)cpu_load_x100;
    pkt.total_misses  = (uint16_t)total_misses;

    pkt.veh_flags      = (uint16_t)veh_flags;
    pkt.veh_speed_x10  = (uint16_t)veh_speed_x10;
    pkt.veh_accel_x100 = (int16_t)veh_accel_x100;
    pkt.bcm_wiper_speed = (uint8_t)bcm_wiper_speed;

    pkt.acm_throttle_x100 = (uint16_t)acm_throttle_x100;
    pkt.acm_brake_pa      = (uint16_t)acm_brake_pa;
    pkt.acm_fsr_raw       = (uint16_t)acm_fsr_raw;
    pkt.acm_status        = (uint8_t)acm_status;

    pkt.seat_position_deg = (int16_t)seat_position_deg;
    pkt.seat_profile      = (uint8_t)seat_profile;

    pkt.env_temp_x10 = (int16_t)env_temp_x10;
    pkt.env_hum_x100 = (uint16_t)env_hum_x100;
    pkt.env_press_pa = (uint32_t)env_press_pa;

    pkt.gps_lat_x1e6 = (int32_t)gps_lat_x1e6;
    pkt.gps_lon_x1e6 = (int32_t)gps_lon_x1e6;
    pkt.gps_alt_m    = (int16_t)gps_alt_m;
    pkt.gps_satellites = (uint8_t)gps_satellites;

    for (int i = 0; i < 4; i++) {
        pkt.task[i].resp_max_x10us  = (uint16_t)task_resp_max[i];
        pkt.task[i].resp_avg_x10us  = (uint16_t)task_resp_avg[i];
        pkt.task[i].exec_count      = (uint16_t)task_exec_count[i];
        pkt.task[i].deadline_misses = (uint16_t)task_deadline_misses[i];
    }

    send_packet();
    seq_counter = pkt.seq + 1;

    server.sendHeader("Location", "/manual");
    server.send(303);
}

void handle_status_json() {
    String json = "{";
    json += "\"active_scenario\":\"" + String(scenario_name(active_scenario)) + "\",";
    json += "\"elapsed_ms\":" + String(active_scenario == SCN_IDLE ? 0 : (millis() - scenario_start_ms)) + ",";
    json += "\"seq_counter\":" + String(seq_counter) + ",";
    json += "\"uptime_ms\":" + String(millis());
    json += "}";
    server.send(200, "application/json", json);
}

void handle_not_found() {
    server.send(404, "text/plain", "Not found: " + server.uri());
}

// ---------------------------------------------------------------------------
// Emit scenario frame
// ---------------------------------------------------------------------------
void emit_scenario_frame(const ScenarioFrame &f) {
    pkt.magic       = MAGIC;
    pkt.version     = 1;
    pkt.seq         = seq_counter++;
    pkt.sched_mode  = (uint8_t)(cycle_counter % 3);
    pkt.run_id      = 1234;
    pkt.num_tasks   = 4;
    pkt.fault_flags = f.fault_flags;
    pkt.uptime_ms   = millis();
    pkt.cpu_load_x100 = f.cpu_load_x100;
    pkt.total_misses  = f.task0_deadline_misses;
    pkt.veh_flags      = f.veh_flags;
    pkt.veh_speed_x10  = f.veh_speed_x10;
    pkt.veh_accel_x100 = f.veh_accel_x100;
    pkt.bcm_wiper_speed = f.bcm_wiper_speed;
    pkt.acm_throttle_x100 = f.acm_throttle_x100;
    pkt.acm_brake_pa      = f.acm_brake_pa;
    pkt.acm_fsr_raw       = f.acm_fsr_raw;
    pkt.acm_status        = f.acm_status;
    pkt.seat_position_deg = 0;
    pkt.seat_profile      = 0;
    pkt.env_temp_x10 = f.env_temp_x10;
    pkt.env_hum_x100 = f.env_hum_x100;
    pkt.env_press_pa = 101325;
    pkt.gps_lat_x1e6 = 37774900;
    pkt.gps_lon_x1e6 = -122419400;
    pkt.gps_alt_m    = 10;
    pkt.gps_satellites = f.gps_satellites;

    pkt.task[0].resp_max_x10us  = f.task0_resp_max_x10us;
    pkt.task[0].resp_avg_x10us  = (uint16_t)(f.task0_resp_max_x10us * 0.7f);
    pkt.task[0].exec_count      = (uint16_t)(1000 + cycle_counter);
    pkt.task[0].deadline_misses = f.task0_deadline_misses;
    for (int i = 1; i < 4; i++) {
        pkt.task[i].resp_max_x10us    = (uint16_t)(100 + i + (cycle_counter % 30));
        pkt.task[i].resp_avg_x10us    = (uint16_t)(50 + i + (cycle_counter % 15));
        pkt.task[i].exec_count        = (uint16_t)(1000 + i + cycle_counter);
        pkt.task[i].deadline_misses   = 0;
    }

    cycle_counter++;
    send_packet();
}

void step_active_scenario() {
    if (active_scenario == SCN_IDLE) return;

    uint32_t now = millis();
    if (now - last_scenario_emit_ms < SCENARIO_EMIT_INTERVAL_MS) return;
    last_scenario_emit_ms = now;

    uint32_t elapsed = now - scenario_start_ms;
    ScenarioFrame f;
    switch (active_scenario) {
        case SCN_HARD_ACCEL:     f = scenario_hard_accel(elapsed); break;
        case SCN_HARD_BRAKE:     f = scenario_hard_brake(elapsed); break;
        case SCN_ACM_FAULT:      f = scenario_acm_fault(elapsed, (uint8_t)(scenario_variant_counter % 2)); break;
        case SCN_SCHED_OVERLOAD: f = scenario_sched_overload(elapsed, cycle_counter); break;
        case SCN_SENSOR_FAULT:   f = scenario_sensor_fault(elapsed); break;
        case SCN_TURN_SIGNALS:   f = scenario_turn_signals(elapsed); break;
        case SCN_HAZARD_LIGHTS:  f = scenario_hazard_lights(elapsed); break;
        case SCN_DOOR_OPEN:      f = scenario_door_open(elapsed); break;
        case SCN_SEATBELT_WARN:  f = scenario_seatbelt_warning(elapsed); break;
        case SCN_CRASH:          f = scenario_crash(elapsed); break;
        case SCN_RAIN_WIPER:     f = scenario_rain_wiper(elapsed); break;
        case SCN_ENGINE_FAULT:   f = scenario_engine_fault(elapsed); break;
        case SCN_ABS_FAULT:      f = scenario_abs_fault(elapsed); break;
        case SCN_LOW_FUEL:       f = scenario_low_fuel(elapsed); break;
        case SCN_BATTERY_WARN:   f = scenario_battery_warn(elapsed); break;
        case SCN_COMBINED:       f = scenario_combined_driving(elapsed); break;
        default:                 f = scenario_baseline(); break;
    }

    emit_scenario_frame(f);

    if (f.done) {
        active_scenario = SCN_IDLE;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    memset(&pkt, 0, sizeof(pkt));

    WiFi.softAP(AP_SSID);

    server.on("/", HTTP_GET, handle_root);
    server.on("/scenario/start", HTTP_POST, handle_scenario_start);
    server.on("/scenario/stop", HTTP_POST, handle_scenario_stop);
    server.on("/manual", HTTP_GET, handle_manual_form);
    server.on("/manual/send", HTTP_POST, handle_manual_send);
    server.on("/status.json", HTTP_GET, handle_status_json);
    server.onNotFound(handle_not_found);
    server.begin();
}

void loop() {
    server.handleClient();
    step_active_scenario();
}