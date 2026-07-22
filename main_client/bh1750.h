#ifndef __BH1750_H__
#define __BH1750_H__

#include "i2c.h"
#include "soc_osal.h"
#include "define.h"

static inline int BH1750_Init(uint8_t bus_id)
{
    i2c_data_t data = { 0 };
    uint8_t tx_buff[1] = { 0 };

    /* 1. 发送 Power On 指令 */
    tx_buff[0] = BH1750_CMD_POWER_ON;
    data.send_buf = tx_buff;
    data.send_len = 1;
    data.receive_buf = NULL;
    data.receive_len = 0;
    if (uapi_i2c_master_write(bus_id, BH1750_SLAVE_ADDR, &data) != ERRCODE_SUCC) {
        return -1;
    }
    osal_msleep(20);

    /* 2. 发送连续高分辨率模式指令 */
    tx_buff[0] = BH1750_CMD_CON_H_RES_MODE;
    data.send_buf = tx_buff;
    data.send_len = 1;
    if (uapi_i2c_master_write(bus_id, BH1750_SLAVE_ADDR, &data) != ERRCODE_SUCC) {
        return -1;
    }
    /* 高分辨率首次积分需等待约 180ms */
    osal_msleep(180);
    return 0;
}

/**
 * @brief 读取实时光强数据 (带无应答自动唤醒防呆机制)
 * @param bus_id  I2C总线ID
 * @param lux_val 接收输出的光照度值指针 (单位: lx)
 * @return 0:成功, -1:读取失败
 */
static inline int BH1750_ReadLux(uint8_t bus_id, uint32_t *lux_val)
{
    if (lux_val == NULL) return -1;
    
    i2c_data_t data = { 0 };
    uint8_t rx_buff[2] = { 0 };

    data.send_buf = NULL;
    data.send_len = 0;
    data.receive_buf = rx_buff;
    data.receive_len = 2;

    if (uapi_i2c_master_read(bus_id, BH1750_SLAVE_ADDR, &data) == ERRCODE_SUCC) {
        uint16_t raw_data = (rx_buff[0] << 8) | rx_buff[1];
        /* 按照 BH1750 数据手册公式：真实光强 = 原始数据 / 1.2 */
        *lux_val = (uint32_t)raw_data * 10 / 12;
        return 0;
    } else {
        /* 读取失败：说明传感器可能被热插拔或总线重置导致休眠，尝试重连发送启动指令 */
        uint8_t tx_buff[1] = { BH1750_CMD_CON_H_RES_MODE };
        data.send_buf = tx_buff;
        data.send_len = 1;
        data.receive_buf = NULL;
        data.receive_len = 0;
        uapi_i2c_master_write(bus_id, BH1750_SLAVE_ADDR, &data);
        return -1;
    }
}

#endif /* __BH1750_H__ */