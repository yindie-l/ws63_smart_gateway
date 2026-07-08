#include "servo_module.h" // 引用头文件
#include "watchdog.h"
#include "app_init.h"

#define SG92R_TASK_STACK_SIZE 0x1000
#define SG92R_TASK_PRIO 17

/* 业务主任务：所有的停顿和延时逻辑都在这里控制 */
void Sg92R_Task(void)
{
    // 1. 初始化引脚
    Sg92R_Init();

    // 2. 初始化平稳过渡到 -45° 起点
    unsigned int start_duty = (unsigned int)sg92r_map(-45, -90, 90, 500, 2500);
    for (int i = 0; i < 30; i++) {
        Sg92R_SetPulseWidth(start_duty); 
    }
    uapi_systick_delay_ms(400); 

    while (1) {
        uapi_watchdog_kick(); // 喂狗

        // --------------------------------------------------
        // A. 执行正向旋转：从 -45° 连续运转到 60°
        // --------------------------------------------------
        Sg92R_Sweep(-45.0f, 60.0f);

        // 【在此处加延时】：到达 60° 顶点，原地锁死静止停留 2 秒
        uapi_systick_delay_ms(2000); 
        uapi_watchdog_kick(); 

        // --------------------------------------------------
        // B. 执行反向旋转：从 60° 连续退回到 -45°
        // --------------------------------------------------
        Sg92R_Sweep(60.0f, -45.0f);

        // 【在此处加延时】：回到 -45° 起点，原地锁死静止停留 2 秒
        uapi_systick_delay_ms(2000);
    }
}

/* OSAL 任务注册入口 */
void SG92RSampleEntry(void)
{
    uint32_t ret;
    osal_task *taskid;
    osal_kthread_lock();
    taskid = osal_kthread_create((osal_kthread_handler)Sg92R_Task, NULL, "Sg92RTask", SG92R_TASK_STACK_SIZE);
    ret = osal_kthread_set_priority(taskid, SG92R_TASK_PRIO);
    if (ret != OSAL_SUCCESS) {
        printf("create task1 failed .\n");
    }
    osal_kthread_unlock();
}