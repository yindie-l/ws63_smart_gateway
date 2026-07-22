/**
 * @file SLE_WiFi_Coexist_Client.c
 * @brief HI3863E 星闪窗户控制 Client
 * @details 扫描固定地址的 Server，连接/配对后发送单字节开窗或关窗命令。
 */
#include "securec.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"
#include "common_def.h"
#include "debug_print.h"
#include "sle_window_client.h"

#define SLE_MTU_SIZE_DEFAULT          300
#define SLE_SEEK_INTERVAL_DEFAULT     100
#define SLE_SEEK_WINDOW_DEFAULT       100
#define SLE_CLIENT_ID                 0
#define SLE_CLIENT_TASK_PRIO          24
#define SLE_CLIENT_TASK_STACK_SIZE    0x2000
#define SLE_WINDOW_TX_TASK_PRIO       18
#define SLE_WINDOW_TX_STACK_SIZE      0x1000

/*
 * 0：调试/单 Server 环境，连接扫描到的第一个设备。
 * 1：仅连接 g_sle_expected_addr 指定的固定地址。
 * 首次联调建议保持为 0，确认链路后再改为 1。
 */
#define SLE_WINDOW_USE_FIXED_ADDR      0

static sle_announce_seek_callbacks_t g_seek_cbk = {0};
static sle_connection_callbacks_t g_connect_cbk = {0};
static ssapc_callbacks_t g_ssapc_cbk = {0};
static sle_addr_t g_remote_addr = {0};

static volatile uint16_t g_conn_id = 0;
static volatile uint16_t g_property_handle = 0;
static volatile uint8_t g_sle_ready = 0;
static volatile uint8_t g_connecting = 0;
static volatile window_command_t g_pending_cmd = WINDOW_CMD_NONE;
static volatile window_command_t g_inflight_cmd = WINDOW_CMD_NONE;

/* 与 Server 广播文件中的 g_sle_local_addr 保持一致。 */

#if SLE_WINDOW_USE_FIXED_ADDR
static uint8_t g_sle_expected_addr[SLE_ADDR_LEN] = {
    0x02, 0x01, 0x06, 0x08, 0x06, 0x03
};
#endif

static void sle_window_start_scan(void);

__attribute__((weak)) void SleWindow_OnCommandResult(window_command_t command, errcode_t status)
{
    unused(command);
    unused(status);
}

uint8_t SleWindow_IsReady(void)
{
    return g_sle_ready;
}

errcode_t SleWindow_Request(window_command_t command)
{
    if (command != WINDOW_CMD_OPEN && command != WINDOW_CMD_CLOSE) {
        return ERRCODE_FAIL;
    }

    /* 未连接时不修改主系统的窗户状态，雨天逻辑可以在后续周期继续重试。 */
    if (g_sle_ready == 0 || g_property_handle == 0) {
        return ERRCODE_FAIL;
    }

    /* 单生产者/单消费者，单字节写入在本平台上为原子操作；采用“最后命令优先”。 */
    g_pending_cmd = command;
    return ERRCODE_SUCC;
}

static errcode_t sle_window_send_command(window_command_t command)
{
    uint8_t data = (uint8_t)command;
    ssapc_write_param_t param = {0};

    if (g_sle_ready == 0 || g_property_handle == 0) {
        return ERRCODE_FAIL;
    }

    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = 1;
    param.data = &data;
    return ssapc_write_req(SLE_CLIENT_ID, g_conn_id, &param);
}

static int sle_window_tx_task(const char *arg)
{
    unused(arg);

    while (1) {
        if (g_sle_ready != 0 && g_inflight_cmd == WINDOW_CMD_NONE && g_pending_cmd != WINDOW_CMD_NONE) {
            window_command_t command = g_pending_cmd;
            g_pending_cmd = WINDOW_CMD_NONE;
            g_inflight_cmd = command;

            errcode_t ret = sle_window_send_command(command);
            if (ret != ERRCODE_SUCC) {
                /* 立即发送失败时保留命令，待下一周期或重连后重试。 */
                g_inflight_cmd = WINDOW_CMD_NONE;
                g_pending_cmd = command;
            }
        }
        osal_msleep(20);
    }
    return 0;
}

static void sle_window_enable_cbk(errcode_t status)
{
    if (status == ERRCODE_SUCC) {
        sle_window_start_scan();
    }
}

static void sle_window_seek_enable_cbk(errcode_t status)
{
    unused(status);
}

static void sle_window_seek_disable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC || g_connecting == 0) {
        return;
    }

    errcode_t ret = sle_connect_remote_device(&g_remote_addr);
    if (ret != ERRCODE_SUCC) {
        PRINT("[SLE Window Client] connect request failed, ret=0x%x\r\n", ret);
        g_connecting = 0;
        sle_window_start_scan();
    }
}

static void sle_window_seek_result_cbk(sle_seek_result_info_t *result)
{
    if (result == NULL || g_connecting != 0 || g_sle_ready != 0) {
        return;
    }

    PRINT("[SLE Window Client] scan addr=%02x:%02x:%02x:%02x:%02x:%02x type=%u\r\n",
          result->addr.addr[0], result->addr.addr[1], result->addr.addr[2],
          result->addr.addr[3], result->addr.addr[4], result->addr.addr[5],
          result->addr.type);

#if SLE_WINDOW_USE_FIXED_ADDR
    if (memcmp(result->addr.addr, g_sle_expected_addr, SLE_ADDR_LEN) != 0) {
        return;
    }
#endif

    if (memcpy_s(&g_remote_addr, sizeof(g_remote_addr),
                 &result->addr, sizeof(result->addr)) != EOK) {
        return;
    }

    g_connecting = 1;
    PRINT("[SLE Window Client] selected server, stop scanning\r\n");
    (void)sle_stop_seek();
}

static void sle_window_register_seek_cbks(void)
{
    g_seek_cbk.sle_enable_cb = sle_window_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_window_seek_enable_cbk;
    g_seek_cbk.seek_disable_cb = sle_window_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_window_seek_result_cbk;
}

static void sle_window_connect_state_cbk(uint16_t conn_id,
                                         const sle_addr_t *addr,
                                         sle_acb_state_t conn_state,
                                         sle_pair_state_t pair_state,
                                         sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(disc_reason);

    PRINT("[SLE Window Client] connection state=%u, conn_id=%u\r\n", conn_state, conn_id);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_connecting = 1;
        g_conn_id = conn_id;
        if (pair_state == SLE_PAIR_NONE) {
            (void)sle_pair_remote_device(&g_remote_addr);
        }
    } else {
        /* 断线后撤销就绪状态，并将未确认命令重新排队。 */
        g_connecting = 0;
        g_sle_ready = 0;
        g_property_handle = 0;
        if (g_inflight_cmd != WINDOW_CMD_NONE) {
            g_pending_cmd = g_inflight_cmd;
            g_inflight_cmd = WINDOW_CMD_NONE;
        }
        sle_window_start_scan();
    }
}

static void sle_window_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    unused(addr);
    PRINT("[SLE Window Client] pair complete status=0x%x\r\n", status);

    if (status == ERRCODE_SUCC) {
        ssap_exchange_info_t info = {0};
        info.mtu_size = SLE_MTU_SIZE_DEFAULT;
        info.version = 1;
        (void)ssapc_exchange_info_req(SLE_CLIENT_ID, conn_id, &info);
    }
}

static void sle_window_register_connect_cbks(void)
{
    g_connect_cbk.connect_state_changed_cb = sle_window_connect_state_cbk;
    g_connect_cbk.pair_complete_cb = sle_window_pair_complete_cbk;
}

static void sle_window_exchange_info_cbk(uint8_t client_id,
                                         uint16_t conn_id,
                                         ssap_exchange_info_t *param,
                                         errcode_t status)
{
    unused(client_id);
    unused(param);

    PRINT("[SLE Window Client] MTU exchange status=0x%x\r\n", status);
    if (status == ERRCODE_SUCC) {
        ssapc_find_structure_param_t find_param = {0};
        find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
        find_param.start_hdl = 1;
        find_param.end_hdl = 0xFFFF;
        (void)ssapc_find_structure(SLE_CLIENT_ID, conn_id, &find_param);
    }
}

static void sle_window_find_structure_cbk(uint8_t client_id,
                                          uint16_t conn_id,
                                          ssapc_find_service_result_t *service,
                                          errcode_t status)
{
    unused(client_id);
    unused(conn_id);

    if (status != ERRCODE_SUCC || service == NULL) {
        return;
    }

    /* 当前 Server 仅注册一个服务和一个属性，属性句柄紧跟服务句柄。 */
    g_property_handle = service->start_hdl + 1;
    g_sle_ready = 1;
    PRINT("[SLE Window Client] ready, property handle=0x%x\r\n", g_property_handle);
}

static void sle_window_find_structure_cmp_cbk(uint8_t client_id,
                                              uint16_t conn_id,
                                              ssapc_find_structure_result_t *result,
                                              errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(result);
    unused(status);
}

static void sle_window_write_cfm_cbk(uint8_t client_id,
                                     uint16_t conn_id,
                                     ssapc_write_result_t *write_result,
                                     errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(write_result);

    window_command_t command = g_inflight_cmd;
    g_inflight_cmd = WINDOW_CMD_NONE;

    PRINT("[SLE Window Client] write confirm cmd=0x%x status=0x%x\r\n", command, status);
    if (command != WINDOW_CMD_NONE) {
        SleWindow_OnCommandResult(command, status);
    }
}

static void sle_window_read_cfm_cbk(uint8_t client_id,
                                    uint16_t conn_id,
                                    ssapc_handle_value_t *read_data,
                                    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(read_data);
    unused(status);
}

static void sle_window_register_ssapc_cbks(void)
{
    g_ssapc_cbk.exchange_info_cb = sle_window_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_window_find_structure_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_window_find_structure_cmp_cbk;
    g_ssapc_cbk.write_cfm_cb = sle_window_write_cfm_cbk;
    g_ssapc_cbk.read_cfm_cb = sle_window_read_cfm_cbk;
}

static void sle_window_start_scan(void)
{
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 0;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    (void)sle_set_seek_param(&param);
    (void)sle_start_seek();
}

static int sle_window_client_task(const char *arg)
{
    unused(arg);
    osal_msleep(5000);

    sle_window_register_seek_cbks();
    sle_window_register_connect_cbks();
    sle_window_register_ssapc_cbks();

    if (sle_announce_seek_register_callbacks(&g_seek_cbk) != ERRCODE_SUCC) {
        PRINT("[SLE Window Client] register seek callback failed\r\n");
        return -1;
    }
    if (sle_connection_register_callbacks(&g_connect_cbk) != ERRCODE_SUCC) {
        PRINT("[SLE Window Client] register connection callback failed\r\n");
        return -1;
    }
    if (ssapc_register_callbacks(&g_ssapc_cbk) != ERRCODE_SUCC) {
        PRINT("[SLE Window Client] register SSAPC callback failed\r\n");
        return -1;
    }
    if (enable_sle() != ERRCODE_SUCC) {
        PRINT("[SLE Window Client] enable SLE failed\r\n");
        return -1;
    }

    PRINT("[SLE Window Client] initialization started\r\n");
    return 0;
}

errcode_t SleWindow_Client_Start(void)
{
    osal_task *sle_task = osal_kthread_create((osal_kthread_handler)sle_window_client_task,
                                              NULL,
                                              "SLEWindowClient",
                                              SLE_CLIENT_TASK_STACK_SIZE);
    if (sle_task == NULL) {
        return ERRCODE_FAIL;
    }
    osal_kthread_set_priority(sle_task, SLE_CLIENT_TASK_PRIO);
    osal_kfree(sle_task);

    osal_task *tx_task = osal_kthread_create((osal_kthread_handler)sle_window_tx_task,
                                             NULL,
                                             "SLEWindowTx",
                                             SLE_WINDOW_TX_STACK_SIZE);
    if (tx_task == NULL) {
        return ERRCODE_FAIL;
    }
    osal_kthread_set_priority(tx_task, SLE_WINDOW_TX_TASK_PRIO);
    osal_kfree(tx_task);
    return ERRCODE_SUCC;
}
