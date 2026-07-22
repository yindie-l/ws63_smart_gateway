#include "beep.h"
/* === [修复 1：添加管脚复用与通用定义头文件] === */
#include "pinctrl.h"            // 解决 uapi_pin_set_mode 未定义的问题
#include "errcode.h"            // 包含 ERRCODE_SUCC 及通用底层宏 definition
#include "common_def.h"         // 解决 UNUSED 宏未定义的问题 (若包含后仍报错，见下方方案B)

/**
 * @brief PWM 中断回调函数（空实现，满足底层驱动注册要求）
 */
static errcode_t pwm_sample_callback(uint8_t channel)
{
    /* === [修复 2：如果引入 common_def.h 后仍提示 UNUSED 找不到，可以直接用原生语法强转，100%解决] === */
    (void)channel;              // 原生 C 语言写法：显式忽略未使用参数，替代 UNUSED(channel);
    return ERRCODE_SUCC;
}

/**
 * @brief 蜂鸣器硬件启动
 * @note  配置 PWM 输出固定周期与占空比，驱动有源/无源蜂鸣器发声
 */
void Buzzer_Hardware_Start(uint32_t freq_hz)
{
    if (freq_hz == 0) freq_hz = 2500; // 安全防错，默认设置为 2500Hz
    
    /* 1. 芯片 PWM 底层硬件时钟为 40MHz (40,000,000 Hz)
     *    总计数值(周期) = 40000000 / freq_hz
     * 2. 为了达到最响亮、最清晰的 50% 占空比，low_time 和 high_time 各分配一半周期
     */
    uint32_t total_ticks = 40000000 / freq_hz;
    uint32_t half_ticks  = total_ticks / 2;
    
    /* 严格对照结构体成员顺序：low_time, high_time, offset_time, cycles, repeat */
    pwm_config_t cfg_dynamic = { half_ticks, half_ticks, 0, 4800, true };
    
    /* 引入 #include "pinctrl.h" 后，此行编译警告/错误即消除 */
    uapi_pin_set_mode(CONFIG_PWM_PIN, CONFIG_PWM_PIN_MODE);
    uapi_pwm_init();
    uapi_pwm_open(PWM_CHANNEL, &cfg_dynamic);
    uapi_pwm_unregister_interrupt(PWM_GROUP_ID);
    uapi_pwm_register_interrupt(PWM_GROUP_ID, pwm_sample_callback);
    
    uint8_t channel_id = PWM_CHANNEL;
    uapi_pwm_set_group(PWM_GROUP_ID, &channel_id, 1);
    uapi_pwm_start(PWM_GROUP_ID);
}

/**
 * @brief 蜂鸣器硬件停止与资源释放
 */
void Buzzer_Hardware_Stop(void)
{
    uapi_pwm_close(PWM_CHANNEL);
    uapi_pwm_deinit();
}