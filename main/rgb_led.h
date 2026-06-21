#pragma once

#include <stdint.h>
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void rgb_led_init();
void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue);
void rgb_led_on(bool onState);
void rgb_led_flash(bool flashState, uint32_t period_ms);
void rgb_led_pulse(bool pulseState);
void rgb_led_single_pulse();
void rgb_led_pulse_every(bool pulseState);
void rgb_led_toggle();
void rgb_led_show_status();

#ifdef __cplusplus
}
#endif
