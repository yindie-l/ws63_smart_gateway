#include "servo.h"

/* 发送单次 PWM 波形，耗时约 20ms。 */
void SetAngle(unsigned int duty)
{
    unsigned int time = FREQ_TIME;
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_HIGH);
    uapi_systick_delay_us(duty);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
    uapi_systick_delay_us(time - duty);
}

unsigned int AngleToPulse(int angle)
{
    return 1500 + (angle * 2000) / 180;
}

void EngineMoveSmooth(int start_angle, int end_angle)
{
    int step = (start_angle < end_angle) ? 1 : -1;
    for (int angle = start_angle; angle != end_angle + step; angle += step) {
        if (g_emergency_stop_flag != 0) {
            break;
        }
        SetAngle(AngleToPulse(angle));
        g_current_servo_angle = angle;
        uapi_watchdog_kick();
    }
}

void S92RInit(void)
{
    uapi_pin_set_mode(BSP_SG92R, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BSP_SG92R, CORES_APPS_CORE);
    uapi_gpio_set_dir(BSP_SG92R, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
}
