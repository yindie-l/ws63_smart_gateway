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
//#define BSP_SG92R               2
#define CONFIG_PWM_PIN            3
#define PIR_REL_GPIO_PIN          9
#define LED_RED_GPIO              7      
#define LED_GREEN_GPIO            11  
#define LED_YELLOW_GPIO           10  
#define BUTTON_GPIO               14    
#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16


/*引脚的复用模式*/
#define CONFIG_I2C_MASTER_PIN_MODE 2
#define PIR_GPIO_MODE              0
#define CONFIG_PWM_PIN_MODE       1

#define I2C_MASTER_ADDR 0x0
#define I2C_SLAVE1_ADDR 0x38
#define I2C_SET_BANDRATE 400000

#define PWM_CHANNEL               3
#define PWM_GROUP_ID              3

/*任务的优先级和任务栈大小*/ 
#define MAIN_TASK_STACK_SIZE 0x2000
#define MAIN_TASK_PRIO       17

/*时间宏定义*/
#define time 200
#define PWM_DELAY_1000MS 1000
/* I2C初始化函数*/
void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/* 全局状态变量 */
volatile int g_servoRun = 0; 
volatile int g_buzzerTrigger = 0;   // 0-未触发，1-需要播放

/* PWM 周期结束回调函数 */
static errcode_t pwm_sample_callback(uint8_t channel)
{
    UNUSED(channel);
    return ERRCODE_SUCC;
}

/* 蜂鸣器完整初始化（严格对照官方例程顺序） */
void BuzzerInit(void)
{
    // 计算3秒需要的脉冲数：32000000Hz 时钟下，周期20000代表每秒发送 1600 个波形。
    // 响 3 秒 = 1600 * 3 = 4800 个波形。
    pwm_config_t cfg_3seconds = {
        20000,  // 周期
        10000,  // 高电平持续时间
        0,      // 相位偏移
        1,   // 【核心修改】只发送 4800 个波形（刚好 3 秒），发完硬件自动停止发声！
        true    // 是否循环
    };

    // 1. 设置引脚模式
    uapi_pin_set_mode(CONFIG_PWM_PIN, CONFIG_PWM_PIN_MODE);
    // 2. 初始化 PWM 模块
    uapi_pwm_init();
    // 3. 打开通道并下发 3 秒定长配置
    uapi_pwm_open(PWM_CHANNEL, &cfg_3seconds);
    
    // 4. 配置中断
    uapi_pwm_unregister_interrupt(PWM_GROUP_ID);
    uapi_pwm_register_interrupt(PWM_GROUP_ID, pwm_sample_callback);
    
    // 5. 将通道加入 Group 中
    uint8_t channel_id = PWM_CHANNEL;
    uapi_pwm_set_group(PWM_GROUP_ID, &channel_id, 1);
}

/* 按键中断回调函数 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    g_servoRun = !g_servoRun; 
    printf("Button pressed. Servo state: %d\r\n", g_servoRun);
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
    
    // 中断里只打标记
    if (g_servoRun == 1) {
        g_servoRun = 2; 
        if (g_buzzerTrigger == 0) {
        g_buzzerTrigger = 1; 
    }
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

    ssd1306_Fill(0); 
    
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

    int temp_int = (int)(temp * 100);
    int humi_int = (int)(humi * 100);
    snprintf(temp_str, sizeof(temp_str), "Temp: %d.%02d C", temp_int / 100, abs(temp_int % 100));
    snprintf(humi_str, sizeof(humi_str), "Humi: %d.%02d %%", humi_int / 100, abs(humi_int % 100));

    ssd1306_SetCursor(0, 24);
    ssd1306_DrawString(temp_str, Font_7x10, 1);
    ssd1306_SetCursor(0, 38);
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
    S92RInit();          
    PIR_Init();          
    PirInterruptInit();  
    LedsInit();
    BuzzerInit(); 
    
    int calib_retry = 0;
    while (AHT20_Calibrate() != 0) {
        uapi_systick_delay_ms(100);
        if (calib_retry++ > 5) break;
    }

    int last_state = -1;
    float current_temp = 0.0f;
    float current_humi = 0.0f;
    uint32_t tick_count = 0;

    while (1)
    {
        uapi_watchdog_kick();

        /* 蜂鸣器响应 */
        if (g_buzzerTrigger == 1) {
        BuzzerInit();
        printf("[BUZZER] Human detected! Hardware starts 3s pulse wave...\r\n");
        uapi_pwm_start(PWM_GROUP_ID);
        uapi_tcxo_delay_ms((uint32_t)PWM_DELAY_1000MS);
        uapi_pwm_close(PWM_CHANNEL); 
        uapi_pwm_deinit();
        g_buzzerTrigger = 0;
        }

        /* 定时读取温湿度 */
        if (tick_count % 5 == 0) 
        {
            ret = AHT20_StartMeasure();
            if (ret == 0) {
                uapi_systick_delay_ms(80); 
                AHT20_GetMeasureResult(&current_temp, &current_humi);
            }
            UpdateOledDisplay(g_servoRun, current_temp, current_humi);
        }

        /* 检测状态LED */
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

        /* 舵机控制 */
        if (g_servoRun == 1)
        {
            RegressMiddle();
            uapi_systick_delay_ms(time);
            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
        }
        else
        {
            uapi_systick_delay_ms(20);
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