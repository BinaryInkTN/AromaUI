#include "shm_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>

/* Internal: shared memory layout - not exposed in header */
typedef struct {
    volatile uint32_t write_seq;
    sdv_telemetry_t telemetry;
} shm_layout_t;

struct shm_reader {
    int                shm_fd;
    shm_layout_t      *shm;
    pthread_t          thread;
    volatile bool      running;

    pthread_mutex_t    mutex;
    pthread_cond_t     cond;
    sdv_telemetry_t    current_state;
    volatile bool      new_data_available;
};

static bool shm_read_robust(shm_layout_t *shm, sdv_telemetry_t *out_raw);
static void *reader_thread_fn(void *arg);

static bool shm_read_robust(shm_layout_t *shm, sdv_telemetry_t *out_raw)
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
    uint8_t last_seq = 0;

    printf("[SHM Reader] Thread started\n");

    while (reader->running && reader->shm) {
        if (shm_read_robust(reader->shm, &raw)) {
            if (raw.seq != last_seq) {
                pthread_mutex_lock(&reader->mutex);
                reader->current_state = raw;
                reader->new_data_available = true;
                pthread_cond_signal(&reader->cond);
                pthread_mutex_unlock(&reader->mutex);

                last_seq = raw.seq;
            }
        }
        usleep(20000);
    }

    printf("[SHM Reader] Thread stopped\n");
    return NULL;
}

shm_reader_t *shm_reader_init(const char *shm_name)
{
    shm_reader_t *reader = calloc(1, sizeof(shm_reader_t));
    if (!reader) {
        fprintf(stderr, "[SHM Reader] calloc failed\n");
        return NULL;
    }

    reader->shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    if (reader->shm_fd < 0) {
        fprintf(stderr, "[SHM Reader] shm_open failed (errno=%d)\n", errno);
        free(reader);
        return NULL;
    }

    reader->shm = (shm_layout_t *)mmap(NULL, sizeof(shm_layout_t), 
                                        PROT_READ, MAP_SHARED, 
                                        reader->shm_fd, 0);
    if (reader->shm == MAP_FAILED) {
        fprintf(stderr, "[SHM Reader] mmap failed (errno=%d)\n", errno);
        close(reader->shm_fd);
        free(reader);
        return NULL;
    }

    pthread_mutex_init(&reader->mutex, NULL);
    pthread_cond_init(&reader->cond, NULL);
    reader->running = true;

    if (pthread_create(&reader->thread, NULL, reader_thread_fn, reader) != 0) {
        fprintf(stderr, "[SHM Reader] pthread_create failed\n");
        munmap(reader->shm, sizeof(shm_layout_t));
        close(reader->shm_fd);
        pthread_mutex_destroy(&reader->mutex);
        pthread_cond_destroy(&reader->cond);
        free(reader);
        return NULL;
    }

    printf("[SHM Reader] Initialized (name=%s)\n", shm_name);
    return reader;
}

bool shm_reader_get_state(shm_reader_t *reader, sdv_telemetry_t *out)
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

    if (reader->shm && reader->shm != MAP_FAILED) {
        munmap(reader->shm, sizeof(shm_layout_t));
    }

    if (reader->shm_fd >= 0) {
        close(reader->shm_fd);
    }

    pthread_mutex_destroy(&reader->mutex);
    pthread_cond_destroy(&reader->cond);

    free(reader);
    printf("[SHM Reader] Shutdown complete\n");
}