// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdarg.h>

// #include "MQTTClient.h"
// #include "MQTTClientPersistence.h"
// #include "osal_debug.h"
// #include "soc_osal.h"
// #include "app_init.h"
// #include "common_def.h"
// #include "watchdog.h"

// #include "cjson/cjson_demo.h"
// #include "wifi/wifi_connect.h"
// #include "mqtt_module.h"
// extern int MQTTClient_init(void);
// // 引用 main.c 中定义的全局状态变量（用于控制舵机/窗户）
// #include "../main/main.h"   // 提供 WINDOW_SPEED_* 等宏和变量声明
// #include <osal_wait.h>

// // ============ 华为云连接参数 ============
// #define ADDRESS "tcp://7b25aad268.st1.iotda-device.cn-north-4.myhuaweicloud.com"
// #define CLIENTID "6a4610777f2e6c302f812758_ws63_0_1_2026070613"
// #define USERNAME "6a4610777f2e6c302f812758_ws63"
// #define PASSWORD "270f0229be8bdb8bdaeaf3ec5173376e1a73f18f4426e71b9f2193440c9298bf"

// #define CONFIG_WIFI_SSID "wbz"
// #define CONFIG_WIFI_PWD "20242081052"
// #define MQTT_STA_TASK_PRIO 16
// #define MQTT_STA_TASK_STACK_SIZE 0x1000
// #define QOS 1
// #define MSG_MAX_LEN 128
// #define MSG_QUEUE_SIZE 32
// // 函数声明，供后续函数调用
// int mqtt_connect(void);
// int mqtt_publish(const char *topic, MQTT_msg *report_msg);
// // 静态变量（模块内部使用）
// static unsigned long g_msg_queue;
// volatile MQTTClient_deliveryToken deliveredtoken;
// MQTTClient client;
// static char g_report_topic[128];

// // 外部变量声明（定义在 main.c）
// extern volatile int g_servoRun;
// extern volatile int g_servoActionDone;
// extern volatile int g_windowState;
// extern volatile uint32_t g_window_remains;
// extern volatile unsigned int g_window_pulse_duty;
// extern char g_hanger_status[20];   // 衣架状态字符串，由主程序维护

// // ========== 回调函数 ==========
// void delivered(void *context, MQTTClient_deliveryToken dt)
// {
//     unused(context);
//     printf("Message with token value %d delivery confirmed\r\n", dt);
//     deliveredtoken = dt;
// }

// // 命令到达回调（直接处理，不经过队列）
// int msgArrved(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
// {
//     unused(context);
//     unused(topic_len);
//     printf("=== msgArrved CALLED ===\n");
//     printf("topic: %s, payload: %s\n", topic_name, (char *)message->payload);

//     // 1. 提取 request_id（用于响应）
//     char *request_id = NULL;
//     const char *prefix = "/sys/commands/request_id=";
//     char *pos = strstr(topic_name, prefix);
//     if (pos != NULL) {
//         request_id = pos + strlen(prefix);
//         char *end = strchr(request_id, '/');
//         if (end == NULL) end = strchr(request_id, ' ');
//         if (end != NULL) *end = '\0';
//         char *id_copy = osal_kmalloc(strlen(request_id) + 1, 0);
//         if (id_copy != NULL) {
//             strcpy(id_copy, request_id);
//             request_id = id_copy;
//         } else {
//             request_id = NULL;
//         }
//     }

//     // 2. 解析命令（target / action）
//     char target[20] = {0};
//     char action[20] = {0};
//     int parse_ret = parse_command_json(message->payload, target, sizeof(target), action, sizeof(action));

//     if (parse_ret == 0) {
//         printf("Recv CMD: target=%s, action=%s\n", target, action);

//         // ====== 控制衣架 ======
//         if (strcmp(target, "hanger") == 0) {
//             if (strcmp(action, "extend") == 0) {
//                 strcpy(g_hanger_status, "extended");
//                 g_servoRun = 1;          // 1 对应伸出到 60°
//                 g_servoActionDone = 0;
//                 printf(">>> 衣架执行伸出\n");
//             } else if (strcmp(action, "retract") == 0) {
//                 strcpy(g_hanger_status, "retracted");
//                 g_servoRun = 2;          // 2 对应收回到 -45°
//                 g_servoActionDone = 0;
//                 printf(">>> 衣架执行收回\n");
//             }
//             // 上报最新状态（立即上报一次）
//             // 由于我们无法在此直接获取传感器数据，可以发送一个带当前状态的报告
//             // 但更合理的做法是：主循环会定期上报，这里可省略，或强制触发一次上报（通过队列？）
//             // 这里我们选择不额外上报，等待下一个周期。
//         }
//         // ====== 控制窗户 ======
//         else if (strcmp(target, "window") == 0) {
//             if (strcmp(action, "extend") == 0) {
//                 // 开窗：设置窗户舵机参数（模仿 Window_Servo_TriggerRotation(1)）
//                 g_window_pulse_duty = WINDOW_SPEED_OPEN_SLOW;
//                 g_window_remains = CYCLES_FOR_90_DEGREE;
//                 g_windowState = 1;
//                 printf(">>> 窗户执行打开\n");
//             } else if (strcmp(action, "retract") == 0) {
//                 g_window_pulse_duty = WINDOW_SPEED_CLOSE_SLOW;
//                 g_window_remains = CYCLES_FOR_90_DEGREE;
//                 g_windowState = -1;
//                 printf(">>> 窗户执行关闭\n");
//             }
//         }
//     }

//     // 3. 发送命令响应（带 request_id）
//     if (request_id != NULL) {
//         char response_topic[256];
//         sprintf(response_topic, "$oc/devices/%s/sys/commands/response/request_id=%s", USERNAME, request_id);
//         printf("=== DEBUG: response_topic = [%s] ===\n", response_topic);

//         char *resp_str = build_command_response(0, "success");
//         if (resp_str != NULL) {
//             MQTTClient_message pubmsg = MQTTClient_message_initializer;
//             MQTTClient_deliveryToken token;
//             pubmsg.payload = resp_str;
//             pubmsg.payloadlen = strlen(resp_str);
//             pubmsg.qos = QOS;
//             pubmsg.retained = 0;
//             int rc = MQTTClient_publishMessage(client, response_topic, &pubmsg, &token);
//             printf("Command response sent, rc=%d, topic=%s\n", rc, response_topic);
//             osal_kfree(resp_str);
//         }
//         osal_kfree(request_id);
//     }

//     return 1;
// }

// void connlost(void *context, char *cause)
// {
//     unused(context);
//     printf("mqtt_connection_lost() error, cause: %s\n", cause);
// }

// // ========== MQTT 基础操作 ==========
// int mqtt_subscribe(const char *topic)
// {
//     printf("subscribe start: %s\r\n", topic);
//     MQTTClient_subscribe(client, topic, QOS);
//     return 0;
// }

// int mqtt_publish(const char *topic, MQTT_msg *report_msg)
// {
//     // 检查连接状态（如果库提供函数）
//     // 如果不提供，可以跳过检查，直接发送，失败返回错误
//     MQTTClient_message pubmsg = MQTTClient_message_initializer;
//     MQTTClient_deliveryToken token;
//     int rc = 0;
//     char *msg = make_json("gateway_data",
//                           report_msg->temp,
//                           report_msg->humi,
//                           report_msg->rain,
//                           report_msg->hanger,
//                           report_msg->pir);
//     if (msg == NULL) {
//         printf("make_json failed\r\n");
//         return -1;
//     }
//     pubmsg.payload = msg;
//     pubmsg.payloadlen = (int)strlen(msg);
//     pubmsg.qos = QOS;
//     pubmsg.retained = 0;
//     rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
//     if (rc != MQTTCLIENT_SUCCESS) {
//         printf("mqtt_publish failed, rc=%d\r\n", rc);
//     }
//     printf("mqtt_publish(), payload: %s\r\n", msg);
//     osal_kfree(msg);
//     return rc;
// }

// int mqtt_connect(void)
// {
//     MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
//     int rc;
//     printf("start mqtt connect...\r\n");
//     MQTTClient_init();
//     MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
//     conn_opts.keepAliveInterval = 120;
//     conn_opts.cleansession = 1;
//     conn_opts.username = USERNAME;
//     conn_opts.password = PASSWORD;
//     MQTTClient_setCallbacks(client, NULL, connlost, msgArrved, delivered);
//     if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
//         printf("Failed to connect, return code %d\n", rc);
//         return -1;
//     }
//     printf("connect success\r\n");
//     return rc;
// }

// // ========== MQTT 任务（独立线程） ==========
// int mqtt_task(void)
// {
//     wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
//     int ret = mqtt_connect();
//     if (ret != 0) {
//         printf("connect failed, result %d\n", ret);
//     }
//     osal_msleep(1000);

//     char *cmd_topic = combine_strings(3, "$oc/devices/", USERNAME, "/sys/commands/#");
//     ret = mqtt_subscribe(cmd_topic);
//     if (ret < 0) {
//         printf("subscribe topic error, result %d\n", ret);
//     }
//     osal_kfree(cmd_topic);

//     sprintf(g_report_topic, "$oc/devices/%s/sys/properties/report", USERNAME);

//     // ====== 消息处理循环（修复版） ======
//     while (1) {
//         MQTT_msg report_msg;
//         uint32_t recv_size = sizeof(MQTT_msg);
//         ret = osal_msg_queue_read_copy(g_msg_queue, &report_msg, &recv_size, OSAL_WAIT_FOREVER);
//         if (ret != 0) {
//             printf("queue_read ret = %#x, recv_size=%d\n", ret, recv_size);
//             osal_msleep(10);
//             continue;
//         }

//         if (report_msg.msg_type == EN_MSG_REPORT) {
//             int pub_ret = mqtt_publish(g_report_topic, &report_msg);
//             if (pub_ret == 0) {
//                 printf(">>> MQTT report published: temp=%s, humi=%s, hanger=%s\n",
//                        report_msg.temp, report_msg.humi, report_msg.hanger);
//             } else {
//                 printf(">>> MQTT publish failed, ret=%d\n", pub_ret);
//             }
//         }
//     }
//     return 0;
// }

// // ========== 对外接口函数 ==========

// /**
//  * 初始化 MQTT 模块（创建队列和任务）
//  */
// void mqtt_module_init(void)
// {
//     uint32_t ret;
//     uapi_watchdog_disable();
//     ret = osal_msg_queue_create("mqtt_queue", MSG_QUEUE_SIZE, &g_msg_queue, 0, MSG_MAX_LEN);
//     if (ret != OSAL_SUCCESS) {
//         printf("create queue failure! error:%x\n", ret);
//         return;
//     }
//     printf("create queue success! queue_id = %d\n", g_msg_queue);

//     osal_kthread_lock();
//     osal_task *task_handle = osal_kthread_create((osal_kthread_handler)mqtt_task, 0, "MqttDemoTask", MQTT_STA_TASK_STACK_SIZE);
//     if (task_handle != NULL) {
//         osal_kthread_set_priority(task_handle, MQTT_STA_TASK_PRIO);
//         printf("mqtt_task created successfully!\n");  // 新增
//         osal_kfree(task_handle);
//     } else {
//         printf("mqtt_task creation FAILED!\n");       // 新增
//     }
//     osal_kthread_unlock();
// }

// /**
//  * 上报传感器数据（由主循环调用）
//  */
// void mqtt_module_send_report(float temp, float humi, uint8_t rain, uint8_t pir, const char *hanger)
// {
//     MQTT_msg *mqtt_msg = osal_kmalloc(sizeof(MQTT_msg), 0);
//     if (mqtt_msg == NULL) {
//         printf("mqtt_send_report malloc failed\r\n");
//         return;
//     }
//     mqtt_msg->msg_type = EN_MSG_REPORT;
//     snprintf(mqtt_msg->temp, sizeof(mqtt_msg->temp), "%.1f", temp);
//     snprintf(mqtt_msg->humi, sizeof(mqtt_msg->humi), "%.1f", humi);
//     snprintf(mqtt_msg->rain, sizeof(mqtt_msg->rain), "%d", rain);
//     snprintf(mqtt_msg->pir, sizeof(mqtt_msg->pir), "%d", pir);
//     strncpy(mqtt_msg->hanger, hanger, sizeof(mqtt_msg->hanger) - 1);
//     mqtt_msg->hanger[sizeof(mqtt_msg->hanger) - 1] = '\0';

//     uint32_t ret = osal_msg_queue_write_copy(g_msg_queue, mqtt_msg, sizeof(MQTT_msg), OSAL_WAIT_FOREVER);
//     if (ret != 0) {
//         printf("queue write ret = %#x\n", ret);
//         osal_kfree(mqtt_msg);
//     } else {
//         printf(">>> queue write SUCCESS, msg_type=%d\n", mqtt_msg->msg_type);
//         // 注意：不要在此处释放 mqtt_msg，它会被队列复制一份，之后由队列管理释放
//         // 但 osal_msg_queue_write_copy 会复制数据，所以我们仍需要释放原内存
//         osal_kfree(mqtt_msg);
//     }
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "osal_debug.h"
#include "soc_osal.h"
#include "app_init.h"
#include "common_def.h"
#include "watchdog.h"

#include "cjson/cjson_demo.h"
#include "wifi/wifi_connect.h"
#include "mqtt_module.h"

// ============ 华为云连接参数 ============
#define ADDRESS "tcp://7b25aad268.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define CLIENTID "6a4610777f2e6c302f812758_ws63_0_1_2026070611"
#define USERNAME "6a4610777f2e6c302f812758_ws63"
#define PASSWORD "3d7d80cadb192c39c066229400bc8ca71539b1f7ed1c3cc1f28160f9e4ac7870"

#define CONFIG_WIFI_SSID "wbz"
#define CONFIG_WIFI_PWD "20242081052"
#define MQTT_STA_TASK_PRIO 16
#define MQTT_STA_TASK_STACK_SIZE 0x2000
#define QOS 1
#define MSG_MAX_LEN 128

// 静态变量
static MQTTClient client;
static char g_report_topic[128];
static char g_username[64] = USERNAME;

// 传感器数据（共享）
static sensor_data_t g_sensor_data;
static volatile uint8_t g_data_updated = 0;  // 有新数据待发送

// 外部函数声明（Paho 库提供）
extern int MQTTClient_init(void);

// ========== MQTT 回调函数 ==========
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    unused(dt);
    // printf("Message with token %d delivered\n", dt);
}

int msgArrved(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);
    printf("=== msgArrved CALLED ===\n");
    printf("topic: %s, payload: %s\n", topic_name, (char *)message->payload);

    // 1. 提取 request_id
    char *request_id = NULL;
    const char *prefix = "/sys/commands/request_id=";
    char *pos = strstr(topic_name, prefix);
    if (pos != NULL) {
        request_id = pos + strlen(prefix);
        char *end = strchr(request_id, '/');
        if (end == NULL) end = strchr(request_id, ' ');
        if (end != NULL) *end = '\0';
        char *id_copy = osal_kmalloc(strlen(request_id) + 1, 0);
        if (id_copy != NULL) {
            strcpy(id_copy, request_id);
            request_id = id_copy;
        } else {
            request_id = NULL;
        }
    }

    // 2. 解析命令 (target/action)
    char target[20] = {0};
    char action[20] = {0};
    int parse_ret = parse_command_json(message->payload, target, sizeof(target), action, sizeof(action));

    if (parse_ret == 0) {
        printf("Recv CMD: target=%s, action=%s\n", target, action);
        // 这里仅打印，实际控制由 main.c 中全局变量完成
        // 若需要直接控制，可在此修改 g_servoRun 等，但需 extern 声明
    }

    // 3. 发送命令响应
    if (request_id != NULL) {
        char response_topic[256];
        sprintf(response_topic, "$oc/devices/%s/sys/commands/response/request_id=%s", g_username, request_id);
        char *resp_str = build_command_response(0, "success");
        if (resp_str != NULL) {
            MQTTClient_message pubmsg = MQTTClient_message_initializer;
            MQTTClient_deliveryToken token;
            pubmsg.payload = resp_str;
            pubmsg.payloadlen = strlen(resp_str);
            pubmsg.qos = QOS;
            pubmsg.retained = 0;
            int rc = MQTTClient_publishMessage(client, response_topic, &pubmsg, &token);
            printf("Command response sent, rc=%d\n", rc);
            osal_kfree(resp_str);
        }
        osal_kfree(request_id);
    }

    return 1;
}

void connlost(void *context, char *cause)
{
    unused(context);
    printf("MQTT connection lost: %s\n", cause);
}

// ========== MQTT 基础操作 ==========
int mqtt_subscribe(const char *topic)
{
    printf("Subscribe: %s\n", topic);
    return MQTTClient_subscribe(client, topic, QOS);
}

// 使用 MQTT_msg 结构体发布
int mqtt_publish(const char *topic, MQTT_msg *report_msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char *msg = make_json("gateway_data",
                          report_msg->temp,
                          report_msg->humi,
                          report_msg->rain,
                          report_msg->hanger,
                          report_msg->pir);
    if (msg == NULL) {
        printf("make_json failed\n");
        return -1;
    }
    pubmsg.payload = msg;
    pubmsg.payloadlen = strlen(msg);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt_publish failed, rc=%d\n", rc);
    } else {
        printf("mqtt_publish success, payload: %s\n", msg);
    }
    osal_kfree(msg);
    return rc;
}

int mqtt_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    printf("start mqtt connect...\n");
    MQTTClient_init();
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 120;
    conn_opts.cleansession = 1;
    conn_opts.username = g_username;
    conn_opts.password = PASSWORD;
    conn_opts.connectTimeout = 30;   // 30秒超时
    MQTTClient_setCallbacks(client, NULL, connlost, msgArrved, delivered);
    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT connect failed, rc=%d\n", rc);
        return -1;
    }
    printf("MQTT connect success\n");
    return 0;
}

// ========== MQTT 任务（独立线程） ==========
int mqtt_task(void)
{
    printf("Connecting to Wi-Fi...\n");
    int wifi_ret = wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
    if (wifi_ret != 0) {
        printf("Wi-Fi connect failed, MQTT task will idle.\n");
        while (1) osal_msleep(1000);
        return 0;
    }
    printf("Wi-Fi connected.\n");

    int mqtt_ret = mqtt_connect();
    if (mqtt_ret != 0) {
        printf("MQTT connect failed, MQTT task will idle.\n");
        while (1) osal_msleep(1000);
        return 0;
    }

    char *cmd_topic = combine_strings(3, "$oc/devices/", g_username, "/sys/commands/#");
    mqtt_subscribe(cmd_topic);
    osal_kfree(cmd_topic);

    sprintf(g_report_topic, "$oc/devices/%s/sys/properties/report", g_username);
    printf("MQTT task ready.\n");

    while (1) {
        if (g_data_updated) {
            sensor_data_t local;
            memcpy(&local, &g_sensor_data, sizeof(sensor_data_t));
            g_data_updated = 0;

            MQTT_msg msg;
            msg.msg_type = EN_MSG_REPORT;
            snprintf(msg.temp, sizeof(msg.temp), "%.1f", local.temp);
            snprintf(msg.humi, sizeof(msg.humi), "%.1f", local.humi);
            snprintf(msg.rain, sizeof(msg.rain), "%d", local.rain);
            snprintf(msg.pir, sizeof(msg.pir), "%d", local.pir);
            strncpy(msg.hanger, local.hanger, sizeof(msg.hanger)-1);
            msg.hanger[sizeof(msg.hanger)-1] = '\0';

            mqtt_publish(g_report_topic, &msg);
        }
        osal_msleep(100);
    }
    return 0;
}

// ========== 对外接口 ==========
void mqtt_module_init(void)
{
    osal_kthread_lock();
    osal_task *task_handle = osal_kthread_create((osal_kthread_handler)mqtt_task, 0, "MqttTask", MQTT_STA_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, MQTT_STA_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

void mqtt_module_update_sensor_data(const sensor_data_t *data)
{
    if (data == NULL) return;
    memcpy(&g_sensor_data, data, sizeof(sensor_data_t));
    g_data_updated = 1;
}