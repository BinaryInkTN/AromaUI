//
// Created by overflow on 5/30/2025.
//

#include "glps_mqtt_client.h"
#include "utils/logger/pico_logger.h"

/*
typedef struct {
    const char* addr;
    uint16_t port;
    bool is_connected;
} glps_mqtt_connection;
*/

glps_mqtt_connection* glps_mqtt_connect(const char* addr, uint16_t port) {
    if (!addr) {
        LOG_ERROR("Address is NULL. Couldn't connect to mqtt broker.");
        return NULL;
    }

    glps_mqtt_connection* conn = (glps_mqtt_connection*)malloc(sizeof(glps_mqtt_connection));

    conn = {
    .addr = addr,
    .port = port,
    };

    // Connect via sockets
    connect(
      [in] SOCKET         s,
      [in] const sockaddr *name,
      [in] int            namelen
    );

    return conn;

}
void glps_mqtt_disconnect(glps_mqtt_connection* connection_ptr) {

}
