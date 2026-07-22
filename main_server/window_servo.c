#include "window_servo.h"

static volatile int g_current_window_pulse = WINDOW_PULSE_CLOSE;

void Window_Servo_Init(void)
{
    uapi_pin_set_mode(BSP_WINDOW_SERVO, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BSP_WINDOW_SERVO, CORES_APPS_CORE);
    uapi_gpio_set_dir(BSP_WINDOW_SERVO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_LOW);
}

void Window_SetAngle(unsigned int duty_us)
{
    if (duty_us >= WINDOW_PWM_PERIOD_US) {
        return;
    }
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_HIGH);
    uapi_systick_delay_us(duty_us);
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_LOW);
    uapi_systick_delay_us(WINDOW_PWM_PERIOD_US - duty_us);
}

void Window_TurnSlow(int start_pulse, int end_pulse, int step, int cycles)
{
    if (step <= 0 || cycles <= 0) {
        return;
    }

    if (start_pulse < end_pulse) {
        for (int pulse = start_pulse; pulse <= end_pulse; pulse += step) {
            for (int i = 0; i < cycles; i++) {
                Window_SetAngle((unsigned int)pulse);
            }
            g_current_window_pulse = pulse;
            uapi_watchdog_kick();
        }
    } else {
        for (int pulse = start_pulse; pulse >= end_pulse; pulse -= step) {
            for (int i = 0; i < cycles; i++) {
                Window_SetAngle((unsigned int)pulse);
            }
            g_current_window_pulse = pulse;
            uapi_watchdog_kick();
        }
    }
}

void Window_Open_90(void)
{
    if (g_current_window_pulse != WINDOW_PULSE_OPEN_90) {
        Window_TurnSlow(g_current_window_pulse, WINDOW_PULSE_OPEN_90,
                        WINDOW_STEP_US, WINDOW_STEP_CYCLES);
        g_current_window_pulse = WINDOW_PULSE_OPEN_90;
    }
}

void Window_Close_0(void)
{
    if (g_current_window_pulse != WINDOW_PULSE_CLOSE) {
        Window_TurnSlow(g_current_window_pulse, WINDOW_PULSE_CLOSE,
                        WINDOW_STEP_US, WINDOW_STEP_CYCLES);
        g_current_window_pulse = WINDOW_PULSE_CLOSE;
    }
}

void Window_Reset_Close(void)
{
    for (int i = 0; i < 25; i++) {
        Window_SetAngle(WINDOW_PULSE_CLOSE);
        uapi_watchdog_kick();
    }
    g_current_window_pulse = WINDOW_PULSE_CLOSE;
}
