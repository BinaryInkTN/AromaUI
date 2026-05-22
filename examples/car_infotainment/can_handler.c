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
#include <errno.h>

#ifndef __EMSCRIPTEN__
#include <linux/can.h>
#include <linux/can/raw.h>

static pthread_t can_thread_id;
static volatile bool can_thread_running = false;
static int can_socket = -1;

void parse_can_frame(uint32_t can_id, const uint8_t *data, uint8_t len)
{
    // Validate input
    if (!data || len < CAN_FRAME_DATA_LEN) {
        return;
    }
    
    // Validate CAN ID range
    if (can_id > 0x7FF) {
        return;
    }
    
    if (pthread_mutex_lock(&state.can_mtx) != 0) {
        return;
    }
    
    switch(can_id) {
        case 0x100: // Battery status
        {
            uint16_t soc = ((uint16_t)data[0] << 8) | data[1];
            int16_t cur = ((int16_t)data[2] << 8) | data[3];
            uint16_t vol = ((uint16_t)data[4] << 8) | data[5];
            
            state.vehicle_state.soc = soc / 100.0;
            state.vehicle_state.current = cur / 10.0;
            state.vehicle_state.voltage = vol / 10.0;
            break;
        }
        
        case 0x101: // Speed/RPM/Gear
        {
            uint16_t spd = ((uint16_t)data[0] << 8) | data[1];
            int32_t rpm = ((int32_t)data[2] << 24) | 
                         ((int32_t)data[3] << 16) | 
                         ((int32_t)data[4] << 8) | 
                         data[5];
            
            state.vehicle_state.speed = spd / 10.0;
            state.vehicle_state.rpm = rpm;
            state.vehicle_state.gear = data[6] & 0x03; // Mask to valid range
            break;
        }
        
        case 0x102: // Climate
        {
            int16_t temp = ((int16_t)data[0] << 8) | data[1];
            
            state.vehicle_state.cabin_temp = temp / 10.0;
            state.vehicle_state.hvac_on = data[2] ? 1 : 0;
            state.vehicle_state.doors = data[3];
            state.vehicle_state.fan_speed = data[4] & 0x0F; // Mask fan speed
            state.vehicle_state.target_temp = data[5];
            state.vehicle_state.seat_heaters = data[6] & 0x0F;
            break;
        }
        
        case 0x103: // Range
        {
            uint16_t rng = ((uint16_t)data[0] << 8) | data[1];
            state.vehicle_state.range = rng;
            break;
        }
        
        case 0x104: // Fault codes
        {
            state.vehicle_state.fault_code = 
                ((uint32_t)data[0] << 24) | 
                ((uint32_t)data[1] << 16) | 
                ((uint32_t)data[2] << 8) | 
                data[3];
            break;
        }
        
        case 0x01: // Map open trigger
        {
            pthread_mutex_unlock(&state.can_mtx);
            if (pthread_mutex_lock(&state.pending_mtx) == 0) {
                state.pending_map_open = 1;
                pthread_mutex_unlock(&state.pending_mtx);
            }
            return;
        }
        
        default:
            break;
    }
    
    pthread_mutex_unlock(&state.can_mtx);
}

static void *can_thread_func(void *arg)
{
    (void)arg;
    
    // Create CAN socket
    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_socket < 0) {
        fprintf(stderr, "CAN: Failed to create socket: %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(can_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(can_socket, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    safe_str_copy(ifr.ifr_name, CAN_INTERFACE, IFNAMSIZ);
    
    if (ioctl(can_socket, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "CAN: Interface '%s' not found: %s\n", 
                CAN_INTERFACE, strerror(errno));
        close(can_socket);
        can_socket = -1;
        return NULL;
    }
    
    // Bind socket
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "CAN: Bind failed: %s\n", strerror(errno));
        close(can_socket);
        can_socket = -1;
        return NULL;
    }
    
    // Receive loop
    struct can_frame frame;
    
    while (can_thread_running && aroma_ui_is_running()) {
        ssize_t nbytes = read(can_socket, &frame, sizeof(struct can_frame));
        
        if (nbytes > 0 && nbytes >= (ssize_t)sizeof(struct can_frame)) {
            if (frame.can_dlc <= CAN_FRAME_DATA_LEN) {
                parse_can_frame(frame.can_id, frame.data, frame.can_dlc);
            }
        } else if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        } else {
            usleep(1000); // 1ms sleep to prevent busy waiting
        }
    }
    
    close(can_socket);
    can_socket = -1;
    return NULL;
}

bool start_can_thread(void)
{
    if (can_thread_running) {
        return false;
    }
    
    can_thread_running = true;
    
    if (pthread_create(&can_thread_id, NULL, can_thread_func, NULL) != 0) {
        can_thread_running = false;
        return false;
    }
    
    return true;
}

void stop_can_thread(void)
{
    can_thread_running = false;
    
    if (can_thread_id) {
        pthread_join(can_thread_id, NULL);
        can_thread_id = 0;
    }
}

#else // __EMSCRIPTEN__

bool start_can_thread(void) { return false; }
void stop_can_thread(void) {}
void parse_can_frame(uint32_t can_id, const uint8_t *data, uint8_t len) 
{
    (void)can_id;
    (void)data;
    (void)len;
}

#endif