#ifndef SERVO_MODULE_H
#define SERVO_MODULE_H

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "systick.h"
#include "watchdog.h"
#include "define.h"

#define BSP_SG92R 2

void S92RInit(void);
void SetAngle(unsigned int duty);
unsigned int AngleToPulse(int angle);
void EngineMoveSmooth(int start_angle, int end_angle);

extern volatile int g_emergency_stop_flag;
extern volatile int g_current_servo_angle;

#endif /* SERVO_MODULE_H */
