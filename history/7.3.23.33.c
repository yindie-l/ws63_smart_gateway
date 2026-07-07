#include "main.h"

/*oled*/
#include "../oled_module/ssd1306_fonts.h"
#include "../oled_module/ssd1306.h"

/*servo*/
#include "../servo_module/servo_module.h"

/*pir*/
#include "../pir_module/pir_test.h"
#include "../pir_module/pir.h"

/*environment*/
#include "../environment_module/aht20.h"
#include "../environment_module/aht20_test.h"

/*buzzer*/
#include "pwm.h"    
#include "tcxo.h"

/*adc*/
#include "adc.h"
#include "adc_porting.h"

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

/* 选择ADC通道1 */
#define CONFIG_ADC_CHANNEL        1 

/*任务的优先级和任务栈大小*/ 
#define MAIN_TASK_STACK_SIZE 0x2000
#define MAIN_TASK_PRIO       17

/*时间宏定义*/
#define FREQ_TIME 20000       // 舵机PWM周期 20ms (20000us)
#define PWM_DELAY_1000MS 1000

/* I2C初始化函数*/
void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/* * 全局状态变量 
 * 0: 初始停止状态 (绿灯)
 * 1: 状态a - -45° 连续运转到 60° (绿灯)
 * 2: 状态b - 60° 连续退回到 -45° (黄灯)
 * 3: 红外触发 - 停止旋转且蜂鸣器报警 (红灯)
 * -1: 状态a转完后的静止待机状态 (保持绿灯)
 * -2: 状态b转完后的静止待机状态 (保持黄灯)
 */
volatile int g_servoRun = 0; 
volatile int g_buzzerTrigger = 0;   // 0-未触发，1-需要播放
volatile int g_servoActionDone = 0; // 动作完成标记

/* 记忆化断点全局度数变量 (用于红外触发后断点续传) */
volatile float g_current_angle = -45.0f; 



/* PWM 周期结束回调函数 */
static errcode_t pwm_sample_callback(uint8_t channel)
{
    UNUSED(channel);
    return ERRCODE_SUCC;
}

/* 蜂鸣器完整初始化 */
void BuzzerInit(void)
{
    pwm_config_t cfg_3seconds = {
        20000,  // 周期
        10000,  // 高电平持续时间
        0,      // 相位偏移
        4800,   // 发送 4800 个波形（约 3 秒）
        true    // 是否循环
    };

    uapi_pin_set_mode(CONFIG_PWM_PIN, CONFIG_PWM_PIN_MODE);
    uapi_pwm_init();
    uapi_pwm_open(PWM_CHANNEL, &cfg_3seconds);
    
    uapi_pwm_unregister_interrupt(PWM_GROUP_ID);
    uapi_pwm_register_interrupt(PWM_GROUP_ID, pwm_sample_callback);
    
    uint8_t channel_id = PWM_CHANNEL;
    uapi_pwm_set_group(PWM_GROUP_ID, &channel_id, 1);
}

/* 按键中断回调函数 - 实现状态切换机制 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    
    // 1. 如果是初始状态、红外报警状态、或者状态b(及其结束状态)，按下后进入状态1(a)
    if (g_servoRun == 0 || g_servoRun == 3 || g_servoRun == 2 || g_servoRun == -2) {
        g_servoRun = 1;
    } 
    // 2. 如果当前处于状态1(a)或者其转完后的静止状态，按下后进入状态2(b)
    else if (g_servoRun == 1 || g_servoRun == -1) {
        g_servoRun = 2;
    }
    
    g_servoActionDone = 0; // 清除动作完成标记，允许执行新的转圈动作
    printf("Button pressed. New State: %d, Resume Angle: %.1f\r\n", g_servoRun, g_current_angle);
}

/* 初始化按键中断函数 */
void KeyInit(void)
{
    uapi_pin_set_mode(BUTTON_GPIO, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BUTTON_GPIO, CORES_APPS_CORE);
    uapi_gpio_set_dir(BUTTON_GPIO, GPIO_DIRECTION_INPUT);
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BUTTON_GPIO);
    }
}

/* 红外传感器中断回调函数 */
static void pir_interrupt_handler(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    
    // 只有在舵机正在旋转(状态1/a 或 状态2/b)时，红外触发才有效
    if (g_servoRun == 1 || g_servoRun == 2) {
        g_servoRun = 3;        // 强制切到状态3：红外触发停止
        g_buzzerTrigger = 1;   // 触发蜂鸣器
        printf("PIR Triggered! Servo emergency stop at angle: %.1f\r\n", g_current_angle);
    }
}

/* 初始化红外传感器中断函数*/
void PirInterruptInit(void)
{
    uapi_pin_set_mode(PIR_REL_GPIO_PIN, (pin_mode_t)PIR_GPIO_MODE);
    gpio_select_core(PIR_REL_GPIO_PIN, CORES_APPS_CORE);
    uapi_gpio_set_dir(PIR_REL_GPIO_PIN, GPIO_DIRECTION_INPUT);
    errcode_t ret = uapi_gpio_register_isr_func(PIR_REL_GPIO_PIN, GPIO_INTERRUPT_RISING_EDGE, pir_interrupt_handler);
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(PIR_REL_GPIO_PIN);
    }
    printf("PIR Interrupt initialized.\r\n");
}

/* OLED 状态显示刷新函数 */
void UpdateOledDisplay(int state, float temp, float humi)
{
    char temp_str[32];
    char humi_str[32];
    char angle_str[32];

    ssd1306_Fill(0); 
    
    ssd1306_SetCursor(0, 0);
    if (state == 1 || state == -1) {
        ssd1306_DrawString("Servo: -45 -> 60", Font_7x10, 1); 
    } else if (state == 2 || state == -2) {
        ssd1306_DrawString("Servo: 60 -> -45", Font_7x10, 1);
    } else if (state == 3) {
        ssd1306_DrawString("Servo: EMERGENCY", Font_7x10, 1);
        ssd1306_SetCursor(0, 10);
        ssd1306_DrawString("PIR DETECTED!!", Font_7x10, 1);
    } else {
        ssd1306_DrawString("Servo: INITIAL", Font_7x10, 1);
    }

    int temp_int = (int)(temp * 100);
    int humi_int = (int)(humi * 100);
    snprintf(temp_str, sizeof(temp_str), "Temp: %d.%02d C", temp_int / 100, abs(temp_int % 100));
    snprintf(humi_str, sizeof(humi_str), "Humi: %d.%02d %%", humi_int / 100, abs(humi_int % 100));
    
    // OLED增加当前度数实时反馈
    int angle_int = (int)(g_current_angle * 10);
    snprintf(angle_str, sizeof(angle_str), "Angl: %s%d.%d deg", (angle_int < 0) ? "-" : "", abs(angle_int / 10), abs(angle_int % 10));

    ssd1306_SetCursor(0, 20);
    ssd1306_DrawString(angle_str, Font_7x10, 1);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString(temp_str, Font_7x10, 1);
    ssd1306_SetCursor(0, 44);
    ssd1306_DrawString(humi_str, Font_7x10, 1);
    
    ssd1306_UpdateScreen(); 
}

/* LED 所有初始化*/
void LedsInit(void)
{
    pin_t leds[] = {LED_RED_GPIO, LED_GREEN_GPIO, LED_YELLOW_GPIO};
    for (int i = 0; i < 3; i++) {
        uapi_pin_set_mode(leds[i], HAL_PIO_FUNC_GPIO);     
        gpio_select_core(leds[i], CORES_APPS_CORE);       
        uapi_gpio_set_dir(leds[i], GPIO_DIRECTION_OUTPUT); 
        SetLedState(leds[i], 0); 
    }
}

/* 主任务 */
void mainTask(void)
{
    uint32_t baudrate = I2C_SET_BANDRATE;
    uint32_t hscode = I2C_MASTER_ADDR;
    
    app_i2c_init_pin();
    errcode_t ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        printf("i2c init failed, ret = %0x\r\n", ret);
    }

    /* 初始化外设驱动 */
    ssd1306_Init();
    KeyInit();           
    Sg92R_Init();
    PIR_Init();          
    PirInterruptInit();  
    LedsInit();
    BuzzerInit(); 
    
    /* 初始化 ADC 模块 */
    uapi_adc_init(ADC_CLOCK_NONE);

    int calib_retry = 0;
    while (AHT20_Calibrate() != 0) {
        uapi_systick_delay_ms(100);
        if (calib_retry++ > 5) break;
    }

    int last_state = -99; 
    float current_temp = 0.0f;
    float current_humi = 0.0f;
    uint32_t tick_count = 0;
    uint16_t adc_voltage = 0;

    while (1)
    {
        uapi_watchdog_kick();

        /* 蜂鸣器响应逻辑 */
        if (g_buzzerTrigger == 1) {
            BuzzerInit();
            printf("[BUZZER] Human detected! Hardware starts 3s pulse wave...\r\n");
            uapi_pwm_start(PWM_GROUP_ID);
            uapi_tcxo_delay_ms((uint32_t)PWM_DELAY_1000MS);
            uapi_pwm_close(PWM_CHANNEL); 
            uapi_pwm_deinit();
            g_buzzerTrigger = 0;
        }

        /* 定时读取温湿度 (约每 100ms * 5 = 500ms 刷新一次) */
        if (tick_count % 5 == 0) 
        {
            ret = AHT20_StartMeasure();
            if (ret == 0) {
                uapi_systick_delay_ms(80); 
                AHT20_GetMeasureResult(&current_temp, &current_humi);
            }
            UpdateOledDisplay(g_servoRun, current_temp, current_humi);
        }

        /* ADC 电压检测逻辑 */
        if (tick_count % 30 == 0)
        {
            if (adc_port_read(CONFIG_ADC_CHANNEL, &adc_voltage) == ERRCODE_SUCC) {
                printf("[ADC] Current Voltage: %d mV, State: %d\r\n", adc_voltage, g_servoRun);
                
                if (adc_voltage < 1300 && g_servoRun == -2) {
                    printf("[ADC TRIGGER] Voltage < 1300mV. Switching to State A!\r\n");
                    g_servoRun = 1;        
                    g_servoActionDone = 0; 
                }
            }
        }

        /* LED 灯光根据状态切换 */
        if (g_servoRun != last_state) {
            last_state = g_servoRun;
            UpdateOledDisplay(g_servoRun, current_temp, current_humi);
            
            if (g_servoRun == 0 || g_servoRun == 1 || g_servoRun == -1) { 
                SetLedState(LED_GREEN_GPIO, 1);  SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 0);    
            } 
            else if (g_servoRun == 2 || g_servoRun == -2) { 
                SetLedState(LED_GREEN_GPIO, 0);  SetLedState(LED_YELLOW_GPIO, 1); SetLedState(LED_RED_GPIO, 0);    
            } 
            else if (g_servoRun == 3) { 
                SetLedState(LED_GREEN_GPIO, 0);  SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 1);    
            }
        }

        /* ===================================================================
         * 舵机动作控制逻辑（重构部分）
         * =================================================================== */
         
        // （1）按下按键绿灯亮：执行状态1（从当前断点 angle 连续正向扫掠至 60°）
        if (g_servoRun == 1 && !g_servoActionDone)
        {
            // 如果上一次残留的物理位置超出了正向边界，强制重设为起点
            if (g_current_angle < -45.0f || g_current_angle >= 60.0f) {
                g_current_angle = -45.0f;
            }

            printf("[SERVO] Resuming Positive Sweep from %.1f to 60.0\r\n", g_current_angle);

            // 以 0.5 度精准步进运行
            while (g_current_angle <= 60.0f) {
                // 允许中途被红外传感器触发（切为状态3）强制跳出
                if (g_servoRun != 1) {
                    break; 
                }

                unsigned int duty_us = (unsigned int)sg92r_map((long)g_current_angle, -90, 90, 500, 2500);
                Sg92R_SetPulseWidth(duty_us);

                g_current_angle += 0.5f; // 步进更新
            }
            
            // 如果成功走完了全程（未被红外截断）
            if (g_servoRun == 1) {
                g_servoActionDone = 1; 
                g_servoRun = -1;  // 转完后进入状态a对应的挂起态，灯光维持绿色
                printf("[SERVO] Positive Sweep complete.\r\n");
            }
        }
        // （2）按下按键黄灯亮：执行状态2（从当前断点 angle 连续反向退回至 -45°）
        else if (g_servoRun == 2 && !g_servoActionDone)
        {
            // 如果上一次残留的物理位置超出了反向边界，强制重设为终点
            if (g_current_angle > 60.0f || g_current_angle <= -45.0f) {
                g_current_angle = 60.0f;
            }

            printf("[SERVO] Resuming Reverse Sweep from %.1f to -45.0\r\n", g_current_angle);

            // 以 0.5 度精准步进运行
            while (g_current_angle >= -45.0f) {
                // 允许中途被红外传感器触发强制跳出
                if (g_servoRun != 2) {
                    break; 
                }

                unsigned int duty_us = (unsigned int)sg92r_map((long)g_current_angle, -90, 90, 500, 2500);
                Sg92R_SetPulseWidth(duty_us);

                g_current_angle -= 0.5f; // 步进反向更新
            }
            
            if (g_servoRun == 2) {
                g_servoActionDone = 1; 
                g_servoRun = -2;  // 转完后进入状态b对应的挂起态，灯光维持黄色
                printf("[SERVO] Reverse Sweep complete.\r\n");
            }
        }
        else
        {
            // 基础空载调度延时
            uapi_systick_delay_ms(100);
        }
        
        tick_count++;
    }
}

void main_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    osal_kthread_lock();
    
    taskid = osal_kthread_create((osal_kthread_handler)mainTask, NULL, "mainTask", MAIN_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, MAIN_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task mainTask failed.\n");
    }

    if (taskid != NULL) {
        osal_kfree(taskid);
    }

    osal_kthread_unlock();
}

app_run(main_entry);