#ifndef WINDOW_SERVER_CONFIG_H
#define WINDOW_SERVER_CONFIG_H

/* 窗户舵机信号线 GPIO。当前沿用原工程 GPIO0；接线变化时只改这里。 */
#define BSP_WINDOW_SERVO       0

#define WINDOW_PWM_PERIOD_US   20000
#define WINDOW_PULSE_CLOSE     500
#define WINDOW_PULSE_OPEN_90   1500
#define WINDOW_STEP_US         20
#define WINDOW_STEP_CYCLES     1

#endif /* WINDOW_SERVER_CONFIG_H */
