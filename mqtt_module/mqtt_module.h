// #ifndef MQTT_MODULE_H
// #define MQTT_MODULE_H

// #include <stdint.h>

// /**
//  * @brief 消息类型枚举
//  */
// typedef enum {
//     EN_MSG_PARS = 1,    // 解析命令
//     EN_MSG_REPORT = 2,  // 上报数据
// } en_msg_type_t;

// /**
//  * @brief MQTT 消息结构体
//  * @note 用于队列传递数据
//  */
// typedef struct {
//     int msg_type;               // 消息类型（EN_MSG_PARS 或 EN_MSG_REPORT）
//     char temp[10];              // 温度字符串（如 "25.5"）
//     char humi[10];              // 湿度字符串（如 "60.2"）
//     char rain[5];               // 雨滴字符串（"0" 或 "1"）
//     char hanger[20];            // 衣架状态（"extended" 或 "retracted"）
//     char pir[5];                // PIR 状态（"0" 或 "1"）
//     char *receive_payload;      // 接收到的命令 payload（仅 EN_MSG_PARS 使用）
//     char *receive_topic;        // 接收到的命令 topic（仅 EN_MSG_PARS 使用）
// } MQTT_msg;

// /**
//  * @brief 初始化 MQTT 模块（创建任务和消息队列）
//  * @note 应在所有外设初始化之后、主循环之前调用一次
//  */
// void mqtt_module_init(void);

// /**
//  * @brief 上报传感器数据到云端（由主循环定期调用）
//  * @param temp     温度值（如 25.5）
//  * @param humi     湿度值（如 60.2）
//  * @param rain     雨滴检测：0-无雨，1-有雨
//  * @param pir      人体红外：0-无人，1-有人
//  * @param hanger   衣架状态字符串："extended" 或 "retracted"
//  */
// void mqtt_module_send_report(float temp, float humi, uint8_t rain, uint8_t pir, const char *hanger);

// #endif

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <stdint.h>

// ========== 与 main.c 交互的数据结构 ==========
typedef struct {
    float temp;          // 温度
    float humi;          // 湿度
    uint8_t rain;        // 0-无雨, 1-有雨
    uint8_t pir;         // 0-无人, 1-有人
    char hanger[20];     // "extended" 或 "retracted"
} sensor_data_t;

// ========== MQTT 内部消息结构（用于发布和解析命令） ==========
typedef enum {
    EN_MSG_PARS = 1,
    EN_MSG_REPORT = 2,
} en_msg_type_t;

typedef struct {
    int msg_type;                // EN_MSG_PARS 或 EN_MSG_REPORT
    char temp[10];
    char humi[10];
    char rain[5];
    char hanger[20];
    char pir[5];
    char *receive_payload;       // 仅 EN_MSG_PARS 使用
    char *receive_topic;         // 仅 EN_MSG_PARS 使用
} MQTT_msg;

// ========== 对外接口 ==========
/**
 * @brief 初始化 MQTT 模块（创建任务）
 * @note 在所有外设初始化之后、主循环之前调用一次
 */
void mqtt_module_init(void);

/**
 * @brief 更新传感器数据（由主循环定期调用）
 * @param data 指向传感器数据的指针（会被复制）
 */
void mqtt_module_update_sensor_data(const sensor_data_t *data);

#endif