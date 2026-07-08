// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdarg.h>
// #include "cJSON.h"

// int string_length(char *str)
// {
//     if (str == NULL) {
//         return 0;
//     }
//     int len = 0;
//     char *temp_str = str;
//     while (*temp_str++ != '\0') {
//         len++;
//     }
//     return len;
// }

// char *combine_strings(int str_amount, char *str1, ...)
// {
//     int length = string_length(str1) + 1;
//     if (length == 1) {
//         return NULL; // 如果第一个字符串为空
//     }

//     char *result = malloc(length);
//     if (result == NULL) {
//         return NULL; // 内存分配失败
//     }

//     strcpy(result, str1); // 复制第一个字符串

//     va_list args;
//     va_start(args, str1);

//     char *tem_str;
//     while (--str_amount > 0) {
//         tem_str = va_arg(args, char *);
//         if (tem_str == NULL) {
//             continue; // 跳过空字符串
//         }
//         length += string_length(tem_str);
//         result = realloc(result, length);
//         if (result == NULL) {
//             return NULL; // 内存重新分配失败
//         }
//         strcat(result, tem_str); // 拼接字符串
//     }
//     va_end(args);

//     return result; // 返回拼接后的字符串
// }

// /**
//  * 构造华为云属性上报 JSON（5 个字段）
//  * service_id: 必须为 "gateway_data"
//  * temp, humidity, rain, hanger, pir: 字符串类型
//  */
// char *make_json(char *service_id, char *temp, char *humidity, char *rain, char *hanger, char *pir)
// {
//     // 创建根对象
//     cJSON *root = cJSON_CreateObject();
//     // 创建 services 数组
//     cJSON *services = cJSON_CreateArray();
//     // 创建 service 对象
//     cJSON *service = cJSON_CreateObject();
//     cJSON_AddStringToObject(service, "service_id", service_id);

//     // 创建 properties 对象
//     cJSON *properties = cJSON_CreateObject();
//     cJSON_AddStringToObject(properties, "temp", temp);
//     cJSON_AddStringToObject(properties, "humidity", humidity);
//     cJSON_AddStringToObject(properties, "rain", rain);
//     cJSON_AddStringToObject(properties, "hanger", hanger);
//     cJSON_AddStringToObject(properties, "pir", pir);

//     // 将 properties 添加到 service
//     cJSON_AddItemToObject(service, "properties", properties);

//     // 将 service 添加到 services 数组
//     cJSON_AddItemToArray(services, service);

//     // 将 services 添加到 root
//     cJSON_AddItemToObject(root, "services", services);

//     // 打印 JSON 字符串
//     char *json_string = cJSON_Print(root);
//     // 释放内存
//     cJSON_Delete(root);
//     return json_string;
// }

// /**
//  * 解析下行控制命令（示例中解析 beep 命令，可按需修改）
//  * 返回 beep 的值（"ON" 或 "OFF"），调用者需 free 返回值
//  */
// char *parse_json(char *json_string)
// {
//     char *string = NULL;
//     cJSON *root = cJSON_Parse(json_string);
//     if (root == NULL) {
//         printf("Error parsing JSON\n");
//         return NULL;
//     }
//     // 获取 paras 对象中的 beep 项（根据实际命令格式调整）
//     cJSON *paras = cJSON_GetObjectItem(root, "paras");
//     if (paras == NULL) {
//         // 如果没有 "paras"，尝试直接获取 "beep"（兼容不同格式）
//         cJSON *beep = cJSON_GetObjectItem(root, "beep");
//         if (beep && cJSON_IsString(beep)) {
//             string = strdup(beep->valuestring);
//         }
//     } else {
//         cJSON *beep = cJSON_GetObjectItem(paras, "beep");
//         if (beep && cJSON_IsString(beep)) {
//             string = strdup(beep->valuestring);
//         }
//     }
//     cJSON_Delete(root);
//     return string;
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cJSON.h"

int string_length(char *str)
{
    if (str == NULL) {
        return 0;
    }
    int len = 0;
    char *temp_str = str;
    while (*temp_str++ != '\0') {
        len++;
    }
    return len;
}

char *combine_strings(int str_amount, char *str1, ...)
{
    int length = string_length(str1) + 1;
    if (length == 1) {
        return NULL;
    }

    char *result = malloc(length);
    if (result == NULL) {
        return NULL;
    }

    strcpy(result, str1);

    va_list args;
    va_start(args, str1);

    char *tem_str;
    while (--str_amount > 0) {
        tem_str = va_arg(args, char *);
        if (tem_str == NULL) {
            continue;
        }
        length += string_length(tem_str);
        result = realloc(result, length);
        if (result == NULL) {
            return NULL;
        }
        strcat(result, tem_str);
    }
    va_end(args);

    return result;
}

/**
 * 构造华为云属性上报 JSON（5 个字段）
 */
char *make_json(char *service_id, char *temp, char *humidity, char *rain, char *hanger, char *pir,char *light)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *services = cJSON_CreateArray();
    cJSON *service = cJSON_CreateObject();
    cJSON_AddStringToObject(service, "service_id", service_id);

    cJSON *properties = cJSON_CreateObject();
    cJSON_AddStringToObject(properties, "temp", temp);
    cJSON_AddStringToObject(properties, "humidity", humidity);
    cJSON_AddStringToObject(properties, "rain", rain);
    cJSON_AddStringToObject(properties, "hanger", hanger);
    cJSON_AddStringToObject(properties, "pir", pir);
    cJSON_AddStringToObject(properties, "light", light);

    cJSON_AddItemToObject(service, "properties", properties);
    cJSON_AddItemToArray(services, service);
    cJSON_AddItemToObject(root, "services", services);

    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

/**
 * 解析下行控制命令（beep 命令，保留兼容）
 */
char *parse_json(char *json_string)
{
    char *string = NULL;
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Error parsing JSON\n");
        return NULL;
    }
    cJSON *paras = cJSON_GetObjectItem(root, "paras");
    if (paras == NULL) {
        cJSON *beep = cJSON_GetObjectItem(root, "beep");
        if (beep && cJSON_IsString(beep)) {
            string = strdup(beep->valuestring);
        }
    } else {
        cJSON *beep = cJSON_GetObjectItem(paras, "beep");
        if (beep && cJSON_IsString(beep)) {
            string = strdup(beep->valuestring);
        }
    }
    cJSON_Delete(root);
    return string;
}

/**
 * 解析下行控制命令（支持两种格式）
 *   格式1: {"target":"hanger","action":"extend"}
 *   格式2: {"paras":{"target":"hanger","action":"extend"},"service_id":"...","command_name":"..."}
 * 返回: 0=成功, -1=失败
 */
int parse_command_json(char *json_string, char *target, int target_len, char *action, int action_len)
{
    if (json_string == NULL || target == NULL || action == NULL) {
        return -1;
    }
    
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("parse_command_json: JSON parse failed\n");
        return -1;
    }
    
    cJSON *target_obj = NULL;
    cJSON *action_obj = NULL;
    
    // 先尝试直接从根节点取 target/action
    target_obj = cJSON_GetObjectItem(root, "target");
    action_obj = cJSON_GetObjectItem(root, "action");
    
    // 如果根节点没有，从 paras 中取
    if (target_obj == NULL || action_obj == NULL) {
        cJSON *paras = cJSON_GetObjectItem(root, "paras");
        if (paras != NULL) {
            target_obj = cJSON_GetObjectItem(paras, "target");
            action_obj = cJSON_GetObjectItem(paras, "action");
        }
    }
    
    if (target_obj && cJSON_IsString(target_obj) && action_obj && cJSON_IsString(action_obj)) {
        strncpy(target, target_obj->valuestring, target_len - 1);
        target[target_len - 1] = '\0';
        strncpy(action, action_obj->valuestring, action_len - 1);
        action[action_len - 1] = '\0';
        cJSON_Delete(root);
        return 0;
    }
    
    printf("parse_command_json: target or action not found\n");
    cJSON_Delete(root);
    return -1;
}
/**
 * 构造命令响应 JSON
 * 示例：{"result_code": 0, "response": {"status": "success"}}
 */
char *build_command_response(int result_code, char *status_msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "result_code", result_code);
    
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "status", status_msg);
    cJSON_AddItemToObject(root, "response", resp_data);
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}