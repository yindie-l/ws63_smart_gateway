#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "systick.h"
#include "watchdog.h"
#include "app_init.h"
#include "servo_module.h"

#define SG92R_TASK_STACK_SIZE 0x1000
#define SG92R_TASK_PRIO 17
#define BUTTON_GPIO 14 
// 使用 volatile 确保多任务/中断间的变量可见性
volatile int g_servoRun = 0; 

/* 按键中断回调函数 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    g_servoRun = !g_servoRun; // 每次按下，翻转舵机运行/停止状态
    printf("Button pressed. Servo state: %d\r\n", g_servoRun);
}

/* 初始化新的数字按键 */
void KeyInit(void)
{
    uapi_pin_set_mode(BUTTON_GPIO, HAL_PIO_FUNC_GPIO);  /* 1. 配置引脚复用为普通 GPIO */
    gpio_select_core(BUTTON_GPIO, CORES_APPS_CORE); /* 2. 选择核心（参考示例） */
    uapi_gpio_set_dir(BUTTON_GPIO, GPIO_DIRECTION_INPUT); /* 3. 设置方向为输入 */
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func); /* 4. 注册下降沿（按键按下）中断 */
    if (ret != 0) {
        uapi_gpio_unregister_isr_func(BUTTON_GPIO);
    }
}

void Sg92RTask(void)
{
    unsigned int time = 200;
    S92RInit();
    KeyInit(); // 此时初始化的是 GPIO 中断按键

    while (1)
    {
        uapi_watchdog_kick();
        /* 直接判断由中断控制的全局变量 */
        if(g_servoRun)
        {
            RegressMiddle();
            uapi_systick_delay_ms(time);

            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
            EngineTurnLeft();
            uapi_systick_delay_ms(time);
        }
        else
        {
            /* 舵机停止时，稍作延时，释放 CPU */
            uapi_systick_delay_ms(20);
        }
    }
}

void SG92RSampleEntry(void)
{
    uint32_t ret;
    osal_task *taskid;
    osal_kthread_lock();
    taskid = osal_kthread_create((osal_kthread_handler)Sg92RTask, NULL, "Sg92RTask", SG92R_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, SG92R_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}
app_run(SG92RSampleEntry);