/*
 * shared_memory_bridge.h
 * ======================
 * Read-only POSIX shared-memory telemetry bridge for instrument cluster
 * application.  A single writer process owns the segment; any number of
 * reader processes may attach concurrently without locks by using a
 * seqlock: the writer increments seq_write to an odd value before touching
 * the frame and back to an even value when done.  Readers spin until they
 * observe two identical even sequence numbers around their memcpy.
 *
 * Writer contract
 * ---------------
 * The writer must call shm_open(O_CREAT|O_RDWR) + ftruncate(TELEMETRY_SHM_SIZE)
 * and initialise seq_write to 0 before any reader attaches.  Every frame
 * update must bracket the write with an odd seq_write increment before and
 * an even increment after, using memory_order_release on both stores.
 *
 * +---------------------------------+-------+----------+--------------------+
 * | Symbol / Function               | Kind  | Returns  | Notes              |
 * +---------------------------------+-------+----------+--------------------+
 * | TELEMETRY_SHM_NAME              | macro | -        | shm_open path      |
 * | TELEMETRY_READ_MAX_RETRIES      | macro | -        | spin limit         |
 * | FLAG_READY_TO_DRIVE             | macro | -        | flags bitmask      |
 * | FLAG_REGEN_ACTIVE               | macro | -        | flags bitmask      |
 * | FLAG_LOW_BATTERY                | macro | -        | flags bitmask      |
 * | CHARGE_FLAG_CONNECTED           | macro | -        | charge_flags mask  |
 * | CHARGE_FLAG_ACTIVE              | macro | -        | charge_flags mask  |
 * | CHARGE_FLAG_FAULT               | macro | -        | charge_flags mask  |
 * | telemetry_msg_id_t              | enum  | -        | frame message type |
 * | telemetry_frame_t               | struct| -        | telemetry payload  |
 * | telemetry_shm_block_t           | struct| -        | full shm layout    |
 * | TELEMETRY_SHM_SIZE              | macro | size_t   | segment byte size  |
 * | telemetry_bridge_t              | struct| -        | reader handle      |
 * | telemetry_bridge_open()         | fn    | int 0/1  | attach shm         |
 * | telemetry_bridge_close()        | fn    | void     | detach shm         |
 * | telemetry_bridge_read()         | fn    | 1/0/-1   | seqlock read       |
 * | telemetry_speed_kmh()           | fn    | float    | km/h               |
 * | telemetry_torque_nm()           | fn    | float    | N*m                |
 * | telemetry_voltage_v()           | fn    | float    | V                  |
 * | telemetry_current_a()           | fn    | float    | A                  |
 * | telemetry_ready()               | fn    | int 0/1  | FLAG_READY_TO_DRIVE|
 * | telemetry_regen_active()        | fn    | int 0/1  | FLAG_REGEN_ACTIVE  |
 * | telemetry_low_battery()         | fn    | int 0/1  | FLAG_LOW_BATTERY   |
 * | telemetry_charge_connected()    | fn    | int 0/1  | CHARGE_FLAG_CONN.  |
 * | telemetry_charge_active()       | fn    | int 0/1  | CHARGE_FLAG_ACTIVE |
 * | telemetry_charge_fault()        | fn    | int 0/1  | CHARGE_FLAG_FAULT  |
 * +---------------------------------+-------+----------+--------------------+
 */

#ifndef SHARED_MEMORY_BRIDGE_H
#define SHARED_MEMORY_BRIDGE_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define TELEMETRY_SHM_NAME          "/telemetry"
#define TELEMETRY_READ_MAX_RETRIES  100000u

#define FLAG_READY_TO_DRIVE         (1u << 0)
#define FLAG_REGEN_ACTIVE           (1u << 1)
#define FLAG_LOW_BATTERY            (1u << 2)

#define CHARGE_FLAG_CONNECTED       (1u << 0)
#define CHARGE_FLAG_ACTIVE          (1u << 1)
#define CHARGE_FLAG_FAULT           (1u << 2)

typedef enum
{
    TELEMETRY_MSG_NONE    = 0,
    TELEMETRY_MSG_MOTION,
    TELEMETRY_MSG_BATTERY,
    TELEMETRY_MSG_CLIMATE,
    TELEMETRY_MSG_CHARGE
} telemetry_msg_id_t;

typedef struct
{
    telemetry_msg_id_t  msg_id;

    uint16_t            speed_kmh_x10;
    uint16_t            motor_torque_nm_x10;

    uint8_t             throttle_pct;
    uint8_t             brake_pct;
    uint8_t             regen_pct;

    uint8_t             battery_soc_pct;
    uint16_t            battery_voltage_dv;
    int16_t             battery_current_da;
    int8_t              battery_temp_c;

    uint16_t            range_km;

    int8_t              temp_cabin_c;
    int8_t              temp_motor_c;

    uint8_t             charge_flags;
    uint8_t             flags;

    uint16_t            seq;

    struct timespec     last_update;
} telemetry_frame_t;

typedef struct
{
    atomic_uint         seq_write;
    telemetry_frame_t   frame;
} telemetry_shm_block_t;

#define TELEMETRY_SHM_SIZE ((size_t)sizeof(telemetry_shm_block_t))

typedef struct
{
    int                     fd;
    telemetry_shm_block_t  *shm;
} telemetry_bridge_t;

static inline int telemetry_bridge_open(telemetry_bridge_t *bridge)
{
    void *mapped;

    if (!bridge)
        return 0;

    memset(bridge, 0, sizeof(*bridge));
    bridge->fd = -1;

    bridge->fd = shm_open(TELEMETRY_SHM_NAME, O_RDONLY, 0666);
    if (bridge->fd < 0)
    {
        perror("shm_open");
        return 0;
    }

    mapped = mmap(NULL, TELEMETRY_SHM_SIZE, PROT_READ, MAP_SHARED, bridge->fd, 0);
    if (mapped == MAP_FAILED)
    {
        perror("mmap");
        close(bridge->fd);
        bridge->fd  = -1;
        bridge->shm = NULL;
        return 0;
    }

    bridge->shm = (telemetry_shm_block_t *)mapped;
    return 1;
}

static inline void telemetry_bridge_close(telemetry_bridge_t *bridge)
{
    if (!bridge)
        return;

    if (bridge->shm && bridge->shm != (telemetry_shm_block_t *)MAP_FAILED)
    {
        munmap(bridge->shm, TELEMETRY_SHM_SIZE);
        bridge->shm = NULL;
    }

    if (bridge->fd >= 0)
    {
        close(bridge->fd);
        bridge->fd = -1;
    }
}

/*
 * telemetry_bridge_read()
 * -----------------------
 * Seqlock reader.  Corrections vs. original:
 *
 * 1. Odd-seq spin no longer burns a retry count.  Retries only increment
 *    when we complete a full copy but find seq0 != seq1 (genuine torn read).
 *
 * 2. Removed the redundant acquire fences around memcpy.  The acquire load
 *    of seq0 already prevents the compiler/CPU from hoisting the memcpy
 *    before the seq0 load.  A single acquire fence after the copy ensures
 *    the seq1 re-read is not speculated before the copy completes.
 *
 * Returns:
 *   1   – clean read, *out is valid
 *   0   – bad arguments / NULL pointers
 *  -1   – max_retries torn reads exceeded
 */
static inline int telemetry_bridge_read(telemetry_bridge_t *bridge,
                                        telemetry_frame_t  *out,
                                        unsigned            max_retries)
{
    unsigned retries = 0u;
    unsigned seq0;
    unsigned seq1;

    if (!bridge || !bridge->shm || !out)
        return 0;

    do
    {
        /* Wait out any in-progress write without consuming a retry. */
        do
        {
            seq0 = atomic_load_explicit(&bridge->shm->seq_write,
                                        memory_order_acquire);
        } while (seq0 & 1u);

        /* Copy the frame.  seq0 is even so the writer is idle here. */
        memcpy(out, &bridge->shm->frame, sizeof(*out));

        /*
         * Acquire fence: prevent seq1 load from being speculated above
         * the memcpy on out-of-order CPUs (e.g. ARM).
         */
        atomic_thread_fence(memory_order_acquire);

        seq1 = atomic_load_explicit(&bridge->shm->seq_write,
                                    memory_order_acquire);

        /* Count only genuine torn reads (writer updated mid-copy). */
        if (seq0 != seq1)
        {
            if (++retries >= max_retries)
                return -1;
        }

    } while (seq0 != seq1);

    return 1;
}

static inline float telemetry_speed_kmh(const telemetry_frame_t *f)
{
    return (float)f->speed_kmh_x10 / 10.0f;
}

static inline float telemetry_torque_nm(const telemetry_frame_t *f)
{
    return (float)f->motor_torque_nm_x10 / 10.0f;
}

static inline float telemetry_voltage_v(const telemetry_frame_t *f)
{
    return (float)f->battery_voltage_dv / 10.0f;
}

static inline float telemetry_current_a(const telemetry_frame_t *f)
{
    return (float)f->battery_current_da / 10.0f;
}

static inline int telemetry_ready(const telemetry_frame_t *f)
{
    return (f->flags & FLAG_READY_TO_DRIVE) != 0;
}

static inline int telemetry_regen_active(const telemetry_frame_t *f)
{
    return (f->flags & FLAG_REGEN_ACTIVE) != 0;
}

static inline int telemetry_low_battery(const telemetry_frame_t *f)
{
    return (f->flags & FLAG_LOW_BATTERY) != 0;
}

static inline int telemetry_charge_connected(const telemetry_frame_t *f)
{
    return (f->charge_flags & CHARGE_FLAG_CONNECTED) != 0;
}

static inline int telemetry_charge_active(const telemetry_frame_t *f)
{
    return (f->charge_flags & CHARGE_FLAG_ACTIVE) != 0;
}

static inline int telemetry_charge_fault(const telemetry_frame_t *f)
{
    return (f->charge_flags & CHARGE_FLAG_FAULT) != 0;
}

#endif /* SHARED_MEMORY_BRIDGE_H */