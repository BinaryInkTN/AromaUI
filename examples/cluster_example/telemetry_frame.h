/**
 ******************************************************************************
 * @file    telemetry_frame.h
 * @brief   Trame de télémétrie unique STM32 -> Raspberry Pi
 *
 * UNE SEULE trame, orientée OBSERVATION DE L'ORDONNANCEUR : le Raspberry Pi
 * est l'instrument de mesure qui compare RM / EDF / CBS (diagrammes temps de
 * réponse vs deadline, deadline misses, charge CPU). La trame embarque en plus
 * l'essentiel des capteurs : GPS, BME280 (temp/hum/pression) et l'état
 * véhicule condensé en bitmasks.
 *
 * La structure ci-dessous EST le payload : packée, little-endian (natif
 * STM32F4 -> struct.unpack('<...') direct côté Python).
 *
 * ── Trame complète sur le fil (ajoutée par protocol_send) ──────────────────
 *
 *   +------+---------+--------------------+--------+----------------+--------+
 *   | 0xAA | VERSION | DATA_TELEMETRY_ALL | LENGTH |  sdv_telemetry_t | CRC16 |
 *   | 1 o  |  1 o    |       1 o (0x45)   |  1 o   |   LENGTH octets  | 2 o LE |
 *   +------+---------+--------------------+--------+----------------+--------+
 *                                          ^                        ^
 *                                          = sizeof(sdv_telemetry_t)|
 *                                                CRC16 calculé par protocol_send()
 *
 * NB CRC : protocol_send() calcule le CRC16 (IBM/0xA001) sur l'en-tête PUIS le
 *          XOR avec le CRC du payload (cf. Protocol/Src/protocol_uart.c). Le
 *          décodeur RPi doit reproduire ce même schéma.
 *
 * ── Décodage Python (pyserial + struct) ────────────────────────────────────
 *
 *   FIXED = '<BBBBHBBIHH' 'HHh' 'B' 'HHHB' 'hB' 'hHI' 'iihB'    # 52 octets
 *   TASK  = 'HHHH'                                               # 8 o / tâche
 *   hdr   = struct.unpack_from(FIXED, payload, 0)
 *   num_tasks = hdr[5]
 *   tasks = [struct.unpack_from('<' + TASK, payload, 52 + 8*i)
 *            for i in range(num_tasks)]
 *
 * ── Notes d'exploitation côté RPi ──────────────────────────────────────────
 *   - exec_count et deadline_misses sont CUMULATIFS (le RPi fait les deltas :
 *     robuste à la perte de trames ; exec_count wrappe modulo 65536).
 *   - La PÉRIODE d'une tâche n'est pas transmise : période ≈ Δuptime/Δexec.
 *     La deadline = période (deadline implicite) -> ligne de deadline des
 *     diagrammes.
 *   - sched_mode + run_id permettent d'agréger par algorithme et de tracer
 *     RM vs EDF vs CBS côte à côte (run_id change à chaque bascule de mode).
 ******************************************************************************
 */

#ifndef TELEMETRY_FRAME_H
#define TELEMETRY_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version du format de télémétrie — incrémenter à CHAQUE changement de layout
 * (ajout/retrait/réordonnancement de champ). La RPi compare ce champ et refuse
 * un format inconnu plutôt que de mal décoder. */
#define SDV_TELEMETRY_VERSION   3u   /* v3 : trame ordonnanceur + capteurs condensés */

/* Octet magique en tête de payload : 2e garde-fou (après le 0xAA de la trame)
 * pour resynchroniser le décodeur si l'UART perd des octets. */
#define SDV_TELEMETRY_MAGIC     0xA5u

/* ── Bits de fault_flags ──────────────────────────────────────────────────── */
#define SDV_FAULT_VSS       (1u << 0)   /* capteur vitesse en défaut          */
#define SDV_FAULT_BME280    (1u << 1)   /* BME280 (ECS) en défaut             */
#define SDV_FAULT_GPS       (1u << 2)   /* module GPS en défaut               */
#define SDV_GPS_FIX_VALID   (1u << 3)   /* 1 = fix GPS valide                 */

/* ── Bits de veh_flags (état véhicule condensé, 1 bit par signal 0/1) ─────── */
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

/* ── Statistiques d'ordonnancement d'UNE tâche (8 octets) ──────────────────
 * L'indice dans le tableau task[] == sched_task_id_t (wcet_monitor.h).
 * Unité ×10 µs : plage 0..655,35 ms, saturée à 0xFFFF. */
typedef struct {
    uint16_t resp_max_x10us;   /* temps de réponse MAX (préemptions incluses) */
    uint16_t resp_avg_x10us;   /* temps de réponse MOYEN                      */
    uint16_t exec_count;       /* nb d'exécutions, CUMULATIF (wrap 65536)     */
    uint16_t deadline_misses;  /* échéances ratées, CUMULATIF (saturé)        */
} sdv_task_stats_t;

/* ── Trame unique : ordonnanceur + GPS + BME280 + état véhicule ──────────── */
typedef struct {

    /* —— En-tête + contexte ordonnanceur (16 o) ——————————————————————————— */
    uint8_t  magic;            /* = SDV_TELEMETRY_MAGIC (0xA5)                */
    uint8_t  version;          /* = SDV_TELEMETRY_VERSION                     */
    uint8_t  seq;              /* compteur roulant : la RPi détecte les pertes */
    uint8_t  sched_mode;       /* 0=RM 1=EDF 2=CBS (scheduler_mode_t)         */
    uint16_t run_id;           /* id de campagne, +1 à chaque changement de
                                  mode -> le RPi sépare les runs à comparer   */
    uint8_t  num_tasks;        /* = TASK_SCHED_MAX (taille du tableau task[]) */
    uint8_t  fault_flags;      /* SDV_FAULT_* + SDV_GPS_FIX_VALID             */
    uint32_t uptime_ms;        /* temps depuis boot                           */
    uint16_t cpu_load_x100;    /* charge CPU % ×100 (Δtemps busy / Δuptime)   */
    uint16_t total_misses;     /* Σ deadline misses toutes tâches (saturé)    */

    /* —— Véhicule : états 0/1 condensés + dynamique VSS (6 o) —————————————— */
    uint16_t veh_flags;        /* bitmask SDV_VEH_* (états 0/1 BCM/ACM/SEAT/VSS) */
    uint16_t veh_speed_x10;    /* vitesse filtrée VSS, km/h ×10               */
    int16_t  veh_accel_x100;   /* accélération, m/s² ×100                     */

    /* —— BCM (1 o) — le reste du BCM est dans veh_flags ———————————————————— */
    uint8_t  bcm_wiper_speed;  /* 0=off 1=slow 2=fast                         */

    /* —— ACM (7 o) — crash/airbag/belt/occupied dans veh_flags ————————————— *
     * Pas de vitesse ici : la SEULE vitesse km/h de la trame est celle du VSS
     * (veh_speed_x10, source de vérité vitesse).                             */
    uint16_t acm_throttle_x100;/* throttle % ×100                             */
    uint16_t acm_brake_pa;     /* pression de freinage, Pa                    */
    uint16_t acm_fsr_raw;      /* ADC FSR brut (0-4095)                       */
    uint8_t  acm_status;       /* 0=ok 1=crash 2=safe-mode                    */

    /* —— SEAT (3 o) — seat_occupied dans veh_flags ————————————————————————— */
    int16_t  seat_position_deg;/* angle siège (degrés)                        */
    uint8_t  seat_profile;     /* 0=manuel 1-3=profil                         */

    /* —— BME280 / environnement (8 o) —————————————————————————————————————— */
    int16_t  env_temp_x10;     /* °C ×10                                      */
    uint16_t env_hum_x100;     /* % HR ×100                                   */
    uint32_t env_press_pa;     /* pression, Pa                                */

    /* —— GPS (11 o) — pas de vitesse GPS : seule vitesse = VSS ————————————— */
    int32_t  gps_lat_x1e6;     /* degrés ×1e6 (N+ / S-)                       */
    int32_t  gps_lon_x1e6;     /* degrés ×1e6 (E+ / W-)                       */
    int16_t  gps_alt_m;        /* m                                           */
    uint8_t  gps_satellites;   /* nb de satellites                            */

    /* —— Ordonnanceur : stats par tâche (8 o × TASK_SCHED_MAX) ————————————— */
    sdv_task_stats_t task[4];

} sdv_telemetry_t;

#pragma pack(pop)

/* La trame doit tenir dans le champ LENGTH (1 octet) du protocole.
 * 52 o fixes + 8 o/tâche : OK jusqu'à 25 tâches (24 avec ENABLE_STRESS). */
_Static_assert(sizeof(sdv_telemetry_t) <= 255,
               "sdv_telemetry_t depasse 255 octets : reduire sdv_task_stats_t "
               "ou passer LENGTH a 2 octets dans frame.h/protocol_uart.c");

/* ── API (implémentation : Protocol/Src/telemetry_frame.c) ──────────────── *
 * Remplit `out` depuis le DataBus + le wcet_monitor. Retourne sizeof.       */
uint16_t telemetry_frame_build(sdv_telemetry_t *out);

/* Construit puis émet la trame via protocol_send(DATA_TELEMETRY_ALL, ...).   */
void     telemetry_frame_send(void);

/* Construit puis émet la MÊME trame en TEXTE ASCII lisible directement sur
 * USART2 (BSP_UART_Send) — pour visualiser les données dans PuTTY côté RPi/PC.
 * Valeurs affichées BRUTES (virgule fixe) ; l'échelle de chaque champ est
 * documentée ci-dessus dans sdv_telemetry_t. */
void     telemetry_frame_send_text(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_FRAME_H */
