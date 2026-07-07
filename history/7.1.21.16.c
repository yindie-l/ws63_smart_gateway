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

/*引脚的宏定义*/
#define CONFIG_PWM_PIN            3
#define PIR_REL_GPIO_PIN          9
#define LED_RED_GPIO              7      
#define LED_GREEN_GPIO            11  
#define LED_YELLOW_GPIO           10  
#define BUTTON_GPIO               14    
#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16
/*12给雨滴ADC   #define BSP_SG92R    2*/

/*引脚的复用模式*/
#define CONFIG_I2C_MASTER_PIN_MODE 2
#define PIR_GPIO_MODE              0
#define PIR_GPIO_MODE              0
#define CONFIG_PWM_PIN_MODE       1

#define I2C_MASTER_ADDR 0x0
#define I2C_SLAVE1_ADDR 0x38
#define I2C_SET_BANDRATE 400000

#define PWM_CHANNEL               3
#define PWM_GROUP_ID              3

/*任务的优先级和任务栈大小*/ 
#define MAIN_TASK_STACK_SIZE 0x1800
#define MAIN_TASK_PRIO       17
#define BUZZER_TASK_STACK_SIZE 0x1000
#define BUZZER_TASK_PRIO       18     

/*时间宏定义*/
#define time 200
#define SERVO_TIME_DELAY     200

/* I2C初始化函数*/
void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/* 全局状态变量 */
volatile int g_servoRun = 0; 
volatile int g_buzzerTrigger = 0;      // 蜂鸣器触发标志：0-未触发，1-需要鸣叫

/* PWM 周期结束回调函数 */
static errcode_t pwm_sample_callback(uint8_t channel)
{
    UNUSED(channel);
    // 周期完成回调，通常留空或用于调试
    return ERRCODE_SUCC;
}

/* 按键中断回调函数 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    // 每次按下按键，状态在 1（运行）和 0（停止）之间翻转
    g_servoRun = !g_servoRun; 
    printf("Button pressed. Servo state: %d\r\n", g_servoRun);
}

/* 初始化按键中断函数 */
void KeyInit(void)
{
    uapi_pin_set_mode(BUTTON_GPIO, HAL_PIO_FUNC_GPIO);/* 1. 配置引脚复用为普通 GPIO */
    gpio_select_core(BUTTON_GPIO, CORES_APPS_CORE); /* 2. 选择核心 */
    uapi_gpio_set_dir(BUTTON_GPIO, GPIO_DIRECTION_INPUT);/* 3. 设置方向为输入 */
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func);/* 4. 注册下降沿中断（按键按下） */
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BUTTON_GPIO);
    }
}

/* 红外传感器中断回调函数 */
static void pir_interrupt_handler(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    // 无论当前舵机是什么状态，只要检测到人就触发蜂鸣器
    if (g_buzzerTrigger == 0) {
        g_buzzerTrigger = 1; // 激活蜂鸣器任务
    }

    if (g_servoRun == 1) {
        g_servoRun = 2; 
        printf("[PIR] Human motion detected! Servo emergency STOP.\r\n");
        printf("Buzzer Trigger: %d\r\n", g_buzzerTrigger);
    }
}

/* 初始化红外传感器中断函数*/
void PirInterruptInit(void)
{
    uapi_pin_set_mode(PIR_REL_GPIO_PIN, (pin_mode_t)PIR_GPIO_MODE);// 1. 将引脚设置为GPIO功能
    gpio_select_core(PIR_REL_GPIO_PIN, CORES_APPS_CORE);           // 2. 选择核心
    uapi_gpio_set_dir(PIR_REL_GPIO_PIN, GPIO_DIRECTION_INPUT);     // 3. 设置GPIO方向为输入
    errcode_t ret = uapi_gpio_register_isr_func(PIR_REL_GPIO_PIN, GPIO_INTERRUPT_RISING_EDGE, pir_interrupt_handler);// 根据你的 pir.c 逻辑（Highlevel 为 detected），此处注册上升沿中断（从无人到有人）
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

    ssd1306_Fill(0); // 清屏
    
    // 1. 显示舵机/系统运行状态
    ssd1306_SetCursor(0, 0);
    if (state == 1) {
        ssd1306_DrawString("Servo: RUNNING", Font_7x10, 1); 
    } else if (state == 2) {
        ssd1306_DrawString("Servo: WARNING", Font_7x10, 1);
        ssd1306_SetCursor(0, 10);
        ssd1306_DrawString("Human Detected!", Font_7x10, 1);
    } else {
        ssd1306_DrawString("Servo: STOPPED", Font_7x10, 1);
    }

    // 2. 将浮点数转换为整型进行安全格式化（防止轻量级库不支持 %f）
    int temp_int = (int)(temp * 100);
    int humi_int = (int)(humi * 100);
    snprintf(temp_str, sizeof(temp_str), "Temp: %d.%02d C", temp_int / 100, abs(temp_int % 100));
    snprintf(humi_str, sizeof(humi_str), "Humi: %d.%02d %%", humi_int / 100, abs(humi_int % 100));

    // 3. 在屏幕中下方显示温湿度
    ssd1306_SetCursor(0, 24);
    ssd1306_DrawString(temp_str, Font_7x10, 1);
    ssd1306_SetCursor(0, 38);
    ssd1306_DrawString(humi_str, Font_7x10, 1);
    
    ssd1306_UpdateScreen(); // 刷新屏幕
}

/* LED 所有初始化*/
void LedsInit(void)
{
    pin_t leds[] = {LED_RED_GPIO, LED_GREEN_GPIO, LED_YELLOW_GPIO};
    for (int i = 0; i < 3; i++) {
        uapi_pin_set_mode(leds[i], HAL_PIO_FUNC_GPIO);     
        gpio_select_core(leds[i], CORES_APPS_CORE);       
        uapi_gpio_set_dir(leds[i], GPIO_DIRECTION_OUTPUT); 
        SetLedState(leds[i], 0);                           // 默认初始化为灭
    }
    printf("LEDs (Red:%d, Green:%d, Yellow:%d) initialized.\r\n", LED_RED_GPIO, LED_GREEN_GPIO, LED_YELLOW_GPIO);
}

/* 主任务 */
void mainTask(void)
{
    uint32_t baudrate = I2C_SET_BANDRATE;
    uint32_t hscode = I2C_MASTER_ADDR;
    
    // 1. 初始化唯一的一组 I2C 总线 
    app_i2c_init_pin();
    errcode_t ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        printf("i2c init failed, ret = %0x\r\n", ret);
    }

    // 2. 初始化所有外设驱动 
    ssd1306_Init();
    KeyInit();           
    S92RInit();          
    PIR_Init();          
    PirInterruptInit();  
    LedsInit();
    
    // 3. 在线校准 AHT20 温湿度传感器
    int calib_retry = 0;
    while (AHT20_Calibrate() != 0) {
        printf("AHT20 calibration failed, retry %d...\r\n", calib_retry++);
        uapi_systick_delay_ms(100);
        if (calib_retry > 5) {
            printf("AHT20 missing or failed. Skipping...\r\n");
            break;
        }
    }

    int last_state = -1;
    float current_temp = 0.0f;
    float current_humi = 0.0f;
    uint32_t tick_count = 0;

    while (1)
    {
        uapi_watchdog_kick();
        /* 定时读取温湿度：每隔大约 5 次循环（~1到2秒，受舵机延时影响）读取一次 */
        if (tick_count % 5 == 0) 
        {
            ret = AHT20_StartMeasure();
            if (ret == 0) {
                // AHT20 硬件转换需要至少 75ms，这里给 80ms 延时让其完成转换
                uapi_systick_delay_ms(80); 
                AHT20_GetMeasureResult(&current_temp, &current_humi);
            }
            // 定时刷新全屏数据
            UpdateOledDisplay(g_servoRun, current_temp, current_humi);
        }

        /* 检测状态突变：切换三色灯状态，并立刻刷一次屏幕响应 */
        if (g_servoRun != last_state) {
            last_state = g_servoRun;
            UpdateOledDisplay(g_servoRun, current_temp, current_humi);
            
            if (g_servoRun == 1) {
                SetLedState(LED_GREEN_GPIO, 1);  SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 0);    
            } else if (g_servoRun == 2) {
                SetLedState(LED_GREEN_GPIO, 0);  SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 1);    
            } else {
                SetLedState(LED_GREEN_GPIO, 0);  SetLedState(LED_YELLOW_GPIO, 1); SetLedState(LED_RED_GPIO, 0);    
            }
        }

        /* 根据中断控制的全局变量决定舵机是否旋转 */
        if (g_servoRun == 1)
        {
            // 舵机旋转逻辑
            RegressMiddle();
            uapi_systick_delay_ms(time);
            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
        }
        else
        {
            uapi_systick_delay_ms(20);// 舵机停止时，让出 CPU 权限，避免死循环占满资源
        }
        
        tick_count++;
    }
}

/* 蜂鸣器专用的独立控制任务 */
static void *buzzer_task(const char *arg)
{
    UNUSED(arg);

    // 1. 配置持续发送的 PWM 波形
    pwm_config_t cfg_repeat = {
        20000, // 周期
        10000, // 高电平时间
        0,     // 相位偏移
        0,     // 0 代表持续发送波形
        true   // 是否循环
    };

    // 2. 初始化管脚和 PWM 驱动
    uapi_pin_set_mode(CONFIG_PWM_PIN, CONFIG_PWM_PIN_MODE);
    uapi_pwm_init();
    
    // 3. 先 Open 通道并写入配置
    uapi_pwm_open(PWM_CHANNEL, &cfg_repeat);
    
    // 4. 配置中断
    uapi_pwm_unregister_interrupt(PWM_GROUP_ID);
    uapi_pwm_register_interrupt(PWM_GROUP_ID, pwm_sample_callback);
    
    // 5. 将通道加入到 Group 中
    uint8_t channel_id = PWM_CHANNEL;
    uapi_pwm_set_group(PWM_GROUP_ID, &channel_id, 1);

    printf("[BUZZER] Task initialized and ready.\r\n");

    // 6. 业务主循环
    while (1) {
        if (g_buzzerTrigger == 1) {
            printf("[BUZZER] Start singing for 3 seconds...\r\n");
            
            // 开启 PWM 输出
            uapi_pwm_start(PWM_GROUP_ID); 
            
            // 延时 3000 毫秒 (3秒)
            uapi_tcxo_delay_ms(3000);        
            
            // 【重要修改】使用 stop 停止输出波形，而不是 close 销毁通道
            uapi_pwm_close(PWM_GROUP_ID); 
            
            // 复位触发标志
            g_buzzerTrigger = 0;
            printf("[BUZZER] Stop singing.\r\n");
        }
        
        // 没被触发时，释放 CPU 资源（延时 50ms）
        uapi_tcxo_delay_ms(50);
    }
    
    return NULL;
}

void main_entry(void)
{
    uint32_t ret;
    osal_task *taskid;
    osal_task *buzzer_taskid; // 声明新任务句柄
    osal_kthread_lock();
    
    // 1. 创建原本的系统主任务 
    taskid = osal_kthread_create((osal_kthread_handler)mainTask, NULL, "mainTask", MAIN_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, MAIN_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task mainTask failed.\n");
    }

    // 2.创建独立蜂鸣器控制任务
    buzzer_taskid = osal_kthread_create((osal_kthread_handler)buzzer_task, NULL, "BuzzerTask", BUZZER_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(buzzer_taskid, BUZZER_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task BuzzerTask failed.\n");
    } else {
        osal_kfree(buzzer_taskid); // 释放轻量级任务句柄内存（防止句柄泄漏，符合海思规范）
    }

    if (taskid != NULL) {
        osal_kfree(taskid);
    }

    osal_kthread_unlock();
}

app_run(main_entry);
