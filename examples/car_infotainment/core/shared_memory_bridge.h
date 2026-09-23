#ifndef TELEMETRY_READER_H
#define TELEMETRY_READER_H

#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "telemetry_shm.h"

typedef struct {
    uint32_t        frame_count;
    uint32_t        error_count;
    uint32_t        last_seq;
    sdv_telemetry_t telemetry;
} telemetry_shm_t;

#define TELEMETRY_READ_MAX_RETRIES  1000u

typedef struct {
    int              shm_id;
    telemetry_shm_t *shm;
} telemetry_bridge_t;

static inline int telemetry_bridge_open(telemetry_bridge_t *bridge)
{
    if (!bridge) return 0;

    memset(bridge, 0, sizeof(*bridge));
    bridge->shm_id = -1;
    bridge->shm    = NULL;

    bridge->shm_id = shmget(SHM_KEY, sizeof(telemetry_shm_t), 0666);
    if (bridge->shm_id < 0) {
        perror("telemetry_bridge_open: shmget");
        return 0;
    }

    bridge->shm = (telemetry_shm_t *)shmat(bridge->shm_id, NULL, SHM_RDONLY);
    if (bridge->shm == (void *)-1) {
        perror("telemetry_bridge_open: shmat");
        bridge->shm_id = -1;
        bridge->shm = NULL;
        return 0;
    }

    return 1;
}

static inline void telemetry_bridge_close(telemetry_bridge_t *bridge)
{
    if (!bridge) return;

    if (bridge->shm && bridge->shm != (void *)-1) {
        shmdt(bridge->shm);
        bridge->shm = NULL;
    }
    bridge->shm_id = -1;
}

static inline int telemetry_bridge_read(telemetry_bridge_t *bridge,
                                        sdv_telemetry_t    *out,
                                        unsigned            max_retries)
{
    telemetry_shm_t snap1, snap2;
    unsigned retries = 0;

    if (!bridge || !bridge->shm || !out) return 0;

    do {

        memcpy(&snap1, bridge->shm, sizeof(telemetry_shm_t));

        __asm__ __volatile__("" ::: "memory");

        memcpy(&snap2, bridge->shm, sizeof(telemetry_shm_t));

        if (snap1.frame_count == snap2.frame_count &&
            snap1.error_count == snap2.error_count &&
            snap1.last_seq    == snap2.last_seq) {

            memcpy(out, &snap1.telemetry, sizeof(sdv_telemetry_t));
            return 1;
        }

        if (++retries >= max_retries) return -1;

    } while (1);
}

static inline bool telemetry_frame_valid(const sdv_telemetry_t *f)
{
    return (f->magic == SDV_TELEMETRY_MAGIC &&
            f->version == SDV_TELEMETRY_VERSION);
}

static inline uint8_t telemetry_sequence(const sdv_telemetry_t *f)
{
    return f->seq;
}

static inline uint8_t telemetry_flags(const sdv_telemetry_t *f)
{
    return f->flags;
}

static inline uint32_t telemetry_uptime_ms(const sdv_telemetry_t *f)
{
    return f->uptime_ms;
}

static inline uint8_t telemetry_wiper_speed(const sdv_telemetry_t *f)
{
    return f->bcm_wiper_speed;
}

static inline uint8_t telemetry_headlight(const sdv_telemetry_t *f)
{
    return f->bcm_headlight_state;
}

static inline bool telemetry_indicator_left(const sdv_telemetry_t *f)
{
    return f->bcm_indicator_left != 0;
}

static inline bool telemetry_indicator_right(const sdv_telemetry_t *f)
{
    return f->bcm_indicator_right != 0;
}

static inline bool telemetry_buzzer(const sdv_telemetry_t *f)
{
    return f->bcm_buzzer != 0;
}

static inline bool telemetry_door_locked(const sdv_telemetry_t *f)
{
    return f->bcm_door_locked != 0;
}

static inline bool telemetry_interior_light(const sdv_telemetry_t *f)
{
    return f->bcm_interior_light != 0;
}

static inline bool telemetry_rain_detected(const sdv_telemetry_t *f)
{
    return f->bcm_rain_detected != 0;
}

static inline bool telemetry_door_open(const sdv_telemetry_t *f)
{
    return f->bcm_door_open != 0;
}

static inline uint16_t telemetry_throttle_raw(const sdv_telemetry_t *f)
{
    return f->acm_throttle_cmd;
}

static inline uint16_t telemetry_brake_raw(const sdv_telemetry_t *f)
{
    return f->acm_brake_cmd;
}

static inline uint16_t telemetry_fsr(const sdv_telemetry_t *f)
{
    return f->acm_fsr_value;
}

static inline uint16_t telemetry_vehicle_speed_raw(const sdv_telemetry_t *f)
{
    return f->acm_vehicle_speed;
}

static inline bool telemetry_crash_detected(const sdv_telemetry_t *f)
{
    return f->acm_crash_detected != 0;
}

static inline bool telemetry_airbag_deployed(const sdv_telemetry_t *f)
{
    return f->acm_airbag_deployed != 0;
}

static inline bool telemetry_seatbelt_warning(const sdv_telemetry_t *f)
{
    return f->acm_seatbelt_warn != 0;
}

static inline bool telemetry_seat_occupied(const sdv_telemetry_t *f)
{
    return f->acm_seat_occupied != 0;
}

static inline uint8_t telemetry_acm_status(const sdv_telemetry_t *f)
{
    return f->acm_system_status;
}

static inline int16_t telemetry_seat_position(const sdv_telemetry_t *f)
{
    return f->seat_position_cmd;
}

static inline uint8_t telemetry_seat_profile(const sdv_telemetry_t *f)
{
    return f->seat_profile;
}

static inline uint16_t telemetry_vss_speed_raw(const sdv_telemetry_t *f)
{
    return f->vss_speed_raw;
}

static inline uint16_t telemetry_vss_speed_filtered(const sdv_telemetry_t *f)
{
    return f->vss_speed_filtered;
}

static inline int16_t telemetry_acceleration_raw(const sdv_telemetry_t *f)
{
    return f->vss_acceleration;
}

static inline uint16_t telemetry_avg_speed(const sdv_telemetry_t *f)
{
    return f->vss_avg_speed;
}

static inline uint16_t telemetry_max_speed(const sdv_telemetry_t *f)
{
    return f->vss_max_speed;
}

static inline uint32_t telemetry_distance(const sdv_telemetry_t *f)
{
    return f->vss_distance;
}

static inline uint32_t telemetry_kinetic_energy(const sdv_telemetry_t *f)
{
    return f->vss_kinetic_energy;
}

static inline bool telemetry_high_speed_warning(const sdv_telemetry_t *f)
{
    return f->vss_high_speed_flag != 0;
}

static inline bool telemetry_harsh_braking(const sdv_telemetry_t *f)
{
    return f->vss_harsh_braking != 0;
}

static inline bool telemetry_vss_fault(const sdv_telemetry_t *f)
{
    return f->vss_sensor_fault != 0;
}

static inline float telemetry_temp_c(const sdv_telemetry_t *f)
{
    return f->ecs_temp_c / 100.0f;
}

static inline float telemetry_humidity_pct(const sdv_telemetry_t *f)
{
    return f->ecs_humidity / 100.0f;
}

static inline float telemetry_dew_point_c(const sdv_telemetry_t *f)
{
    return f->ecs_dew_point_c / 100.0f;
}

static inline float telemetry_altitude_m(const sdv_telemetry_t *f)
{
    return f->ecs_altitude_m / 100.0f;
}

static inline uint32_t telemetry_pressure_pa(const sdv_telemetry_t *f)
{
    return f->ecs_pressure_pa;
}

static inline bool telemetry_ecs_fault(const sdv_telemetry_t *f)
{
    return f->ecs_sensor_fault != 0;
}

static inline bool telemetry_comfort_cold(const sdv_telemetry_t *f)
{
    return f->ecs_comfort_cold != 0;
}

static inline bool telemetry_comfort_hot(const sdv_telemetry_t *f)
{
    return f->ecs_comfort_hot != 0;
}

static inline bool telemetry_high_humidity(const sdv_telemetry_t *f)
{
    return f->ecs_high_humidity != 0;
}

#endif
