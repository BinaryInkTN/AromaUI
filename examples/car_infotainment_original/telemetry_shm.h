#ifndef TELEMETRY_BRIDGE_H
#define TELEMETRY_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#define SDV_TELEMETRY_VERSION   1u
#define SDV_TELEMETRY_MAGIC     0xA5u
#define UART_FRAME_START        0xAAu
#define SHM_KEY                 0x1234ABCD

#pragma pack(push, 1)

typedef struct {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  seq;
    uint8_t  flags;
    uint32_t uptime_ms;

    uint8_t  bcm_wiper_speed;
    uint8_t  bcm_headlight_state;
    uint8_t  bcm_indicator_left;
    uint8_t  bcm_indicator_right;
    uint8_t  bcm_buzzer;
    uint8_t  bcm_door_locked;
    uint8_t  bcm_interior_light;
    uint8_t  bcm_rain_detected;
    uint8_t  bcm_door_open;

    uint16_t acm_throttle_cmd;
    uint16_t acm_brake_cmd;
    uint16_t acm_fsr_value;
    uint16_t acm_vehicle_speed;
    uint8_t  acm_crash_detected;
    uint8_t  acm_airbag_deployed;
    uint8_t  acm_seatbelt_warn;
    uint8_t  acm_seat_occupied;
    uint8_t  acm_system_status;

    int16_t  seat_position_cmd;
    uint8_t  seat_profile;
    uint8_t  seat_occupied;

    uint16_t vss_speed_raw;
    uint16_t vss_speed_filtered;
    int16_t  vss_acceleration;
    uint16_t vss_avg_speed;
    uint16_t vss_max_speed;
    uint32_t vss_distance;
    uint32_t vss_kinetic_energy;
    uint8_t  vss_high_speed_flag;
    uint8_t  vss_harsh_braking;
    uint8_t  vss_sensor_fault;

    int16_t  ecs_temp_c;
    uint16_t ecs_humidity;
    int16_t  ecs_dew_point_c;
    int16_t  ecs_altitude_m;
    uint32_t ecs_pressure_pa;
    uint8_t  ecs_sensor_fault;
    uint8_t  ecs_comfort_cold;
    uint8_t  ecs_comfort_hot;
    uint8_t  ecs_high_humidity;

} sdv_telemetry_t;

#pragma pack(pop)

#endif