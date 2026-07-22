#ifndef SLE_WINDOW_PROTOCOL_H
#define SLE_WINDOW_PROTOCOL_H

#include <stdint.h>

/* Client -> Server：单字节窗户控制命令 */
typedef enum {
    WINDOW_CMD_NONE  = 0x00,
    WINDOW_CMD_OPEN  = 0x01,
    WINDOW_CMD_CLOSE = 0x02
} window_command_t;

/* 星闪服务和属性 UUID，Client 与 Server 必须保持一致 */
#define SLE_UUID_WINDOW_SERVICE   0xABCD
#define SLE_UUID_WINDOW_PROPERTY  0x3344

#endif /* SLE_WINDOW_PROTOCOL_H */
