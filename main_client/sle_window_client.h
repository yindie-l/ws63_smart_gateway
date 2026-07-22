#ifndef SLE_WINDOW_CLIENT_H
#define SLE_WINDOW_CLIENT_H

#include "errcode.h"
#include "sle_window_protocol.h"

/* 在 main_entry() 创建其他任务时调用，启动星闪 Client 和发送任务。 */
errcode_t SleWindow_Client_Start(void);

/*
 * 请求远程窗户动作。
 * 仅在星闪连接和属性发现完成后返回 ERRCODE_SUCC；否则返回 ERRCODE_FAIL。
 */
errcode_t SleWindow_Request(window_command_t command);

/* 当前是否已经完成连接、配对和属性发现。 */
uint8_t SleWindow_IsReady(void);

/*
 * 命令写确认回调。sle_window_client.c 提供弱实现，main.c 可实现同名函数。
 * 注意：成功表示 Server 已收到写请求，不等价于机械舵机已经转动完毕。
 */
void SleWindow_OnCommandResult(window_command_t command, errcode_t status);

#endif /* SLE_WINDOW_CLIENT_H */
