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
#include "i2c.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "aht20.h"      // 包含 AHT20 相关函数声明
#include "app_init.h"
#include <stdint.h>

#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16
#define CONFIG_I2C_MASTER_PIN_MODE 2
#define I2C_MASTER_ADDR 0x0          // 主机地址，通常为0
#define I2C_SET_BANDRATE 400000
#define I2C_TASK_STACK_SIZE 0x1800
#define I2C_TASK_PRIO 17

// 初始化 I2C 引脚
void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

// 温湿度读取任务
void Aht20TestTask(void)
{
    float temp = 0.0f;
    float humi = 0.0f;
    uint32_t baudrate = I2C_SET_BANDRATE;
    uint32_t hscode = I2C_MASTER_ADDR;

    // 1. 配置 I2C 引脚
    app_i2c_init_pin();

    // 2. 初始化 I2C 主机
    errcode_t ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        osal_printk("i2c master init failed, ret = 0x%x\r\n", ret);
        return;  // 初始化失败，退出任务
    }
    osal_printk("I2C master initialized successfully.\r\n");

    // 3. 校准 AHT20 传感器（带重试）
    int calib_retry = 0;
    while (AHT20_Calibrate() != 0) {
        osal_printk("AHT20 calibration failed, retry %d...\r\n", calib_retry++);
        osal_mdelay(100);
        if (calib_retry > 10) {
            osal_printk("AHT20 calibration failed after 10 retries, exit.\r\n");
            return;
        }
    }
    osal_printk("AHT20 calibration OK.\r\n");

    // 4. 主循环：读取并打印温湿度
    while (1) {
        ret = AHT20_StartMeasure();
        if (ret != 0) {
            osal_printk("Start measure failed, ret = %d\r\n", ret);
        } else {
            // 等待测量完成（传感器内部需要时间）
            osal_mdelay(100);  // 至少75ms，这里给100ms足够

            ret = AHT20_GetMeasureResult(&temp, &humi);
            if (ret != 0) {
                osal_printk("Get measure result failed, ret = %d\r\n", ret);
            } else {
                int temp_int = (int)(temp * 100);
                int humi_int = (int)(humi * 100);
                osal_printk("Temperature: %d.%02d C, Humidity: %d.%02d %%\r\n", temp_int / 100, temp_int % 100, humi_int / 100, humi_int % 100);
            }
        }
        osal_mdelay(1000);  // 1秒读取一次
    }
}

// 创建任务
void Aht20Test(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)Aht20TestTask, 0, "Aht20Task", I2C_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, I2C_TASK_PRIO);
        osal_printk("AHT20 task created.\r\n");
    } else {
        osal_printk("Failed to create AHT20 task.\r\n");
    }
    osal_kfree(task_handle);  // 句柄已不再需要
    osal_kthread_unlock();
}
