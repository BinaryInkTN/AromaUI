#ifndef SHM_READER_H
#define SHM_READER_H

#include <stdint.h>
#include <stdbool.h>


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

typedef struct {
    uint32_t          frame_count;
    uint32_t          error_count;
    uint32_t          crc_error_count;
    uint32_t          last_seq;
    volatile uint32_t write_seq;     
    sdv_telemetry_t   telemetry;
} telemetry_shm_t;
#pragma pack(pop)


typedef struct {
    uint8_t  seq;
    uint8_t  sched_mode;
    uint16_t run_id;
    uint8_t  num_tasks;
    uint8_t  fault_flags;
    uint32_t uptime_ms;
    float    cpu_load_pct;
    uint16_t total_misses;

    float    speed_kmh;
    float    accel_ms2;
    float    throttle_pct;
    uint16_t brake_pa;
    uint16_t fsr_raw;
    uint8_t  acm_status;
    uint8_t  wiper_speed;

    bool rain;
    bool door_open;
    bool door_locked;
    bool headlight;
    bool wiper_on;
    bool indicator_l;
    bool indicator_r;
    bool crash;
    bool airbag;
    bool seatbelt_warn;
    bool seat_occupied;
    bool high_speed;
    bool harsh_braking;
    bool buzzer;
    bool interior_light;

    float    temp_c;
    float    humidity_pct;
    float    pressure_hpa;

    double   latitude;
    double   longitude;
    int16_t  altitude_m;
    uint8_t  satellites;

    int16_t  seat_position_deg;
    uint8_t  seat_profile;

    struct {
        float    resp_max_ms;
        float    resp_avg_ms;
        uint16_t exec_count;
        uint16_t deadline_misses;
    } task[4];

    uint32_t bridge_frame_count;
    uint32_t bridge_error_count;
    uint32_t bridge_crc_error_count;

    uint32_t state_version; 
} telemetry_state_t;


typedef struct shm_reader shm_reader_t;



/**
 * @brief  Initialize the reader, attach shared memory, spawn background thread.
 * @param  key  System V shared memory key (e.g. 0x1234ABCD).
 * @return Pointer to reader handle, or NULL on failure.
 */
shm_reader_t *shm_reader_init(uint32_t key);

/**
 * @brief  Get latest state (non-blocking, thread-safe).
 * @param  reader  Reader handle.
 * @param  out     Output state struct.
 * @return true if new state was available, false otherwise.
 */
bool shm_reader_get_state(shm_reader_t *reader, telemetry_state_t *out);

/**
 * @brief  Shutdown reader, stop thread, detach shared memory.
 * @param  reader  Reader handle.
 */
void shm_reader_shutdown(shm_reader_t *reader);

#endif