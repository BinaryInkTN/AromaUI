#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

#define TELEMETRY_SHM_NAME  "/telemetry"

/*
 * EV Telemetry wire frame (UART, 32 bytes)
 *
 *  [0]       0xAA          start-of-frame magic
 *  [1]       msg_id        0x01=motion  0x02=battery  0x03=climate  0x04=charge
 *  [2..3]    seq_le        little-endian uint16 sequence counter
 *
 *  --- motion ---
 *  [4..5]    speed_le      vehicle speed × 10  (km/h, uint16 LE)
 *  [6..7]    torque_le     motor torque × 10   (N·m,  uint16 LE)
 *  [8]       throttle_pct  0–100 %
 *  [9]       brake_pct     0–100 %
 *  [10]      regen_pct     0–100 %  (0=none, 100=full regen)
 *
 *  --- battery ---
 *  [11]      soc_pct       state-of-charge 0–100 %
 *  [12..13]  voltage_le    pack voltage in dV (uint16 LE; divide by 10 for V)
 *  [14..15]  current_le    pack current in dA (int16 LE;  divide by 10 for A;
 *                                              negative = regen / charging)
 *  [16]      battery_temp  battery temp °C + 40 offset (uint8, range −40..215)
 *
 *  --- range / charge ---
 *  [17..18]  range_le      estimated range in km (uint16 LE)
 *  [19]      charge_flags  bit0=plug_connected  bit1=charging  bit2=charge_fault
 *                          bit3=dc_fast_charge  bit4=preconditioning
 *
 *  --- climate ---
 *  [20]      temp_cabin    cabin temp °C + 40 offset
 *  [21]      temp_motor    motor temp °C + 40 offset
 *
 *  --- status ---
 *  [22]      flags         bit0=check-system   bit1=low-battery  bit2=door-open
 *                          bit3=ready-to-drive bit4=regen-active
 *
 *  [23..30]  reserved      zero-filled, reserved for future fields
 *  [31]      crc8          CRC-8/MAXIM over bytes [0..30]
 */

#define TELEMETRY_FRAME_LEN  32
#define TELEMETRY_SOF_MAGIC  0xAAu

/* msg_id values */
#define TELEMETRY_MSG_MOTION   0x01u
#define TELEMETRY_MSG_BATTERY  0x02u
#define TELEMETRY_MSG_CLIMATE  0x03u
#define TELEMETRY_MSG_CHARGE   0x04u

/* charge_flags bits */
#define CHARGE_FLAG_PLUG_CONNECTED   (1u << 0)
#define CHARGE_FLAG_CHARGING         (1u << 1)
#define CHARGE_FLAG_FAULT            (1u << 2)
#define CHARGE_FLAG_DC_FAST          (1u << 3)
#define CHARGE_FLAG_PRECONDITIONING  (1u << 4)

/* status flags bits */
#define FLAG_CHECK_SYSTEM    (1u << 0)
#define FLAG_LOW_BATTERY     (1u << 1)
#define FLAG_DOOR_OPEN       (1u << 2)
#define FLAG_READY_TO_DRIVE  (1u << 3)
#define FLAG_REGEN_ACTIVE    (1u << 4)

/*
 * In-memory decoded frame written by telemetry-bridge, read by consumers.
 *
 * Seqlock protocol (see telemetry_shm_block):
 *   Writer:  fetch_add(&seq_write, 1, release)  → odd (write in progress)
 *            frame = f;
 *            fetch_add(&seq_write, 1, release)  → even (done)
 *
 *   Reader:  do {
 *                s1 = load(&seq_write, acquire);
 *                if (s1 & 1) continue;
 *                local = frame;
 *                s2 = load(&seq_write, acquire);
 *            } while (s1 != s2);
 */
struct telemetry_frame {
    /* motion */
    uint16_t  speed_kmh_x10;       /* ÷10 for km/h                          */
    uint16_t  motor_torque_nm_x10; /* ÷10 for N·m                           */
    uint8_t   throttle_pct;        /* 0–100 %                              */
    uint8_t   brake_pct;           /* 0–100 %                              */
    uint8_t   regen_pct;           /* 0–100 %                              */

    /* battery */
    uint8_t   battery_soc_pct;     /* 0–100 %                              */
    uint16_t  battery_voltage_dv;  /* pack voltage dV; ÷10 → V            */
    int16_t   battery_current_da;  /* pack current dA; ÷10 → A; neg=regen */
    int8_t    battery_temp_c;      /* °C                                   */

    /* range / charge */
    uint16_t  range_km;            /* estimated remaining range            */
    uint8_t   charge_flags;        /* CHARGE_FLAG_* bits                   */

    /* climate */
    int8_t    temp_cabin_c;        /* °C                                   */
    int8_t    temp_motor_c;        /* °C                                   */

    /* status */
    uint8_t   flags;               /* FLAG_* bits                          */

    /* frame meta */
    uint16_t  seq;
    uint8_t   msg_id;              /* TELEMETRY_MSG_* values               */

    uint8_t   _pad[1];          

    /* timestamp of last successful update */
    struct timespec last_update;   /* CLOCK_MONOTONIC                      */
};

struct telemetry_shm_block {
    atomic_uint_fast32_t seq_write; 
    struct telemetry_frame frame;
};

#define TELEMETRY_SHM_SIZE  ((size_t)sizeof(struct telemetry_shm_block))