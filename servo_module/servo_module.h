#ifndef SG92R_CONTROL_H
#define SG92R_CONTROL_H

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "systick.h"

/* 硬件与时序参数配置 */
#define FREQ_TIME 20000       // PWM周期 20ms (20000us)
#define BSP_SG92R 2           // 舵机控制引脚 GPIO2

/**
 * @brief 内部数学映射函数（静态内联）
 */
static inline long sg92r_map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief 初始化舵机 GPIO 配置
 */
static inline void Sg92R_Init(void)
{
    uapi_pin_set_mode(BSP_SG92R, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_SG92R, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
}

/**
 * @brief 底层单次波形输出（带有临界区锁保护）
 */
static inline void Sg92R_SetPulseWidth(unsigned int duty_us)
{
    unsigned int time = FREQ_TIME;
    unsigned int int_status;

    // 进入临界区，锁定中断
    int_status = osal_irq_lock(); 

    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_HIGH);
    uapi_systick_delay_us(duty_us);
    uapi_gpio_set_val(BSP_SG92R, GPIO_LEVEL_LOW);
    uapi_systick_delay_us(time - duty_us);

    // 退出临界区，恢复中断
    osal_irq_restore(int_status); 
}

/**
 * @brief 带平滑速度控制的一次性角度输出函数
 * @param start_angle 动作开始时的当前角度
 * @param target_angle 期望转到的目标角度
 * @param speed_deg_per_20ms 每一个PWM周期(20ms)允许转动的最大角度。
 * 【关键控制】: 比如设为 1.0f，表示每秒转 50度；
 * 设为 0.5f，表示每秒转 25度。数值越小越慢。
 * @param g_servoRun_ptr 全局运行状态指针（用于在函数内部感知红外打断，及时退出）
 */
static inline void Sg92R_SetAngleSmooth(float start_angle, float target_angle, float speed_deg_per_20ms, volatile int *g_servoRun_ptr)
{
    // 限幅保护
    if (target_angle < -90.0f) target_angle = -90.0f;
    if (target_angle > 90.0f)  target_angle = 90.0f;
    if (start_angle < -90.0f)  start_angle = -90.0f;
    if (start_angle > 90.0f)   start_angle = 90.0f;

    float current_step_angle = start_angle;

    if (start_angle <= target_angle) {
        // 正向平滑旋转
        while (current_step_angle <= target_angle) {
            // 如果外部触发了红外中断(状态变为了3)，立刻退出波形输出，紧急停止
            if (*g_servoRun_ptr == 3) {
                break;
            }

            unsigned int duty_us = (unsigned int)sg92r_map((long)current_step_angle, -90, 90, 500, 2500);
            Sg92R_SetPulseWidth(duty_us); // 这一步消耗 20ms

            current_step_angle += speed_deg_per_20ms;
        }
    } else {
        // 反向平滑旋转
        while (current_step_angle >= target_angle) {
            if (*g_servoRun_ptr == 3) {
                break;
            }

            unsigned int duty_us = (unsigned int)sg92r_map((long)current_step_angle, -90, 90, 500, 2500);
            Sg92R_SetPulseWidth(duty_us); // 这一步消耗 20ms

            current_step_angle -= speed_deg_per_20ms;
        }
    }

    // 走到终点后，额外追加 5 次目标脉宽（100ms），确保机械完全对齐、锁死目标位置
    if (*g_servoRun_ptr != 3) {
        unsigned int final_duty_us = (unsigned int)sg92r_map((long)target_angle, -90, 90, 500, 2500);
        for (int i = 0; i < 5; i++) {
            Sg92R_SetPulseWidth(final_duty_us);
        }
    }
}

#endif /* SG92R_CONTROL_H */