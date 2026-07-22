/**
 * @file SLE_WiFi_Coexist_Server.c
 * @brief HI3863E 星闪窗户舵机 Server
 */
#include "securec.h"
#include "errcode.h"
#include "osal_addr.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "WiFi_SLE_Coexist_Server_adv.h"
#include "WiFi_SLE_Coexist_Server.h"
#include "window_servo.h"

#include "debug_print.h"
#include "soc_osal.h"
#include "app_init.h"
#include "common_def.h"

#define OCTET_BIT_LEN 8
#define UUID_LEN_2 2
#define WINDOW_SERVER_TASK_PRIO       24
#define WINDOW_SERVER_TASK_STACK_SIZE 0x2000

#define encode2byte_little(_ptr, data)                     \
    do {                                                   \
        *(uint8_t *)((_ptr) + 1) = (uint8_t)((data) >> 8); \
        *(uint8_t *)(_ptr) = (uint8_t)(data);              \
    } while (0)

static char g_sle_uuid_app_uuid[UUID_LEN_2] = {0x0, 0x0};
static char g_sle_property_value[OCTET_BIT_LEN] = {0};
static uint16_t g_conn_id = 0;
static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;

/* 协议回调只写事件；舵机动作由 Server 任务执行，避免阻塞星闪协议回调。 */
static volatile window_command_t g_pending_window_cmd = WINDOW_CMD_NONE;

static uint8_t sle_uuid_base[] = {0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
                                  0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void sle_uuid_set_base(sle_uuid_t *out)
{
    (void)memcpy_s(out->uuid, SLE_UUID_LEN, sle_uuid_base, SLE_UUID_LEN);
    out->len = UUID_LEN_2;
}

static void sle_uuid_set_u2(uint16_t u2, sle_uuid_t *out)
{
    sle_uuid_set_base(out);
    out->len = UUID_LEN_2;
    encode2byte_little(&out->uuid[14], u2);
}

static void window_command_write_handler(const ssaps_req_write_cb_t *request)
{
    if (request == NULL || request->length != 1 || request->value == NULL) {
        PRINT("[SLE Window Server] invalid command packet\r\n");
        return;
    }

    window_command_t command = (window_command_t)request->value[0];
    if (command != WINDOW_CMD_OPEN && command != WINDOW_CMD_CLOSE) {
        PRINT("[SLE Window Server] unknown command=0x%x\r\n", command);
        return;
    }

    /* 若运动期间又收到命令，保留最后一个命令，当前动作结束后立即执行。 */
    g_pending_window_cmd = command;
    PRINT("[SLE Window Server] queued command=0x%x\r\n", command);
}

static void ssaps_read_request_cbk(uint8_t server_id,
                                   uint16_t conn_id,
                                   ssaps_req_read_cb_t *read_para,
                                   errcode_t status)
{
    PRINT("[SLE Window Server] read server=%u conn=%u handle=%u status=0x%x\r\n",
          server_id, conn_id, read_para->handle, status);
}

static void ssaps_write_request_cbk(uint8_t server_id,
                                    uint16_t conn_id,
                                    ssaps_req_write_cb_t *write_para,
                                    errcode_t status)
{
    PRINT("[SLE Window Server] write server=%u conn=%u handle=%u len=%u status=0x%x\r\n",
          server_id, conn_id, write_para->handle, write_para->length, status);

    if (status == ERRCODE_SUCC && write_para->handle == g_property_handle) {
        window_command_write_handler(write_para);
    }
}

static void ssaps_mtu_changed_cbk(uint8_t server_id,
                                  uint16_t conn_id,
                                  ssap_exchange_info_t *mtu_size,
                                  errcode_t status)
{
    PRINT("[SLE Window Server] MTU server=%u conn=%u mtu=%u status=0x%x\r\n",
          server_id, conn_id, mtu_size->mtu_size, status);
}

static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    PRINT("[SLE Window Server] service start server=%u handle=%u status=0x%x\r\n",
          server_id, handle, status);
}

static errcode_t register_ssaps_callbacks(void)
{
    ssaps_callbacks_t callbacks = {0};
    callbacks.start_service_cb = ssaps_start_service_cbk;
    callbacks.mtu_changed_cb = ssaps_mtu_changed_cbk;
    callbacks.read_request_cb = ssaps_read_request_cbk;
    callbacks.write_request_cb = ssaps_write_request_cbk;
    return ssaps_register_callbacks(&callbacks);
}

static errcode_t add_service(void)
{
    sle_uuid_t service_uuid = {0};
    sle_uuid_set_u2(SLE_UUID_SERVER_SERVICE, &service_uuid);
    return ssaps_add_service_sync(g_server_id, &service_uuid, true, &g_service_handle);
}

static errcode_t add_property(void)
{
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t descriptor_value[] = {0x01, 0x00};

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    sle_uuid_set_u2(SLE_UUID_SERVER_PROPERTY, &property.uuid);
    property.value = osal_vmalloc(sizeof(g_sle_property_value));
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                                  SSAP_OPERATE_INDICATION_BIT_WRITE;
    if (property.value == NULL) {
        return ERRCODE_MALLOC;
    }
    if (memcpy_s(property.value, sizeof(g_sle_property_value),
                 g_sle_property_value, sizeof(g_sle_property_value)) != EOK) {
        osal_vfree(property.value);
        return ERRCODE_MEMCPY;
    }

    errcode_t ret = ssaps_add_property_sync(g_server_id, g_service_handle,
                                            &property, &g_property_handle);
    if (ret != ERRCODE_SUCC) {
        osal_vfree(property.value);
        return ret;
    }

    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                                    SSAP_OPERATE_INDICATION_BIT_WRITE;
    descriptor.type = SSAP_DESCRIPTOR_USER_DESCRIPTION;
    descriptor.value = descriptor_value;
    descriptor.value_len = sizeof(descriptor_value);

    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle,
                                    g_property_handle, &descriptor);
    osal_vfree(property.value);
    return ret;
}

static errcode_t add_and_start_server(void)
{
    sle_uuid_t app_uuid = {0};
    app_uuid.len = sizeof(g_sle_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, app_uuid.len,
                 g_sle_uuid_app_uuid, sizeof(g_sle_uuid_app_uuid)) != EOK) {
        return ERRCODE_MEMCPY;
    }

    errcode_t ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = add_service();
    if (ret != ERRCODE_SUCC) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }
    ret = add_property();
    if (ret != ERRCODE_SUCC) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }
    return ssaps_start_service(g_server_id, g_service_handle);
}

static void connect_state_changed_cbk(uint16_t conn_id,
                                      const sle_addr_t *addr,
                                      sle_acb_state_t conn_state,
                                      sle_pair_state_t pair_state,
                                      sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    unused(disc_reason);
    g_conn_id = conn_id;
    PRINT("[SLE Window Server] connection state=%u conn=%u\r\n", conn_state, conn_id);
}

static void pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(addr);
    PRINT("[SLE Window Server] pair conn=%u status=0x%x\r\n", conn_id, status);
}

static errcode_t register_connection_callbacks(void)
{
    sle_connection_callbacks_t callbacks = {0};
    callbacks.connect_state_changed_cb = connect_state_changed_cbk;
    callbacks.pair_complete_cb = pair_complete_cbk;
    return sle_connection_register_callbacks(&callbacks);
}

static void execute_pending_window_command(void)
{
    window_command_t command = g_pending_window_cmd;
    if (command == WINDOW_CMD_NONE) {
        return;
    }
    g_pending_window_cmd = WINDOW_CMD_NONE;

    if (command == WINDOW_CMD_OPEN) {
        PRINT("[SLE Window Server] opening window\r\n");
        Window_Open_90();
        PRINT("[SLE Window Server] window opened\r\n");
    } else if (command == WINDOW_CMD_CLOSE) {
        PRINT("[SLE Window Server] closing window\r\n");
        Window_Close_0();
        PRINT("[SLE Window Server] window closed\r\n");
    }
}

static int sle_window_server_task(const char *arg)
{
    unused(arg);

    /* 窗户舵机只在 Server 初始化和输出 PWM。 */
    Window_Servo_Init();
    Window_Reset_Close();
    PRINT("[SLE Window Server] servo reset to closed position\r\n");

    osal_msleep(5000);
    if (enable_sle() != ERRCODE_SUCC) {
        PRINT("[SLE Window Server] enable SLE failed\r\n");
        return -1;
    }
    if (register_connection_callbacks() != ERRCODE_SUCC) {
        PRINT("[SLE Window Server] connection callback registration failed\r\n");
        return -1;
    }
    if (register_ssaps_callbacks() != ERRCODE_SUCC) {
        PRINT("[SLE Window Server] SSAPS callback registration failed\r\n");
        return -1;
    }
    if (add_and_start_server() != ERRCODE_SUCC) {
        PRINT("[SLE Window Server] service creation failed\r\n");
        return -1;
    }
    if (example_sle_server_adv_init() != ERRCODE_SUCC) {
        PRINT("[SLE Window Server] advertising initialization failed\r\n");
        return -1;
    }

    PRINT("[SLE Window Server] ready\r\n");
    while (1) {
        execute_pending_window_command();
        osal_msleep(20);
    }
    return 0;
}

static void sle_window_server_entry(void)
{
    osal_task *task = NULL;
    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)sle_window_server_task,
                               NULL,
                               "SLEWindowServer",
                               WINDOW_SERVER_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, WINDOW_SERVER_TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
}

app_run(sle_window_server_entry);
