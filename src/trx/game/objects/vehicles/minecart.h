#pragma once

#include <stdint.h>

typedef struct {
    int32_t speed;
    int32_t mid_pos;
    int32_t front_pos;
    int32_t turn_x;
    int32_t turn_z;
    int16_t turn_length;
    int16_t turn_rot;
    int16_t y_velocity;
    int16_t gradient;
    uint8_t flags;
    uint8_t stop_delay;
} MINECART_INFO;

bool Minecart_Control(void);
