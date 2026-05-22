#include "can_handler.h"
#include "app_state.h"
#include "aroma.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#ifndef __EMSCRIPTEN__
#include <linux/can.h>
#include <linux/can/raw.h>

void parse_can_frame(uint32_t can_id, uint8_t *data, uint8_t len)
{
    (void)len;
    pthread_mutex_lock(&state.can_mtx);
    
    switch(can_id) {
        case 0x100: {
            uint16_t soc = (data[0] << 8) | data[1];
            int16_t cur = (data[2] << 8) | data[3];
            uint16_t vol = (data[4] << 8) | data[5];
            state.vehicle_state.soc = soc / 100.0;
            state.vehicle_state.current = cur / 10.0;
            state.vehicle_state.voltage = vol / 10.0;
            break;
        }
        case 0x101: {
            uint16_t spd = (data[0] << 8) | data[1];
            int32_t rpm = (data[2] << 24) | (data[3] << 16) | 
                         (data[4] << 8) | data[5];
            state.vehicle_state.speed = spd / 10.0;
            state.vehicle_state.rpm = rpm;
            state.vehicle_state.gear = data[6];
            break;
        }
        case 0x102: {
            int16_t temp = (data[0] << 8) | data[1];
            state.vehicle_state.cabin_temp = temp / 10.0;
            state.vehicle_state.hvac_on = data[2];
            state.vehicle_state.doors = data[3];
            state.vehicle_state.fan_speed = data[4];
            state.vehicle_state.target_temp = data[5];
            state.vehicle_state.seat_heaters = data[6];
            break;
        }
        case 0x103: {
            uint16_t rng = (data[0] << 8) | data[1];
            state.vehicle_state.range = rng;
            break;
        }
        case 0x104: {
            state.vehicle_state.fault_code = (data[0] << 24) | (data[1] << 16) | 
                                            (data[2] << 8) | data[3];
            break;
        }
        case 0x01: {
            pthread_mutex_unlock(&state.can_mtx);
            pthread_mutex_lock(&state.pending_mtx);
            state.pending_map_open = 1;
            pthread_mutex_unlock(&state.pending_mtx);
            return;
        }
    }
    
    pthread_mutex_unlock(&state.can_mtx);
}

static void *can_thread_func(void *arg)
{
    (void)arg;
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) return NULL;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, CAN_INTERFACE, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        close(s);
        return NULL;
    }

    struct sockaddr_can addr = {0};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s);
        return NULL;
    }

    struct can_frame frame;
    while (aroma_ui_is_running()) {
        if (read(s, &frame, sizeof(struct can_frame)) > 0) {
            parse_can_frame(frame.can_id, frame.data, frame.can_dlc);
        }
    }
    
    close(s);
    return NULL;
}

void start_can_thread(void)
{
    pthread_t can_t;
    pthread_create(&can_t, NULL, can_thread_func, NULL);
}

#else
void start_can_thread(void) {}
void parse_can_frame(uint32_t can_id, uint8_t *data, uint8_t len) 
{
    (void)can_id;
    (void)data;
    (void)len;
}
#endif