#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>

#define CAN_INTERFACE "vcan0"

void start_can_thread(void);
void parse_can_frame(uint32_t can_id, uint8_t *data, uint8_t len);

#endif