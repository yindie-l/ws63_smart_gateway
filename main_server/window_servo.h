#ifndef WINDOW_SERVO_H
#define WINDOW_SERVO_H

#include "pinctrl.h"
#include "common_def.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "systick.h"
#include "watchdog.h"
#include "window_server_config.h"

void Window_Servo_Init(void);
void Window_SetAngle(unsigned int duty_us);
void Window_TurnSlow(int start_pulse, int end_pulse, int step, int cycles);
void Window_Open_90(void);
void Window_Close_0(void);
void Window_Reset_Close(void);

#endif /* WINDOW_SERVO_H */
