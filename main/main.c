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

/*mqtt*/
#include "../mqtt_module/mqtt_module.h"

/* I2C初始化函数*/
void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/* ==================== 全局状态变量 ==================== */
volatile int g_servoRun = 0;
volatile int g_buzzerTrigger = 0;   // 0-未触发，1-需要播放
volatile int g_servoActionDone = 0; // 动作完成标记
volatile uint32_t g_last_button_time = 0;

/* 晾衣架角度量程 */
volatile float g_current_angle = -45.0f;
volatile float g_interrupted_angle = -45.0f;
char g_hanger_status[20] = "retracted";   // 衣架状态字符串

/* 窗户状态控制变量 */
volatile int g_windowState = 0;             // 0:已关闭/静止, 1:正在开窗, -1:正在关窗, 2:已开窗
volatile uint32_t g_window_remains = 0;     // 窗户舵机剩余脉冲周期数
unsigned int g_window_pulse_duty = WINDOW_SPEED_STOP;

/* 紧急避雨联动状态机 */
volatile int g_rain_emergency_active = 0;   // 0:正常, 1:正在紧急处理避雨

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

/* 按键中断回调函数 - 实现红外触发后的原单片按键响应 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    
    uint32_t current_time = (uint32_t)uapi_systick_get_ms();
    if (current_time - g_last_button_time < 300) {
        return;
    }
    g_last_button_time = current_time;

    if (g_servoRun == 3) {
        if (g_interrupted_angle == 60.0f) {
            g_servoRun = 1;
        } else {
            g_servoRun = 2;
        }
    } 
    else if (g_servoRun == 0 || g_servoRun == -1 || g_servoRun == -2) {
        if (g_current_angle == -45.0f) {
            g_servoRun = 1;
        } else {
            g_servoRun = 2;
        }
    }
    
    g_servoActionDone = 0;
    printf("Button pressed. New State: %d\r\n", g_servoRun);
}

/* 初始化板载按键中断函数 */
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
    
    if (g_servoRun == 1 || g_servoRun == 2) {
        if (g_servoRun == 1) {
            g_interrupted_angle = -45.0f;
        } else {
            g_interrupted_angle = 60.0f;
        }
        
        g_servoRun = 3;
        g_buzzerTrigger = 1;
        printf("PIR Triggered! Safe return target: %.1f\r\n", g_interrupted_angle);
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
}

/* OLED 状态显示刷新函数 */
void UpdateOledDisplay(int state, float temp, float humi, uint32_t tick)
{
    char data_str[32];
    ssd1306_Fill(0);

    if (state == 3) {
        uint32_t ms = (uint32_t)uapi_systick_get_ms();
        if ((ms / 500) % 2 == 0) {
            Oled_DrawChineseString(16, 20, hz_line10, 7);
        } else {
            ssd1306_Fill(0);
        }
        ssd1306_UpdateScreen();
        return;
    }

    if (state == 1 || state == 2) {
        Oled_DrawChineseString(16, 0, hz_line8, 6);
        Oled_DrawChineseString(16, 24, hz_line9, 3);

        int step = (int)(tick % 6);
        int dot_count = (step <= 3) ? step : (6 - step);

        int idx = 0;
        for (idx = 0; idx < dot_count; idx++) {
            data_str[idx] = '.';
        }
        data_str[idx] = '\0';

        ssd1306_SetCursor(66, 29);
        ssd1306_DrawString(data_str, Font_7x10, 1);
        
        ssd1306_UpdateScreen();
        return;
    }

    if (state == -1 || state == 0) {
        Oled_DrawChineseString(16, 0, hz_line4, 6);
    } 
    else if (state == -2) {
        Oled_DrawChineseString(16, 0, hz_line3, 6);
    }

    Oled_DrawChineseString(0, 16, hz_line5, 2);
    int temp_int = (int)(temp * 100);
    snprintf(data_str, sizeof(data_str), ":%d.%02d C", temp_int / 100, abs(temp_int % 100));
    ssd1306_SetCursor(34, 19);
    ssd1306_DrawString(data_str, Font_7x10, 1);

    Oled_DrawChineseString(0, 32, hz_line6, 2);
    int humi_int = (int)(humi * 100);
    snprintf(data_str, sizeof(data_str), ":%d.%02d %%", humi_int / 100, abs(humi_int % 100));
    ssd1306_SetCursor(34, 35);
    ssd1306_DrawString(data_str, Font_7x10, 1);

    Oled_DrawChineseString(0, 48, hz_line7, 2);
    ssd1306_SetCursor(34, 51);
    ssd1306_DrawString(":500 lux", Font_7x10, 1);
    
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

/* 窗户硬件引脚初始化 */
static void SmartWindow_Hardware_Init(void)
{
    uapi_pin_set_mode(BSP_WINDOW_SERVO, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BSP_WINDOW_SERVO, CORES_APPS_CORE);
    uapi_gpio_set_dir(BSP_WINDOW_SERVO, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_LOW);
}

/* 窗户驱动单次脉冲发送 */
static void Window_Servo_WritePulse(unsigned int duty)
{
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_HIGH);
    uapi_systick_delay_us(duty);
    uapi_gpio_set_val(BSP_WINDOW_SERVO, GPIO_LEVEL_LOW);
}

/* 配置窗户旋转参数触发器 */
static void Window_Servo_TriggerRotation(int direction)
{
    if (direction == 1) { // 开窗
        g_window_pulse_duty = WINDOW_SPEED_OPEN_SLOW;
        g_window_remains = CYCLES_FOR_90_DEGREE;
        g_windowState = 1; // 标记正在开窗
    } else if (direction == -1) { // 关窗
        g_window_pulse_duty = WINDOW_SPEED_CLOSE_SLOW;
        g_window_remains = CYCLES_FOR_90_DEGREE;
        g_windowState = -1; // 标记正在关窗
    } else {
        g_window_pulse_duty = WINDOW_SPEED_STOP;
        g_window_remains = 0;
    }
}

/* 主任务 */
void mainTask(void)
{
    printf("=== mainTask STARTED ===\n");  // 新增
    uint32_t baudrate = I2C_SET_BANDRATE;
    uint32_t hscode = I2C_MASTER_ADDR;
    
    app_i2c_init_pin();
    errcode_t ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        printf("i2c init failed, ret = %0x\r\n", ret);
    }
    
    /* 初始化外设驱动 */
    ssd1306_Init();

    /* 开机首屏 */
    ssd1306_Fill(0);
    Oled_DrawChineseString(32, 12, hz_line1, 4);
    Oled_DrawChineseString(32, 36, hz_line2, 4);
    ssd1306_UpdateScreen();
    uapi_systick_delay_ms(2000);
    
    KeyInit();
    Sg92R_Init();
    PIR_Init();
    PirInterruptInit();
    LedsInit();
    BuzzerInit();
    SmartWindow_Hardware_Init(); 

    uapi_adc_init(ADC_CLOCK_NONE);

    int calib_retry = 0;
    while (AHT20_Calibrate() != 0) {
        uapi_systick_delay_ms(100);
        if (calib_retry++ > 5) break;
    }

    mqtt_module_init();
    
    int last_state = -99;
    float current_temp = 0.0f;
    float current_humi = 0.0f;
    uint32_t tick_count = 0;
    
    /* 两个独立的 ADC 通道电压变量 */
    uint16_t button_voltage = 0;
    uint16_t rain_voltage = 0;

    /* ADC 按键消抖计数器 */
    uint8_t s1_counter = 0;
    uint8_t s2_counter = 0;
    uint8_t none_counter = 0;
    Key_State_t last_stable_key = KEY_NONE;

    while (1)
    {
        uapi_watchdog_kick();

        /* 蜂鸣器响应逻辑 */
        if (g_buzzerTrigger == 1) {
            BuzzerInit();
            uapi_pwm_start(PWM_GROUP_ID);
            for (int f = 0; f < 10; f++) {
                UpdateOledDisplay(g_servoRun, current_temp, current_humi, tick_count);
                uapi_systick_delay_ms(100);
            }
            uapi_pwm_close(PWM_CHANNEL);
            uapi_pwm_deinit();
            g_buzzerTrigger = 0;
        }

        /* 定时读取温湿度并更新屏幕显示 */
        if (tick_count % 5 == 0) {
            ret = AHT20_StartMeasure();
            if (ret == 0) {
                uapi_systick_delay_ms(80);
                AHT20_GetMeasureResult(&current_temp, &current_humi);
            }
        }
        UpdateOledDisplay(g_servoRun, current_temp, current_humi, tick_count);

        /* ==================== ADC 多通道分时采样与逻辑处理 ==================== */
        
        // 1. 读取雨滴传感器 (通道 5)
        if (adc_port_read(ADC_CH_RAIN_SENSOR, &rain_voltage) == ERRCODE_SUCC) 
        {
            if (rain_voltage < 1300) // 检测到降雨
            {
                if (!g_rain_emergency_active) {
                    g_rain_emergency_active = 1; 
                    printf("Rain detected on CH5! Starting Emergency Sequence.\r\n");
                }

                /* 避雨逻辑控制树 */
                if (g_rain_emergency_active == 1) 
                {
                    // 检查晾衣架是否处于伸出状态 (对应需求 1 和 3)
                    if (g_servoRun == -2 || g_servoRun == 2) {
                        g_servoRun = 1;         // 强制触发收衣服
                        g_servoActionDone = 0;
                    }
                    
                    // 等待晾衣架成功缩回完毕
                    if (g_servoRun == -1 || g_servoRun == 0) {
                        // 衣服安全收回后，接着看窗户开着没，开着就关窗 (对应需求 1)
                        if (g_windowState == 2) {
                            Window_Servo_TriggerRotation(-1); 
                        }
                        g_rain_emergency_active = 2; 
                    }
                    
                    // 如果衣服本来就是收回的，但窗户是开着的 (对应需求 2)
                    if ((g_servoRun == -1 || g_servoRun == 0) && g_windowState == 2) {
                        Window_Servo_TriggerRotation(-1); 
                        g_rain_emergency_active = 2;
                    }
                }
                else if (g_rain_emergency_active == 2)
                {
                    // 等待窗户也完全关闭
                    if (g_windowState == 0) {
                        g_rain_emergency_active = 3; 
                        printf("Emergency Rain Sequence Completed.\r\n");
                    }
                }
            }
            else 
            {
                // 天晴，清除下雨紧急标记
                g_rain_emergency_active = 0;
            }
        }

        // 2. 如果没下雨，读取ADC分压按钮 (通道 1) 进行窗户的手动控制
        if (g_rain_emergency_active == 0) 
        {
            if (adc_port_read(ADC_CH_BUTTON, &button_voltage) == ERRCODE_SUCC) 
            {
                Key_State_t current_sample = KEY_NONE;
                if (button_voltage >= VOLT_S1_MIN && button_voltage < VOLT_S1_MAX) {
                    current_sample = KEY_S1_PRESS;
                } else if (button_voltage >= VOLT_S2_MIN && button_voltage < VOLT_S2_MAX) {
                    current_sample = KEY_S2_PRESS;
                } else if (button_voltage >= VOLT_NONE_MIN) {
                    current_sample = KEY_NONE;
                }

                // 按钮防抖状态机
                if (current_sample == KEY_S1_PRESS) {
                    s1_counter++; s2_counter = 0; none_counter = 0;
                    if (s1_counter >= DEBOUNCE_COUNT) {
                        // S1 按下：如果窗户静止且处于关闭状态，触发开窗
                        if (last_stable_key == KEY_NONE && g_window_remains == 0 && g_windowState == 0) {
                            Window_Servo_TriggerRotation(1); 
                        }
                        last_stable_key = KEY_S1_PRESS;
                        s1_counter = DEBOUNCE_COUNT;
                    }
                } 
                else if (current_sample == KEY_S2_PRESS) {
                    s2_counter++; s1_counter = 0; none_counter = 0;
                    if (s2_counter >= DEBOUNCE_COUNT) {
                        // S2 按下：如果窗户静止且处于打开状态，触发关窗
                        if (last_stable_key == KEY_NONE && g_window_remains == 0 && g_windowState == 2) {
                            Window_Servo_TriggerRotation(-1); 
                        }
                        last_stable_key = KEY_S2_PRESS;
                        s2_counter = DEBOUNCE_COUNT;
                    }
                } 
                else {
                    none_counter++; s1_counter = 0; s2_counter = 0;
                    if (none_counter >= DEBOUNCE_COUNT) {
                        last_stable_key = KEY_NONE;
                        none_counter = DEBOUNCE_COUNT;
                    }
                }
            }
        }

        /* ==================== 3. 窗户舵机脉冲驱动驱动 ==================== */
        if (g_window_remains > 0) {
            Window_Servo_WritePulse(g_window_pulse_duty);
            g_window_remains--;
            if (g_window_remains == 0) {
                if (g_windowState == 1)  g_windowState = 2; // 开窗动作完成
                if (g_windowState == -1) g_windowState = 0; // 关窗动作完成
            }
        } else {
            Window_Servo_WritePulse(WINDOW_SPEED_STOP);
        }

        /* LED 灯光状态感知 */
        if (g_servoRun != last_state) {
            last_state = g_servoRun;
            UpdateOledDisplay(g_servoRun, current_temp, current_humi, tick_count);
            
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

        /* 晾衣架平滑控制逻辑 */
        if (g_servoRun == 1 && !g_servoActionDone)
        {
            Sg92R_SetAngleSmooth(g_current_angle, 60.0f, 1.5f, &g_servoRun);
            if (g_servoRun == 1) {
                g_current_angle = 60.0f;
                g_servoActionDone = 1;
                g_servoRun = -1;
            }
        }
        else if (g_servoRun == 2 && !g_servoActionDone)
        {
            Sg92R_SetAngleSmooth(g_current_angle, -45.0f, 1.5f, &g_servoRun);
            if (g_servoRun == 2) {
                g_current_angle = -45.0f;
                g_servoActionDone = 1;
                g_servoRun = -2;
            }
        }
        
        // 收集传感器数据
        sensor_data_t data;
        data.temp = current_temp;
        data.humi = current_humi;
        data.rain = (rain_voltage < 1300) ? 1 : 0;
        PIR_GetStatus(&data.pir);
        strncpy(data.hanger, g_hanger_status, sizeof(data.hanger)-1);
        data.hanger[sizeof(data.hanger)-1] = '\0';

        mqtt_module_update_sensor_data(&data);
        uapi_systick_delay_ms(20); // 维持 20ms 的核心周期底色
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