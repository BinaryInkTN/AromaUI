#ifndef SHM_READER_H
#define SHM_READER_H

#include <stdint.h>
#include <stdbool.h>

#define SHM_NAME "/sdv_telemetry_shm"

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

typedef struct shm_reader shm_reader_t;

shm_reader_t *shm_reader_init(const char *shm_name);
bool shm_reader_get_state(shm_reader_t *reader, sdv_telemetry_t *out);
void shm_reader_shutdown(shm_reader_t *reader);

#endif