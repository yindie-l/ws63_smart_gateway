/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pinctrl.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "app_init.h"
#include "pir_test.h"

#define PIR_TASK_STACK_SIZE     0x1800
#define PIR_TASK_PRIO           17

// 人体红外检测任务
void PirTestTask(void)
{
    uint8_t detected = 0;
    uint32_t ret;

    // 初始化传感器引脚
    ret = PIR_Init();
    if (ret != 0) {
        osal_printk("PIR initialization failed, ret = %d\r\n", ret);
        return;
    }
    osal_printk("PIR sensor initialized.\r\n");

    // 主循环：每500ms读取一次状态并打印
    while (1) {
        ret = PIR_GetStatus(&detected);
        if (ret != 0) {
            osal_printk("Read PIR status failed, ret = %d\r\n", ret);
        } else {
            if (detected) {
                osal_printk("Human motion detected!\r\n");
            } 
        }
        osal_mdelay(500);   // 每500ms查询一次
    }
}

// 创建PIR测试任务
void PirTest(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)PirTestTask, 0, "PirTask", PIR_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, PIR_TASK_PRIO);
        osal_printk("PIR task created.\r\n");
    } else {
        osal_printk("Failed to create PIR task.\r\n");
    }
    osal_kfree(task_handle);
    osal_kthread_unlock();
}
