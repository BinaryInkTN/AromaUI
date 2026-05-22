#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#define CAN_INTERFACE "vcan0"
#define CAN_FRAME_DATA_LEN 8

bool start_can_thread(void);
void stop_can_thread(void);
void parse_can_frame(uint32_t can_id, const uint8_t *data, uint8_t len);

#endif // CAN_HANDLER_H