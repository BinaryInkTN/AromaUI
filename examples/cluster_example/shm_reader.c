#include "shm_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>


struct shm_reader {
    telemetry_shm_t   *shm;              
    pthread_t          thread;            
    volatile bool      running;           

    pthread_mutex_t    mutex;             
    pthread_cond_t     cond;              
    telemetry_state_t  current_state;     
    volatile bool      new_data_available;
};


static void frame_to_state(const sdv_telemetry_t *frame,
                           uint32_t frame_count, uint32_t error_count,
                           uint32_t crc_error_count, telemetry_state_t *state);
static bool shm_read_robust(telemetry_shm_t *shm,
                            sdv_telemetry_t *out_raw,
                            uint32_t *out_fc, uint32_t *out_ec, uint32_t *out_cec);
static void *reader_thread_fn(void *arg);


static void frame_to_state(const sdv_telemetry_t *frame,
                           uint32_t frame_count, uint32_t error_count,
                           uint32_t crc_error_count, telemetry_state_t *state)
{
    memset(state, 0, sizeof(*state));

    state->seq          = frame->seq;
    state->sched_mode   = frame->sched_mode;
    state->run_id       = frame->run_id;
    state->num_tasks    = frame->num_tasks;
    state->fault_flags  = frame->fault_flags;
    state->uptime_ms    = frame->uptime_ms;
    state->cpu_load_pct = frame->cpu_load_x100 / 100.0f;
    state->total_misses = frame->total_misses;

    state->speed_kmh     = frame->veh_speed_x10 / 10.0f;
    state->accel_ms2     = frame->veh_accel_x100 / 100.0f;
    state->throttle_pct  = frame->acm_throttle_x100 / 100.0f;
    state->brake_pa      = frame->acm_brake_pa;
    state->fsr_raw       = frame->acm_fsr_raw;
    state->acm_status    = frame->acm_status;
    state->wiper_speed   = frame->bcm_wiper_speed;

    uint16_t f = frame->veh_flags;
    state->rain           = (f >> 0) & 1;
    state->door_open      = (f >> 1) & 1;
    state->door_locked    = (f >> 2) & 1;
    state->headlight      = (f >> 3) & 1;
    state->wiper_on       = (f >> 4) & 1;
    state->indicator_l    = (f >> 5) & 1;
    state->indicator_r    = (f >> 6) & 1;
    state->crash          = (f >> 7) & 1;
    state->airbag         = (f >> 8) & 1;
    state->seatbelt_warn  = (f >> 9) & 1;
    state->seat_occupied  = (f >> 10) & 1;
    state->high_speed     = (f >> 11) & 1;
    state->harsh_braking  = (f >> 12) & 1;
    state->buzzer         = (f >> 13) & 1;
    state->interior_light = (f >> 14) & 1;

    state->temp_c       = frame->env_temp_x10 / 10.0f;
    state->humidity_pct = frame->env_hum_x100 / 100.0f;
    state->pressure_hpa = frame->env_press_pa / 100.0f;

    state->latitude    = frame->gps_lat_x1e6 / 1000000.0;
    state->longitude   = frame->gps_lon_x1e6 / 1000000.0;
    state->altitude_m  = frame->gps_alt_m;
    state->satellites  = frame->gps_satellites;

    state->seat_position_deg = frame->seat_position_deg;
    state->seat_profile      = frame->seat_profile;

    for (int i = 0; i < 4; i++) {
        state->task[i].resp_max_ms     = frame->task[i].resp_max_x10us / 100.0f;
        state->task[i].resp_avg_ms     = frame->task[i].resp_avg_x10us / 100.0f;
        state->task[i].exec_count      = frame->task[i].exec_count;
        state->task[i].deadline_misses = frame->task[i].deadline_misses;
    }

    state->bridge_frame_count    = frame_count;
    state->bridge_error_count    = error_count;
    state->bridge_crc_error_count = crc_error_count;

    state->state_version++;
}


static bool shm_read_robust(telemetry_shm_t *shm,
                            sdv_telemetry_t *out_raw,
                            uint32_t *out_fc, uint32_t *out_ec, uint32_t *out_cec)
{
    uint32_t s1, s2;
    int retry = 0;
    const int max_retries = 3;

    do {
        s1 = shm->write_seq;

       
        if (s1 & 1) {
            usleep(100);
            retry++;
            if (retry > max_retries) {
                return false; 
            }
            continue;
        }

       
        *out_raw = shm->telemetry;
        *out_fc  = shm->frame_count;
        *out_ec  = shm->error_count;
        *out_cec = shm->crc_error_count;

       
        __sync_synchronize();

        s2 = shm->write_seq;
        retry++;
    } while (s1 != s2 && retry <= max_retries);

    return (s1 == s2); 
}


static void *reader_thread_fn(void *arg)
{
    shm_reader_t *reader = (shm_reader_t *)arg;
    sdv_telemetry_t raw;
    uint32_t fc, ec, cec;
    uint32_t last_seq = 0;
    uint32_t last_fc  = 0;

    printf("[SHM Reader] Thread started\n");

    while (reader->running && reader->shm) {

        if (shm_read_robust(reader->shm, &raw, &fc, &ec, &cec)) {

           
            if (raw.seq != last_seq || fc != last_fc) {

                pthread_mutex_lock(&reader->mutex);
                frame_to_state(&raw, fc, ec, cec, &reader->current_state);
                reader->new_data_available = true;
                pthread_cond_signal(&reader->cond);
                pthread_mutex_unlock(&reader->mutex);

                last_seq = raw.seq;
                last_fc  = fc;
            }
        }
        usleep(20000); 
    }

    printf("[SHM Reader] Thread stopped\n");
    return NULL;
}



shm_reader_t *shm_reader_init(uint32_t key)
{
    shm_reader_t *reader = calloc(1, sizeof(shm_reader_t));
    if (!reader) {
        fprintf(stderr, "[SHM Reader] calloc failed\n");
        return NULL;
    }

    int shm_id = shmget(key, sizeof(telemetry_shm_t), 0666);
    if (shm_id < 0) {
        fprintf(stderr, "[SHM Reader] shmget failed (errno=%d)\n", errno);
        free(reader);
        return NULL;
    }

    reader->shm = (telemetry_shm_t *)shmat(shm_id, NULL, SHM_RDONLY);
    if (reader->shm == (void *)-1) {
        fprintf(stderr, "[SHM Reader] shmat failed (errno=%d)\n", errno);
        free(reader);
        return NULL;
    }

    pthread_mutex_init(&reader->mutex, NULL);
    pthread_cond_init(&reader->cond, NULL);
    reader->running = true;

    if (pthread_create(&reader->thread, NULL, reader_thread_fn, reader) != 0) {
        fprintf(stderr, "[SHM Reader] pthread_create failed\n");
        shmdt(reader->shm);
        pthread_mutex_destroy(&reader->mutex);
        pthread_cond_destroy(&reader->cond);
        free(reader);
        return NULL;
    }

    printf("[SHM Reader] Initialized (key=0x%08X)\n", key);
    return reader;
}

bool shm_reader_get_state(shm_reader_t *reader, telemetry_state_t *out)
{
    if (!reader || !out) return false;

    bool got = false;

    pthread_mutex_lock(&reader->mutex);
    if (reader->new_data_available) {
        *out = reader->current_state;
        reader->new_data_available = false;
        got = true;
    }
    pthread_mutex_unlock(&reader->mutex);

    return got;
}

void shm_reader_shutdown(shm_reader_t *reader)
{
    if (!reader) return;

    reader->running = false;
    pthread_cond_broadcast(&reader->cond);

    if (reader->thread) {
        pthread_join(reader->thread, NULL);
    }

    if (reader->shm && reader->shm != (void *)-1) {
        shmdt(reader->shm);
    }

    pthread_mutex_destroy(&reader->mutex);
    pthread_cond_destroy(&reader->cond);

    free(reader);
    printf("[SHM Reader] Shutdown complete\n");
}