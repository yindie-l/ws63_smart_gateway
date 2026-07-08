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

extern int MQTTClient_init(void);
// 引用 main.c 中定义的全局状态变量（用于控制舵机/窗户）
#include "../main/main.h"
#include <osal_wait.h>

// ============ 华为云连接参数 ============
#define ADDRESS "tcp://"
#define CLIENTID ""
#define USERNAME ""
#define PASSWORD ""

#define CONFIG_WIFI_SSID ""
#define CONFIG_WIFI_PWD ""
#define QOS 1
#define MSG_MAX_LEN 128
#define MSG_QUEUE_SIZE 32

// 静态变量（模块内部使用）
static unsigned long g_msg_queue;
volatile MQTTClient_deliveryToken deliveredtoken;
MQTTClient client;
static char g_report_topic[128];

// 外部变量声明（定义在 main.c）
extern volatile int g_servoRun;
extern volatile int g_servoActionDone;
extern volatile int g_windowState;
extern volatile uint32_t g_window_remains;
extern volatile unsigned int g_window_pulse_duty;
extern char g_hanger_status[20];

// ========== 回调函数 ==========
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    // printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredtoken = dt;
}

// 命令到达回调（直接处理）
int msgArrved(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);
    printf("=== msgArrved CALLED ===\n");
    printf("topic: %s, payload: %s\n", topic_name, (char *)message->payload);

    // 1. 提取 request_id（用于响应）
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

    // 2. 解析命令（target / action）
    char target[20] = {0};
    char action[20] = {0};
    int parse_ret = parse_command_json(message->payload, target, sizeof(target), action, sizeof(action));

    if (parse_ret == 0) {
        printf("Recv CMD: target=%s, action=%s\n", target, action);

        // ====== 控制衣架 ======
        if (strcmp(target, "hanger") == 0) {
            if (strcmp(action, "extend") == 0) {
                strcpy(g_hanger_status, "extended");
                g_servoRun = 2;
                g_servoActionDone = 0;
                printf(">>> 衣架执行伸出\n");
            } else if (strcmp(action, "retract") == 0) {
                strcpy(g_hanger_status, "retracted");
                g_servoRun = 1;
                g_servoActionDone = 0;
                printf(">>> 衣架执行收回\n");
            }
        }
        // ====== 控制窗户 ======
        else if (strcmp(target, "window") == 0) {
            if (strcmp(action, "extend") == 0) {
                g_window_pulse_duty = WINDOW_SPEED_OPEN_SLOW;
                g_window_remains = CYCLES_FOR_90_DEGREE;
                g_windowState = 1;
                printf(">>> 窗户执行打开\n");
            } else if (strcmp(action, "retract") == 0) {
                g_window_pulse_duty = WINDOW_SPEED_CLOSE_SLOW;
                g_window_remains = CYCLES_FOR_90_DEGREE;
                g_windowState = -1;
                printf(">>> 窗户执行关闭\n");
            }
        }
    }

    // 3. 发送命令响应（带 request_id）
    if (request_id != NULL) {
        char response_topic[256];
        sprintf(response_topic, "$oc/devices/%s/sys/commands/response/request_id=%s", USERNAME, request_id);
        printf("=== DEBUG: response_topic = [%s] ===\n", response_topic);

        char *resp_str = build_command_response(0, "success");
        if (resp_str != NULL) {
            MQTTClient_message pubmsg = MQTTClient_message_initializer;
            MQTTClient_deliveryToken token;
            pubmsg.payload = resp_str;
            pubmsg.payloadlen = strlen(resp_str);
            pubmsg.qos = QOS;
            pubmsg.retained = 0;
            int rc = MQTTClient_publishMessage(client, response_topic, &pubmsg, &token);
            printf("Command response sent, rc=%d, topic=%s\n", rc, response_topic);
            osal_kfree(resp_str);
        }
        osal_kfree(request_id);
    }

    return 1;
}

void connlost(void *context, char *cause)
{
    unused(context);
    printf("mqtt_connection_lost() error, cause: %s\n", cause);
}

// ========== MQTT 基础操作 ==========
int mqtt_subscribe(const char *topic)
{
    printf("subscribe start: %s\r\n", topic);
    return MQTTClient_subscribe(client, topic, QOS);
}

// 发布函数
int mqtt_publish(const char *topic, MQTT_msg *report_msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char *msg = make_json("gateway_data",
                          report_msg->temp,
                          report_msg->humi,
                          report_msg->rain,
                          report_msg->hanger,
                          report_msg->pir,
                          report_msg->light);
    if (msg == NULL) {
        printf("make_json failed\r\n");
        return -1;
    }
    pubmsg.payload = msg;
    pubmsg.payloadlen = (int)strlen(msg);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt_publish failed, rc=%d\r\n", rc);
    }
    printf("mqtt_publish(), payload: %s\r\n", msg);
    osal_kfree(msg);
    return rc;
}

int mqtt_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    printf("start mqtt connect...\r\n");
    MQTTClient_init();
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 120;
    conn_opts.cleansession = 1;
    conn_opts.username = USERNAME;
    conn_opts.password = PASSWORD;
    conn_opts.connectTimeout = 30;
    MQTTClient_setCallbacks(client, NULL, connlost, msgArrved, delivered);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return -1;
    }
    printf("connect success\r\n");
    return rc;
}

// ========== 对外接口函数 ==========

/**
 * 初始化 MQTT 模块（在主任务中直接调用，不创建独立任务）
 * 返回：0-成功，-1-失败
 */
int mqtt_module_init_direct(void)
{
    // 创建消息队列
    uint32_t ret;
    ret = osal_msg_queue_create("mqtt_queue", MSG_QUEUE_SIZE, &g_msg_queue, 0, MSG_MAX_LEN);
    if (ret != OSAL_SUCCESS) {
        printf("create queue failure! error:%x\n", ret);
        return -1;
    }
    printf("create queue success! queue_id = %d\n", g_msg_queue);

    printf("Connecting to Wi-Fi...\n");
    int wifi_ret = wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
    if (wifi_ret != 0) {
        printf("Wi-Fi connect failed\n");
        return -1;
    }
    printf("Wi-Fi connected.\n");

    int mqtt_ret = mqtt_connect();
    if (mqtt_ret != 0) {
        printf("MQTT connect failed\n");
        return -1;
    }

    char *cmd_topic = combine_strings(3, "$oc/devices/", USERNAME, "/sys/commands/#");
    mqtt_subscribe(cmd_topic);
    osal_kfree(cmd_topic);

    sprintf(g_report_topic, "$oc/devices/%s/sys/properties/report", USERNAME);

    printf("MQTT initialized successfully.\n");
    return 0;
}

/**
 * @brief 上报传感器数据
 */
void mqtt_module_send_report(float temp, float humi, uint8_t rain, uint8_t pir, const char *hanger)
{
    // 如果 MQTT 未连接，不执行上报
    if (!MQTTClient_isConnected(client)) {
        return;
    }

    MQTT_msg mqtt_msg;
    mqtt_msg.msg_type = EN_MSG_REPORT;
    snprintf(mqtt_msg.temp, sizeof(mqtt_msg.temp), "%.1f", temp);
    snprintf(mqtt_msg.humi, sizeof(mqtt_msg.humi), "%.1f", humi);
    snprintf(mqtt_msg.rain, sizeof(mqtt_msg.rain), "%d", rain);
    snprintf(mqtt_msg.rain, sizeof(mqtt_msg.rain), "%d", rain);
    snprintf(mqtt_msg.pir, sizeof(mqtt_msg.pir), "%d", pir);
    strncpy(mqtt_msg.hanger, hanger, sizeof(mqtt_msg.hanger) - 1);
    mqtt_msg.hanger[sizeof(mqtt_msg.hanger) - 1] = '\0';
    strcpy(mqtt_msg.light, "326lux");

    mqtt_publish(g_report_topic, &mqtt_msg);
}