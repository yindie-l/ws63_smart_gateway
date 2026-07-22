
#ifndef __DEFINE_H
#define __DEFINE_H

/* ---------------------------------- 系统底层头文件 ---------------------------------- */
#include "pwm.h"                           // PWM 硬件输出（用于蜂鸣器）
#include "tcxo.h"                          // 系统硬件时钟
#include "adc.h"                           // ADC 采样接口
#include "adc_porting.h"                   // ADC 通道映射适配
#include <math.h>                          // 数学计算库
#include "osal_debug.h"                    // OSAL 日志与调试打印
#include "soc_osal.h"                      // OSAL 内核抽象层（线程、互斥锁、延时）
#include "pinctrl.h"
#include "gpio.h"
#include "app_init.h"
/*引脚的宏定义*/
#define PIR_GPIO_MODE              0
#define CONFIG_PWM_PIN_MODE       1
#define CONFIG_I2C_MASTER_PIN_MODE 2

#define CONFIG_PWM_PIN            3
#define LED_RED_GPIO              7 
#define PIR_REL_GPIO_PIN          9
#define LED_YELLOW_GPIO           10    
#define LED_GREEN_GPIO            11  
#define BUTTON_GPIO               14    
#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16

#define I2C_MASTER_ADDR 0x0
#define I2C_SLAVE1_ADDR 0x38
#define I2C_SET_BANDRATE 400000

#define PWM_CHANNEL               3
#define PWM_GROUP_ID              3

/*任务的优先级和任务栈大小*/ 
#define MAIN_TASK_STACK_SIZE 0x2000
#define MAIN_TASK_PRIO       17

/*时间宏定义*/
#define FREQ_TIME 20000       // 舵机PWM周期 20ms (20000us)
#define PWM_DELAY_1000MS 1000


/* ADC 按键电压区间定义 (单位: mV) */
#define VOLT_S1_MIN                   400
#define VOLT_S1_MAX                   800
#define VOLT_S2_MIN                   800
#define VOLT_S2_MAX                   1200
#define VOLT_NONE_MIN                 2500
/* 防抖连续确认次数 */
#define DEBOUNCE_COUNT                3

/* ADC 通道分配 */
#define ADC_CH_BUTTON             1    // 按键使用的 ADC 通道 1
#define ADC_CH_RAIN_SENSOR        5    // 雨滴传感器使用的 ADC 通道 5
/* BH1750FV I2C 从机配置 */
#define BH1750_SLAVE_ADDR                 0x23    // ADDR接地时的标准地址
#define BH1750_CMD_POWER_ON               0x01    // 通电指令
#define BH1750_CMD_RESET                  0x07    // 重置数据寄存器
#define BH1750_CMD_CON_H_RES_MODE         0x10    // 连续高分辨率模式 (1 lx 分辨率)

/**
 * @brief 系统全局共享状态结构体
 * @note  所有并发线程需在获取互斥锁（g_data_mutex）后方可读写该结构体
 */
typedef struct {
    float    env_temp;               // 实时环境温度 (℃)
    float    env_humi;               // 实时环境湿度 (%)
    uint32_t env_lux;                // 实时光照度 (lx)
    int      rack_state;             // 晾衣架状态: 0=缩回(60°), 1=正外伸, 2=伸出(-45°), 3=正缩回, 4=红外报警态
    int      window_state;           // 窗户状态: 0=关闭(0°), 1=正在开启, 2=已开启(90°), -1=正在关闭
    int      rain_alert;             // 雨滴报警预警标志: 0=无雨, 1=检测到降雨
    int      buzzer_req;             // 蜂鸣器触发请求标志: 0=空闲, 1=请求蜂鸣器鸣响报警
    float    rack_interrupted_angle; // 记录晾衣架被打断时的目标安全角度
    uint8_t  button_print_flag;      // 按键事件打印通知标志（UI线程处理后清零）
    uint8_t  pir_print_flag;         // 红外触发事件打印通知标志（UI线程处理后清零）
    uint8_t  child_lock_enable;      // 子锁状态: 0=未启用, 1=已启用（默认未启用）
} SystemSharedData_t;

/* 按键枚举状态 */
typedef enum {
    KEY_NONE = 0,
    KEY_S1_PRESS,
    KEY_S2_PRESS
} Key_State_t;


#endif // __DEFINE_H