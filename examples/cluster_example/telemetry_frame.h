















































#ifndef TELEMETRY_FRAME_H
#define TELEMETRY_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif




#define SDV_TELEMETRY_VERSION   3u



#define SDV_TELEMETRY_MAGIC     0xA5u


#define SDV_FAULT_VSS       (1u << 0)
#define SDV_FAULT_BME280    (1u << 1)
#define SDV_FAULT_GPS       (1u << 2)
#define SDV_GPS_FIX_VALID   (1u << 3)


#define SDV_VEH_RAIN            (1u << 0)
#define SDV_VEH_DOOR_OPEN       (1u << 1)
#define SDV_VEH_DOOR_LOCKED     (1u << 2)
#define SDV_VEH_HEADLIGHT       (1u << 3)
#define SDV_VEH_WIPER_ON        (1u << 4)
#define SDV_VEH_INDICATOR_L     (1u << 5)
#define SDV_VEH_INDICATOR_R     (1u << 6)
#define SDV_VEH_CRASH           (1u << 7)
#define SDV_VEH_AIRBAG          (1u << 8)
#define SDV_VEH_SEATBELT_WARN   (1u << 9)
#define SDV_VEH_SEAT_OCCUPIED   (1u << 10)
#define SDV_VEH_HIGH_SPEED      (1u << 11)
#define SDV_VEH_HARSH_BRAKING   (1u << 12)
#define SDV_VEH_BUZZER          (1u << 13)
#define SDV_VEH_INTERIOR_LIGHT  (1u << 14)

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



_Static_assert(sizeof(sdv_telemetry_t) <= 255,
               "sdv_telemetry_t depasse 255 octets : reduire sdv_task_stats_t "
               "ou passer LENGTH a 2 octets dans frame.h/protocol_uart.c");



uint16_t telemetry_frame_build(sdv_telemetry_t *out);


void     telemetry_frame_send(void);





void     telemetry_frame_send_text(void);

#ifdef __cplusplus
}
#endif

#endif
