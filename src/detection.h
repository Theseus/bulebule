#ifndef __DETECTION_H
#define __DETECTION_H

#include <stdio.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>

/* Sensors */
#define SENSOR_SIDE_LEFT 0
#define SENSOR_SIDE_RIGHT 1
#define SENSOR_FRONT_LEFT 2
#define SENSOR_FRONT_RIGHT 3

void get_gyro_raw(uint16_t *vo, uint16_t *vref);

#endif /* __DETECTION_H */
