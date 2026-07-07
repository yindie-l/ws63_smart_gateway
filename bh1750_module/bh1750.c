#include "bh1750.h"

#include "i2c.h"
#include "cmsis_os2.h"
#include "osal_debug.h"

#define CONFIG_I2C_MASTER_BUS_ID   1

// ADDR接地
#define BH1750_ADDR        0x23

// 连续高分辨率模式
#define BH1750_CONT_H_MODE 0x10

static uint32_t BH1750_Write(uint8_t *buf,uint32_t len)
{
    i2c_data_t data={0};

    data.send_buf=buf;
    data.send_len=len;

    return uapi_i2c_master_write(CONFIG_I2C_MASTER_BUS_ID,
                                 BH1750_ADDR,
                                 &data);
}

static uint32_t BH1750_Read(uint8_t *buf,uint32_t len)
{
    i2c_data_t data={0};

    data.receive_buf=buf;
    data.receive_len=len;

    return uapi_i2c_master_read(CONFIG_I2C_MASTER_BUS_ID,
                                BH1750_ADDR,
                                &data);
}

/* 初始化 */
uint32_t BH1750_Init(void)
{
    uint8_t cmd;

    // 上电
    cmd = 0x01;
    BH1750_Write(&cmd,1);

    osDelay(10);

    // 连续高分辨率模式
    cmd = BH1750_CONT_H_MODE;
    return BH1750_Write(&cmd,1);
}

/* 读取Lux */
uint32_t BH1750_ReadLux(float *lux)
{
    uint8_t buf[2];

    uint32_t ret = BH1750_Read(buf,2);

    if(ret != 0)
        return ret;

    uint16_t raw = ((uint16_t)buf[0]<<8) | buf[1];

    *lux = raw / 1.2f;

    return 0;
}