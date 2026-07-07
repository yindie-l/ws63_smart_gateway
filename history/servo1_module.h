#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "systick.h"
#include "watchdog.h"
#include "app_init.h"

#define COUNT 10 // 通过计算舵机转到对应角度需要发送10个左右的波形//
#define BSP_SG92R GPIO_02
#define FREQ_TIME 20000

extern volatile int g_servoRun;
void S92RInit(void)
{
    uapi_pin_set_mode(BSP_SG92R, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_SG92R, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
}
void SetAngle(unsigned int duty)
{
    unsigned int time = FREQ_TIME;

    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_HIGH);
    uapi_systick_delay_us(duty);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
    uapi_systick_delay_us(time - duty);
}

/* The steering gear is centered
 * 1、依据角度与脉冲的关系，设置高电平时间为1500微秒
 * 2、不断地发送信号，控制舵机居中
 */
void RegressMiddle(void)
{
    unsigned int angle = 1500;
    for (int i = 0; i < COUNT; i++) {
        SetAngle(angle);
    }
}

/* Turn 90 degrees to the right of the steering gear
 * 1、依据角度与脉冲的关系，设置高电平时间为500微秒
 * 2、不断地发送信号，控制舵机向右旋转90度
 */
/*  Steering gear turn right */
void EngineTurnRight(void)
{
    unsigned int angle = 500;
    for (int i = 0; i < COUNT; i++) {
        SetAngle(angle);
    }
}

/* Turn 90 degrees to the left of the steering gear
 * 1、依据角度与脉冲的关系，设置高电平时间为2500微秒
 * 2、不断地发送信号，控制舵机向左旋转90度
 */
/* Steering gear turn left */
void EngineTurnLeft(void)
{
    unsigned int angle = 2500;
    for (int i = 0; i < COUNT; i++) {
        SetAngle(angle);
    }
}
