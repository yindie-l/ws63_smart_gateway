#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <stdint.h>

// MQTT 消息结构体（保留用于上报）
typedef enum {
    EN_MSG_PARS = 1,
    EN_MSG_REPORT = 2,
} en_msg_type_t;

typedef struct {
    int msg_type;
    char temp[10];
    char humi[10];
    char rain[5];
    char hanger[20];
    char pir[5];
    char light[10]; 
    char *receive_payload;
    char *receive_topic;
} MQTT_msg;

/**
 * @brief 直接初始化 MQTT（在主任务中调用，不创建独立任务）
 * @return 0-成功，-1-失败
 */
int mqtt_module_init_direct(void);

/**
 * @brief 上报传感器数据（由主循环定期调用）
 */
void mqtt_module_send_report(float temp, float humi, uint8_t rain, uint8_t pir, const char *hanger);

#endif