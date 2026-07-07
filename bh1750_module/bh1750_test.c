#include "pinctrl.h"
#include "i2c.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "app_init.h"

#include "bh1750.h"

#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16
#define CONFIG_I2C_MASTER_PIN_MODE 2

#define I2C_MASTER_ADDR 0
#define I2C_SET_BANDRATE 400000

#define I2C_TASK_STACK_SIZE 0x1800
#define I2C_TASK_PRIO 17

void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN,
                      CONFIG_I2C_MASTER_PIN_MODE);

    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN,
                      CONFIG_I2C_MASTER_PIN_MODE);
}

void Bh1750Task(void)
{
    float lux;

    app_i2c_init_pin();

    if(uapi_i2c_master_init(1,
                            I2C_SET_BANDRATE,
                            I2C_MASTER_ADDR)!=0)
    {
        osal_printk("I2C init failed\r\n");
        return;
    }

    osal_printk("I2C init OK\r\n");
    
for (uint8_t addr = 1; addr < 127; addr++)
{
    uint8_t cmd = 0x00;

    i2c_data_t data = {0};
    data.send_buf = &cmd;
    data.send_len = 1;

    uint32_t ret = uapi_i2c_master_write(1, addr, &data);

    if (ret == 0)
    {
        osal_printk("Find I2C Device: 0x%02X\r\n", addr);
    }
}


    if(BH1750_Init()!=0)
    {
        osal_printk("BH1750 init failed\r\n");
        return;
    }

    osal_printk("BH1750 init OK\r\n");

    while(1)
    {
        if(BH1750_ReadLux(&lux)==0)
        {
            int value=(int)(lux*100);

            osal_printk("Light = %d.%02d lux\r\n",
                        value/100,
                        value%100);
        }
        else
        {
            osal_printk("Read BH1750 failed\r\n");
        }

        osal_mdelay(1000);
    }
}

void Bh1750Test(void)
{
    osal_task *task=NULL;

    osal_kthread_lock();

    task=osal_kthread_create((osal_kthread_handler)Bh1750Task,
                             0,
                             "BH1750",
                             I2C_TASK_STACK_SIZE);

    if(task)
    {
        osal_kthread_set_priority(task,I2C_TASK_PRIO);
    }

    osal_kfree(task);

    osal_kthread_unlock();
}

app_run(Bh1750Test);