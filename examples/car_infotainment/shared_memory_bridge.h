/*
 * shared_memory_bridge.h
 * ======================
 * Read-only POSIX shared-memory telemetry bridge for the instrument cluster.
 *
 * All struct definitions (struct telemetry_frame, struct telemetry_shm_block),
 * segment constants (TELEMETRY_SHM_NAME, TELEMETRY_SHM_SIZE), message-id
 * macros, flag macros, and charge-flag macros come exclusively from
 * telemetry_shm.h — the writer's canonical header.  Nothing is redefined
 * here.  This is the fix for the three incompatibilities that previously
 * existed between the two headers:
 *
 *   Bug 1 – seq_write type mismatch
 *     Old reader used atomic_uint; writer uses atomic_uint_fast32_t.
 *     On x86-64 atomic_uint_fast32_t is 8 bytes, atomic_uint is 4 bytes,
 *     so offsetof(shm_block, frame) differed → every field read from the
 *     wrong address.  Fixed by removing the duplicate struct and using the
 *     writer's struct telemetry_shm_block directly.
 *
 *   Bug 2 – struct telemetry_frame field order mismatch
 *     Old reader put msg_id (enum, 4 bytes) first; writer puts speed_kmh_x10
 *     (uint16) first.  Every field was at the wrong offset.  Fixed by
 *     removing the duplicate struct and using the writer's definition.
 *
 *   Bug 3 – flag bit positions mismatch
 *     Old reader: FLAG_READY_TO_DRIVE=bit0, FLAG_REGEN_ACTIVE=bit1,
 *                 FLAG_LOW_BATTERY=bit2.
 *     Writer:     FLAG_READY_TO_DRIVE=bit3, FLAG_REGEN_ACTIVE=bit4,
 *                 FLAG_LOW_BATTERY=bit1.
 *     Also, old reader CHARGE_FLAG_CONNECTED/ACTIVE were independent macros
 *     with values that happened to match, but were not aliases for the
 *     writer's CHARGE_FLAG_PLUG_CONNECTED/CHARGING.
 *     Fixed by removing all duplicate flag macros and using the writer's.
 *
 * +---------------------------------+-------+----------+--------------------+
 * | Symbol / Function               | Kind  | Returns  | Notes              |
 * +---------------------------------+-------+----------+--------------------+
 * | TELEMETRY_SHM_NAME              | macro | -        | from telemetry_shm |
 * | TELEMETRY_SHM_SIZE              | macro | size_t   | from telemetry_shm |
 * | TELEMETRY_READ_MAX_RETRIES      | macro | -        | spin limit         |
 * | FLAG_READY_TO_DRIVE             | macro | -        | from telemetry_shm |
 * | FLAG_REGEN_ACTIVE               | macro | -        | from telemetry_shm |
 * | FLAG_LOW_BATTERY                | macro | -        | from telemetry_shm |
 * | FLAG_CHECK_SYSTEM               | macro | -        | from telemetry_shm |
 * | FLAG_DOOR_OPEN                  | macro | -        | from telemetry_shm |
 * | CHARGE_FLAG_PLUG_CONNECTED      | macro | -        | from telemetry_shm |
 * | CHARGE_FLAG_CHARGING            | macro | -        | from telemetry_shm |
 * | CHARGE_FLAG_FAULT               | macro | -        | from telemetry_shm |
 * | CHARGE_FLAG_DC_FAST             | macro | -        | from telemetry_shm |
 * | CHARGE_FLAG_PRECONDITIONING     | macro | -        | from telemetry_shm |
 * | struct telemetry_frame          | struct| -        | from telemetry_shm |
 * | struct telemetry_shm_block      | struct| -        | from telemetry_shm |
 * | telemetry_bridge_t              | struct| -        | reader handle      |
 * | telemetry_bridge_open()         | fn    | int 0/1  | attach shm         |
 * | telemetry_bridge_close()        | fn    | void     | detach shm         |
 * | telemetry_bridge_read()         | fn    | 1/0/-1   | seqlock read       |
 * | telemetry_speed_kmh()           | fn    | float    | km/h               |
 * | telemetry_torque_nm()           | fn    | float    | N·m                |
 * | telemetry_voltage_v()           | fn    | float    | V                  |
 * | telemetry_current_a()           | fn    | float    | A                  |
 * | telemetry_ready()               | fn    | int 0/1  | FLAG_READY_TO_DRIVE|
 * | telemetry_regen_active()        | fn    | int 0/1  | FLAG_REGEN_ACTIVE  |
 * | telemetry_low_battery()         | fn    | int 0/1  | FLAG_LOW_BATTERY   |
 * | telemetry_check_system()        | fn    | int 0/1  | FLAG_CHECK_SYSTEM  |
 * | telemetry_door_open()           | fn    | int 0/1  | FLAG_DOOR_OPEN     |
 * | telemetry_charge_connected()    | fn    | int 0/1  | PLUG_CONNECTED     |
 * | telemetry_charge_active()       | fn    | int 0/1  | CHARGING           |
 * | telemetry_charge_fault()        | fn    | int 0/1  | CHARGE_FAULT       |
 * | telemetry_charge_dc_fast()      | fn    | int 0/1  | DC_FAST            |
 * | telemetry_preconditioning()     | fn    | int 0/1  | PRECONDITIONING    |
 * +---------------------------------+-------+----------+--------------------+
 */

#ifndef SHARED_MEMORY_BRIDGE_H
#define SHARED_MEMORY_BRIDGE_H

/*
 * Feature-test macros must precede all system headers.  Guard against
 * double-definition in case the including translation unit already set them.
 */
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 700
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/*
 * The writer's header is the single source of truth for:
 *   - struct telemetry_frame      (field order, types, padding)
 *   - struct telemetry_shm_block  (atomic type for seq_write, frame offset)
 *   - TELEMETRY_SHM_NAME / TELEMETRY_SHM_SIZE
 *   - All FLAG_* and CHARGE_FLAG_* bit positions
 *   - All TELEMETRY_MSG_* message-id values
 *
 * Do NOT redeclare any of these below.
 */
#include "telemetry_shm.h"

/* -------------------------------------------------------------------------
 * Reader tuning constant
 * ---------------------------------------------------------------------- */

/*
 * Maximum number of *torn* reads (completed memcpy with seq0 != seq1) before
 * telemetry_bridge_read() gives up and returns -1.  Odd-seq spins (writer
 * in progress) do not count against this limit.
 */
#define TELEMETRY_READ_MAX_RETRIES  100000u

/* -------------------------------------------------------------------------
 * telemetry_bridge_t  –  opaque reader handle
 * ---------------------------------------------------------------------- */
typedef struct {
    int                        fd;   /* shm_open descriptor; -1 when closed */
    struct telemetry_shm_block *shm; /* mmap base; NULL when not mapped     */
} telemetry_bridge_t;

/* -------------------------------------------------------------------------
 * telemetry_bridge_open
 *
 * Opens the POSIX shared-memory segment created by the writer process and
 * maps it read-only into the caller's address space.
 *
 * The writer must have already called shm_open(O_CREAT|O_RDWR) and
 * ftruncate(TELEMETRY_SHM_SIZE) before this function is invoked; otherwise
 * shm_open will fail with ENOENT or mmap will fail with EINVAL.
 *
 * Returns 1 on success, 0 on failure (errno set; error printed to stderr).
 * ---------------------------------------------------------------------- */
static inline int telemetry_bridge_open(telemetry_bridge_t *bridge)
{
    void *mapped;

    if (!bridge)
        return 0;

    memset(bridge, 0, sizeof *bridge);
    bridge->fd  = -1;
    bridge->shm = NULL;

    bridge->fd = shm_open(TELEMETRY_SHM_NAME, O_RDONLY, 0666);
    if (bridge->fd < 0) {
        perror("telemetry_bridge_open: shm_open");
        return 0;
    }

    mapped = mmap(NULL, TELEMETRY_SHM_SIZE, PROT_READ, MAP_SHARED,
                  bridge->fd, 0);
    if (mapped == MAP_FAILED) {
        perror("telemetry_bridge_open: mmap");
        close(bridge->fd);
        bridge->fd = -1;
        return 0;
    }

    bridge->shm = (struct telemetry_shm_block *)mapped;
    return 1;
}

/* -------------------------------------------------------------------------
 * telemetry_bridge_close
 *
 * Unmaps the shared-memory region and closes the file descriptor.
 * Safe to call on a bridge that was never successfully opened.
 * ---------------------------------------------------------------------- */
static inline void telemetry_bridge_close(telemetry_bridge_t *bridge)
{
    if (!bridge)
        return;

    if (bridge->shm) {
        munmap(bridge->shm, TELEMETRY_SHM_SIZE);
        bridge->shm = NULL;
    }

    if (bridge->fd >= 0) {
        close(bridge->fd);
        bridge->fd = -1;
    }
}

/* -------------------------------------------------------------------------
 * telemetry_bridge_read
 *
 * Copies a consistent snapshot of the writer's telemetry frame into *out
 * using the seqlock protocol.
 *
 * Protocol:
 *   1. Spin (without counting retries) while seq_write is odd — the writer
 *      is mid-update.
 *   2. memcpy the frame.
 *   3. Acquire fence to prevent the seq1 reload from being speculated above
 *      the memcpy on weakly-ordered CPUs (ARM etc.).
 *   4. Re-read seq_write.  If it changed, the writer updated the frame
 *      while we were copying (torn read); increment retry counter and loop.
 *   5. Return 1 when seq0 == seq1 (clean read).
 *
 * Note: accessors (telemetry_speed_kmh etc.) must be called on the local
 * *out copy, never directly on shm->frame.
 *
 * Parameters:
 *   bridge      – open bridge handle
 *   out         – destination for the frame snapshot
 *   max_retries – torn-read limit; pass TELEMETRY_READ_MAX_RETRIES
 *
 * Returns:
 *    1   clean read, *out is valid
 *    0   bad arguments (NULL bridge, unopen shm, or NULL out)
 *   -1   max_retries torn reads exceeded (writer is starving the reader)
 * ---------------------------------------------------------------------- */
static inline int telemetry_bridge_read(telemetry_bridge_t     *bridge,
                                        struct telemetry_frame *out,
                                        unsigned                max_retries)
{
    unsigned retries = 0u;
    unsigned seq0, seq1;

    if (!bridge || !bridge->shm || !out)
        return 0;

    do {
        /*
         * Wait for any in-progress write to finish.  An odd seq_write
         * means the writer is between its two fetch_add calls.  This inner
         * spin does NOT consume a retry count.
         */
        do {
            seq0 = atomic_load_explicit(&bridge->shm->seq_write,
                                        memory_order_acquire);
        } while (seq0 & 1u);

        /* seq0 is even: writer is idle.  Snapshot the frame. */
        memcpy(out, &bridge->shm->frame, sizeof *out);

        /*
         * Acquire fence: on OoO CPUs prevents the seq1 reload below from
         * being reordered above the memcpy.  On x86 this compiles to nothing
         * but documents the architectural requirement.
         */
        atomic_thread_fence(memory_order_acquire);

        seq1 = atomic_load_explicit(&bridge->shm->seq_write,
                                    memory_order_acquire);

        if (seq0 != seq1) {
            /* Torn read: the writer updated mid-copy.  Count and retry. */
            if (++retries >= max_retries)
                return -1;
        }

    } while (seq0 != seq1);

    return 1;
}

/* =========================================================================
 * Accessor helpers
 *
 * Call these on the local telemetry_frame copy returned by
 * telemetry_bridge_read(), never on shm->frame directly.
 * ====================================================================== */

/** Vehicle speed in km/h (converts from fixed-point ×10 encoding). */
static inline float telemetry_speed_kmh(const struct telemetry_frame *f)
{
    return (float)f->speed_kmh_x10 / 10.0f;
}

/** Motor torque in N·m (converts from fixed-point ×10 encoding). */
static inline float telemetry_torque_nm(const struct telemetry_frame *f)
{
    return (float)f->motor_torque_nm_x10 / 10.0f;
}

/** Battery pack voltage in V (converts from deci-volt encoding). */
static inline float telemetry_voltage_v(const struct telemetry_frame *f)
{
    return (float)f->battery_voltage_dv / 10.0f;
}

/**
 * Battery pack current in A (converts from deci-ampere encoding).
 * Positive = discharging (driving).  Negative = regen or charging.
 */
static inline float telemetry_current_a(const struct telemetry_frame *f)
{
    return (float)f->battery_current_da / 10.0f;
}

/** Non-zero if the vehicle is ready to drive. */
static inline int telemetry_ready(const struct telemetry_frame *f)
{
    return (f->flags & FLAG_READY_TO_DRIVE) != 0;
}

/** Non-zero if regenerative braking is currently active. */
static inline int telemetry_regen_active(const struct telemetry_frame *f)
{
    return (f->flags & FLAG_REGEN_ACTIVE) != 0;
}

/** Non-zero if the battery state-of-charge is below the low threshold. */
static inline int telemetry_low_battery(const struct telemetry_frame *f)
{
    return (f->flags & FLAG_LOW_BATTERY) != 0;
}

/** Non-zero if a general system fault (check-system) is active. */
static inline int telemetry_check_system(const struct telemetry_frame *f)
{
    return (f->flags & FLAG_CHECK_SYSTEM) != 0;
}

/** Non-zero if any door is currently open. */
static inline int telemetry_door_open(const struct telemetry_frame *f)
{
    return (f->flags & FLAG_DOOR_OPEN) != 0;
}

/** Non-zero if the charge plug is physically connected. */
static inline int telemetry_charge_connected(const struct telemetry_frame *f)
{
    return (f->charge_flags & CHARGE_FLAG_PLUG_CONNECTED) != 0;
}

/** Non-zero if the vehicle is actively receiving charge current. */
static inline int telemetry_charge_active(const struct telemetry_frame *f)
{
    return (f->charge_flags & CHARGE_FLAG_CHARGING) != 0;
}

/** Non-zero if a charge fault condition is present. */
static inline int telemetry_charge_fault(const struct telemetry_frame *f)
{
    return (f->charge_flags & CHARGE_FLAG_FAULT) != 0;
}

/** Non-zero if DC fast-charge is in use. */
static inline int telemetry_charge_dc_fast(const struct telemetry_frame *f)
{
    return (f->charge_flags & CHARGE_FLAG_DC_FAST) != 0;
}

/** Non-zero if battery preconditioning is active. */
static inline int telemetry_preconditioning(const struct telemetry_frame *f)
{
    return (f->charge_flags & CHARGE_FLAG_PRECONDITIONING) != 0;
}

#endif /* SHARED_MEMORY_BRIDGE_H */