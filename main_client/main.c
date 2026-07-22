/**
 * @file main.c
 * @brief 全屋智能晾衣架系统主程序
 * @details 基于 HiSilicon 平台操作系统，采用多线程互斥架构，实现了包含：
 *          1. 晾衣架/窗户双舵机平滑控制与紧急刹车
 *          2. AHT20 温湿度与 BH1750 光强分时分片采集 (目前已停用，固定数值为20)
 *          3. 雨滴检测与红外（PIR）安防触发响应
 *          4. OLED 多页面动态刷新、LED 状态指示与蜂鸣器报警
 */
#include "main.h"
#include "define.h"
/* ---------------------------------- 外设模块头文件 ---------------------------------- */
#include "ssd1306_fonts.h"    // OLED 字符集与字模定义
#include "ssd1306.h"          // OLED 屏幕底层驱动
#include "aht20.h"            // AHT20 温湿度传感器驱动 (保留头文件以防外部依赖)
#include "bh1750.h"           // BH1750 环境光强传感器 I2C 驱动 (保留头文件以防外部依赖)
#include "servo.h"            // 晾衣架 180° 舵机驱动
#include "beep.h"             // 蜂鸣器驱动
#include "sle_window_client.h"// 星闪远程窗户控制 Client

/* ==============================================================================
 * 1. 全局资源与跨线程共享数据管理
 * ============================================================================== */
/* 初始化全局共享数据默认值：将温度、湿度、光强默认直接绑定为 20 */
static SystemSharedData_t g_sys_data = {
    .env_temp               = 20.0f,  // 【修改】：默认温度定死为 20.0℃
    .env_humi               = 20.0f,  // 【修改】：默认湿度定死为 20.0%
    .env_lux                = 20,     // 【修改】：默认光强定死为 20 lux
    .rack_state             = 0,
    .window_state           = 0,
    .rain_alert             = 0,
    .buzzer_req             = 0,
    .rack_interrupted_angle = 60.0f,
    .button_print_flag      = 0,
    .pir_print_flag         = 0,
    .child_lock_enable      = 0
};

static osal_mutex g_data_mutex; // 全局互斥锁句柄

/* 互斥锁内联操作封装，简化代码表达 */
static inline void SysData_Lock(void)   { osal_mutex_lock(&g_data_mutex); }
static inline void SysData_Unlock(void) { osal_mutex_unlock(&g_data_mutex); }

/* 跨模块高频同步变量（由于属于裸机实时处理，使用 volatile 防止编译器优化） */
volatile int g_emergency_stop_flag = 0;  // 紧急刹车信号: 1=通知底层平滑运动立即停止
volatile int g_current_servo_angle = 60; // 记录当前晾衣架实际物理角度（默认60°室内安全缩回态）
static volatile uint32_t s_last_button_time = 0; // 记录上一次有效按键中断的时间毫秒戳
volatile uint8_t g_isr_button_event = 0; // 按键触发事件标志
volatile uint8_t g_isr_pir_event    = 0; // 红外触发事件标志

/* ------------------- 中断服务程序（ISR）与防抖处理 ------------------- */
/**
 * @brief 外部按键 GPIO 中断回调处理函数
 * @details 负责处理手动控制晾衣架伸缩的状态机流转，支持执行过程中的重复按键中断操作
 */
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin); UNUSED(param);
    
    /* 1. 软件防抖：400ms 内的重复触发直接忽略 (中断内的计算极快，完全允许) */
    uint32_t current_time = (uint32_t)uapi_systick_get_ms();
    if (current_time - s_last_button_time < 400) return;
    s_last_button_time = current_time;

    /* 2. 核心：绝对不加锁！仅抛出事件标志，通知任务线程去处理 */
    g_isr_button_event = 1; 
}

/**
 * @brief 初始化独立按键中断
 */
void KeyInit(void)
{
    uapi_pin_set_mode(BUTTON_GPIO, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BUTTON_GPIO, CORES_APPS_CORE);
    uapi_gpio_set_dir(BUTTON_GPIO, GPIO_DIRECTION_INPUT);
    
    /* 注册下降沿触发中断 */
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func);
    if (ret != 0) uapi_gpio_unregister_isr_func(BUTTON_GPIO);
}

/**
 * @brief 红外人体传感器（PIR）外部边沿触发中断回调
 * @details 核心风控安全机制：检测到活体接近时，强行刹停正在运动的晾衣架，避免发生机械碰撞或夹伤
 */
static void pir_interrupt_handler(pin_t pin, uintptr_t param)
{
    UNUSED(pin); UNUSED(param);
    
    /* 1. 硬件级安防最高优先级响应：立刻赋急停标志！
     *    注：此处对一个 volatile int 进行单次赋值是汇编级的原子操作，绝对安全且耗时仅几纳秒！
     *    底层的 EngineMoveSmooth 运动循环一旦检测到此位为 1，立刻就会刹车。
     */
    g_emergency_stop_flag = 1; 
    
    /* 2. 抛出红外报警事件，交由任务线程处理后续的“改变系统状态、鸣响蜂鸣器、刷 OLED” */
    g_isr_pir_event = 1;
}
/**
 * @brief 初始化红外传感器外部边沿触发中断
 */
void PirInterruptInit(void)
{
    uapi_pin_set_mode(PIR_REL_GPIO_PIN, (pin_mode_t)PIR_GPIO_MODE);
    gpio_select_core(PIR_REL_GPIO_PIN, CORES_APPS_CORE);
    uapi_gpio_set_dir(PIR_REL_GPIO_PIN, GPIO_DIRECTION_INPUT);
    
    /* 注册上升沿触发中断 */
    errcode_t ret = uapi_gpio_register_isr_func(PIR_REL_GPIO_PIN, GPIO_INTERRUPT_RISING_EDGE, pir_interrupt_handler);
    if (ret != 0) uapi_gpio_unregister_isr_func(PIR_REL_GPIO_PIN);
}

/* ==============================================================================
 * 3. 线程一：最高优先级 [Motion_Task]
 *    负责执行电机机械控制，周期 50Hz (20ms)，实时响应急停与运动插值
 * ============================================================================== */
int Motion_Task_Handler(void *param)
{
    UNUSED(param);
    
    /* 1. 硬件执行器初始化 */
    S92RInit();

    /* 默认初始化机械复位至室内安全点（60° 缩回态） */
    g_current_servo_angle = 60;
    SetAngle(AngleToPulse(g_current_servo_angle));
    
    /* 窗户舵机已迁移到星闪 Server，本板只保留远程控制状态。 */
    SysData_Lock();
    g_sys_data.window_state = 0;
    SysData_Unlock();

    while(1) {
        SysData_Lock();
        int rack_cmd = g_sys_data.rack_state;
        SysData_Unlock();
        
        /* --- 1. 晾衣架 180° 舵机平滑伸缩控制（安防闭环升级版） --- */
        if (rack_cmd == 1 && g_current_servo_angle != -45) {
            /* 【安全防护 1】：启动运动前进行前置校验。若存在触发的红外中断或急停标志，立刻放弃启动！ */
            if (g_isr_pir_event == 1 || g_emergency_stop_flag == 1) {
                /* 延时等待下一次调度，让急停处理逻辑介入，绝对不盲目清零急停标志 */
                osal_msleep(20);
                continue; 
            }
            
            /* 确认环境安全后，才允许清零急停标志并执行电机运动 */
            g_emergency_stop_flag = 0;
            EngineMoveSmooth(g_current_servo_angle, -45);
            
            /* 【安全防护 2】：运动结束或被打断后，高优先级任务主动处理状态跃迁！ */
            SysData_Lock();
            if (g_emergency_stop_flag == 1 || g_isr_pir_event == 1) {
                /* 被红外急停打断：立刻由最高优先级任务强行切入报警态！无需等待 Sensor_Task */
                g_sys_data.rack_interrupted_angle = -45.0f;
                g_sys_data.rack_state = 4; // 立即变更为红外报警态
                g_sys_data.buzzer_req = 1; // 触发蜂鸣器报警
                g_sys_data.pir_print_flag = 1;
                g_isr_pir_event = 0;       // 事件已消费，清空标志位
            } else if (g_sys_data.rack_state == 1) {
                /* 正常走完全程未被打断，更新为已完全伸出态 */
                g_sys_data.rack_state = 2;
            }
            SysData_Unlock();
        } 
        else if (rack_cmd == 3 && g_current_servo_angle != 60) {
            /* 同理：缩回前的安全前置校验 */
            if (g_isr_pir_event == 1 || g_emergency_stop_flag == 1) {
                osal_msleep(20);
                continue;
            }
            
            g_emergency_stop_flag = 0;
            EngineMoveSmooth(g_current_servo_angle, 60);
            
            SysData_Lock();
            if (g_emergency_stop_flag == 1 || g_isr_pir_event == 1) {
                g_sys_data.rack_interrupted_angle = 60.0f;
                g_sys_data.rack_state = 4; // 立即变更为红外报警态
                g_sys_data.buzzer_req = 1;
                g_sys_data.pir_print_flag = 1;
                g_isr_pir_event = 0;
            } else if (g_sys_data.rack_state == 3) {
                g_sys_data.rack_state = 0;
            }
            SysData_Unlock();
        }
        
        osal_msleep(20); // 维持控制周期
    }
    return 0;
}

/* ==============================================================================
 * 4. 线程二：中等优先级 [Sensor_Task]
 *    采用分时复用技术，轮询采集环境温湿度、光照度及 ADC 传感器接口，处理环境风控
 * ============================================================================== */
int Sensor_Task_Handler(void *param)
{
    UNUSED(param);
    uint16_t rain_volt = 0, btn_volt = 0;
    uint32_t loop_count = 0;
    
    /* 【修改】：删除了不再使用的 AHT20 与 BH1750 采集局部缓存变量 */

    Key_State_t last_stable_key = KEY_NONE;
    uint8_t     rain_confirm_cnt = 0;
    
    while(1) {
        /* --- 处理按键触发事件 --- */
        if (g_isr_button_event == 1) {
            g_isr_button_event = 0; // 先清空标志位
            
            SysData_Lock(); // 【安全加锁】：现在我们在任务线程里，获取锁是 100% 合法的
            if (g_sys_data.child_lock_enable == 1) {
                printf("[Child Lock ON] Button action rejected!\r\n");
            } else {
                if (g_sys_data.rack_state == 0) {
                    g_sys_data.rack_state = 1;
                    g_emergency_stop_flag = 0;
                } else if (g_sys_data.rack_state == 2) {
                    g_sys_data.rack_state = 3;
                    g_emergency_stop_flag = 0;
                } else if (g_sys_data.rack_state == 1 || g_sys_data.rack_state == 3) {
                    g_emergency_stop_flag = 1;
                    g_sys_data.rack_state = (g_sys_data.rack_state == 1) ? 3 : 1;
                } else if (g_sys_data.rack_state == 4) {
                    g_emergency_stop_flag = 0;
                    g_sys_data.rack_state = (g_sys_data.rack_interrupted_angle == 60.0f) ? 3 : 1;
                }
                g_sys_data.button_print_flag = 1; // 通知 UI 刷屏
            }
            SysData_Unlock(); // 【安全解锁】
        }

        /* --- 处理红外防夹触发事件 --- */
        if (g_isr_pir_event == 1) {
            g_isr_pir_event = 0; // 清空标志位
            
            SysData_Lock();
            /* 如果 Motion_Task 正在运动中，这里的切态其实已经被 Motion_Task 抢先完成了；
             * 这里保留此逻辑可以防止在电机刚准备启动前一纳秒触发红外时的状态遗漏。 */
            if (g_sys_data.rack_state == 1 || g_sys_data.rack_state == 3) {
                g_sys_data.rack_interrupted_angle = (g_sys_data.rack_state == 1) ? -45.0f : 60.0f;
                g_sys_data.rack_state = 4; // 切换为红外报警态
                g_sys_data.buzzer_req = 1; 
                g_sys_data.pir_print_flag = 1;
            }
            SysData_Unlock();
        }
        
        /* --- 1. 雨滴传感器 ADC 采样与天气风控 --- */
        if (adc_port_read(ADC_CH_RAIN_SENSOR, &rain_volt) == ERRCODE_SUCC) {
            /* 电压小于 1300mV 判定为导电率升高（检测到降雨） */
            if (rain_volt < 1300) {
                if (++rain_confirm_cnt >= 3) { // 连续 3 次确认防抖
                    rain_confirm_cnt = 3;
                    int need_remote_close = 0;
                    SysData_Lock();
                    if (g_sys_data.rain_alert == 0) {
                        g_sys_data.rain_alert = 1;
                        printf("Rain detected (%dmV)! Emergency closing sequence activated.\r\n", rain_volt);
                    }
                    /* 降雨自动响应：紧急缩回晾衣架 */
                    if (g_sys_data.rack_state == 2 || g_sys_data.rack_state == 1) {
                        g_emergency_stop_flag = 1;
                        g_sys_data.rack_state = 3;
                    }
                    /* 窗户舵机位于 Server；记录是否需要发送远程关窗命令。 */
                    if (g_sys_data.window_state == 2 || g_sys_data.window_state == 1) {
                        need_remote_close = 1;
                    }
                    SysData_Unlock();

                    if (need_remote_close && SleWindow_Request(WINDOW_CMD_CLOSE) == ERRCODE_SUCC) {
                        SysData_Lock();
                        g_sys_data.window_state = -1; // 远程关窗命令已进入发送队列
                        SysData_Unlock();
                    }
                }
            } else {
                rain_confirm_cnt = 0;
                SysData_Lock(); g_sys_data.rain_alert = 0; SysData_Unlock();
            }
        }
        
        SysData_Lock();
        int is_raining = g_sys_data.rain_alert;
        int win_st     = g_sys_data.window_state;
        SysData_Unlock();
        
        /* --- 2. 板载梯形电阻矩阵按键 ADC 扫描（增加长按3秒童锁控制与拦截） --- */
        static uint16_t key_press_timer = 0;      // 记录按住的总周期数 (乘以50ms)
        static uint8_t  long_press_triggered = 0; // 标记本次按压是否已经触发了长按事件

        if (!is_raining && adc_port_read(ADC_CH_BUTTON, &btn_volt) == ERRCODE_SUCC) {
            Key_State_t cur = KEY_NONE;
            if (btn_volt >= VOLT_S1_MIN && btn_volt < VOLT_S1_MAX)      cur = KEY_S1_PRESS;
            else if (btn_volt >= VOLT_S2_MIN && btn_volt < VOLT_S2_MAX) cur = KEY_S2_PRESS;
            else if (btn_volt >= VOLT_NONE_MIN)                         cur = KEY_NONE;
            
            /* 分支 A：检测到任意一个有效按键正在被按下 */
            if (cur != KEY_NONE) {
                if (cur == last_stable_key) {
                    key_press_timer++; // 键值持续未变，时间计数器累加
                    
                    /* --- 长按 S1 超过 3 秒 (60 * 50ms) -> 启动童锁 --- */
                    if (cur == KEY_S1_PRESS && key_press_timer == 60 && !long_press_triggered) {
                        SysData_Lock();
                        g_sys_data.child_lock_enable = 1; // 激活锁
                        g_sys_data.buzzer_req = 2;        // 请求蜂鸣器鸣响 1 秒
                        SysData_Unlock();
                        long_press_triggered = 1;         // 标记已触发长按，避免按住不放反复写
                        printf(">>> Child Lock ENABLED (S1 Long Press) <<<\r\n");
                    }
                    /* --- 长按 S2 超过 3 秒 (60 * 50ms) -> 解除童锁 --- */
                    else if (cur == KEY_S2_PRESS && key_press_timer == 60 && !long_press_triggered) {
                        SysData_Lock();
                        g_sys_data.child_lock_enable = 0; // 解除锁
                        g_sys_data.buzzer_req = 2;        // 请求蜂鸣器鸣响 1 秒
                        SysData_Unlock();
                        long_press_triggered = 1;
                        printf(">>> Child Lock DISABLED (S2 Long Press) <<<\r\n");
                    }
                } else {
                    /* 按键发生了抖动或刚由空闲转为按压：初始化按压特征 */
                    last_stable_key = cur;
                    key_press_timer = 1;
                    long_press_triggered = 0;
                }
            } 
            /* 分支 B：按键已松开 (由按下变为空闲态)，开始进行“短按动作”触发判定 */
            else {
                if (last_stable_key != KEY_NONE) {
                    /* 只有当按住时间大于防抖时间(150ms)，且没到达长按阈值(<3秒)时，才视为短按操作！ */
                    if (key_press_timer >= DEBOUNCE_COUNT && !long_press_triggered) {
                        window_command_t remote_cmd = WINDOW_CMD_NONE;
                        int child_locked = 0;

                        SysData_Lock();
                        child_locked = g_sys_data.child_lock_enable;
                        if (!child_locked) {
                            if (last_stable_key == KEY_S1_PRESS && win_st == 0) {
                                remote_cmd = WINDOW_CMD_OPEN;
                            } else if (last_stable_key == KEY_S2_PRESS && win_st == 2) {
                                remote_cmd = WINDOW_CMD_CLOSE;
                            }
                        }
                        SysData_Unlock();

                        if (child_locked) {
                            printf("[Child Lock ON] Window action rejected!\r\n");
                        } else if (remote_cmd != WINDOW_CMD_NONE) {
                            if (SleWindow_Request(remote_cmd) == ERRCODE_SUCC) {
                                SysData_Lock();
                                g_sys_data.window_state = (remote_cmd == WINDOW_CMD_OPEN) ? 1 : -1;
                                SysData_Unlock();
                            } else {
                                printf("[SLE Window] Server not ready; command was not sent.\r\n");
                            }
                        }
                    }
                    /* 彻底释放完成，复位计状态 */
                    last_stable_key = KEY_NONE;
                    key_press_timer = 0;
                    long_press_triggered = 0;
                }
            }
        }
        
        /* --- 3 & 4. 【修改】：全面屏蔽 AHT20 与 BH1750 传感器的 I2C 读取 --- */
        /* 不再尝试调用 AHT20 与 BH1750 读取函数，完全杜绝 I2C 通信超时导致死锁，数值持续保持初始的 20 */
        
        loop_count++;
        osal_msleep(50); // 基础采集周期为 50ms
    }
    return 0;
}

/* ==============================================================================
 * 5. 线程三：最低优先级 [UI_Task]
 *    处理慢速外设与用户交互，包括 OLED 字符图形渲染、三色指示灯流转与警报输出
 * ============================================================================== */

/**
 * @brief 动态封装并安全刷新 OLED 显示界面
 * @param state 当前晾衣架系统状态
 * @param temp  实时环境温度 (一直为 20.0)
 * @param humi  实时环境湿度 (一直为 20.0)
 * @param lux   实时环境光强 (一直为 20)
 * @param tick  系统动画时钟帧计数器
 */
void UpdateOledDisplay_Safe(int state, float temp, float humi, uint32_t lux, uint32_t tick)
{
    UNUSED(temp);
    UNUSED(humi);
    char data_str[32];
    ssd1306_Fill(0); // 清空显存缓冲区
    /* 获取当前童锁状态 */
    SysData_Lock();
    int is_locked = g_sys_data.child_lock_enable;
    SysData_Unlock();

    /* 页面分支 0：童锁锁定页面 */
    if (is_locked == 1) {
        ssd1306_DrawRegion(40, 0, 48, image_lock, 384);
        ssd1306_UpdateScreen(); 
        return; 
    }

    /* 页面分支 1：红外触发报警页面（闪烁警告动画） */
    if (state == 4) {
        if ((tick / 5) % 2 == 0) {
            Oled_DrawChineseString(16, 20, hz_line10, 7); // 显示字模："请小心运行中"
        } else {
            ssd1306_Fill(0); // 交替黑屏，形成视觉警示闪烁
        }
        ssd1306_UpdateScreen();
        return;
    }

    /* 页面分支 2：晾衣架正在运动页面（动态进度点动画） */
    if (state == 1 || state == 3) {
        Oled_DrawChineseString(16, 0,  hz_line8, 6);
        Oled_DrawChineseString(16, 24, hz_line9, 3);
        
        /* 根据时钟帧计算动态省略号数量 (1~3个点来回震荡) */
        int step = (int)(tick % 6);
        int dot_count = (step <= 3) ? step : (6 - step);
        int idx = 0;
        for (idx = 0; idx < dot_count; idx++) data_str[idx] = '.';
        data_str[idx] = '\0';
        
        ssd1306_SetCursor(66, 29);
        ssd1306_DrawString(data_str, Font_7x10, 1);
        ssd1306_UpdateScreen();
        return;
    }

    /* 页面分支 3：默认主监控页面（显示状态表头与各类传感器实时数据） */
    if (state == 0) {
        Oled_DrawChineseString(16, 0, hz_line4, 6); // 字模："已安全缩回"
    } else if (state == 2) {
        Oled_DrawChineseString(16, 0, hz_line3, 6); // 字模："已完全伸出"
    }

    /* 渲染行 3：实时光照度值 (固定显示 :20 lux) */
    Oled_DrawChineseString(0, 48, hz_line7, 2);
    snprintf(data_str, sizeof(data_str), ":%u lux", lux);
    ssd1306_SetCursor(34, 51);
    ssd1306_DrawString(data_str, Font_7x10, 1);
    
    ssd1306_UpdateScreen(); // 统一提交显存刷屏
}

int UI_Task_Handler(void *param)
{
    UNUSED(param);
    uint32_t ui_ticks = 0;
    int last_rack_state = -99;
    static int last_lock_state = -99;
    
    /* OLED 开机自检与欢迎界面渲染 */
    ssd1306_Init();
    ssd1306_Fill(0);
    Oled_DrawChineseString(32, 12, hz_line1, 4);
    Oled_DrawChineseString(32, 36, hz_line2, 4);
    ssd1306_UpdateScreen();
    osal_msleep(2000); 
    
    while(1) {
        /* =====================================================================
         * 1. 唯一互斥临界区：批量读取快照 + 统一清零消费标记
         * ===================================================================== */
        SysData_Lock();
        // A. 读取快照到局部栈变量 (此处 t、h、lx 将长久保持 20.0f、20.0f、20)
        int      rack    = g_sys_data.rack_state;
        float    t       = g_sys_data.env_temp;
        float    h       = g_sys_data.env_humi;
        uint32_t lx      = g_sys_data.env_lux;
        int      buzz    = g_sys_data.buzzer_req;
        int      btn_prt = g_sys_data.button_print_flag;
        int      pir_prt = g_sys_data.pir_print_flag;
        float    int_ag  = g_sys_data.rack_interrupted_angle;
        int      ch_lock = g_sys_data.child_lock_enable;
        
        // B. 统一消费重置
        if (btn_prt) g_sys_data.button_print_flag = 0;
        if (pir_prt) g_sys_data.pir_print_flag = 0;
        if (buzz > 0) g_sys_data.buzzer_req = 0;
        SysData_Unlock();

        /* --- 2. 异步打印调试日志 --- */
        if (btn_prt) printf("Button pressed. New State: %d\r\n", rack);
        if (pir_prt) printf("PIR Triggered! Safe return target: %.1f\r\n", int_ag);
        
        /* --- 3. LED 优先级渲染管理 --- */
        if (ch_lock != last_lock_state || rack != last_rack_state) {
            if (ch_lock == 1) {
                SetLedState(LED_GREEN_GPIO,  1);
                SetLedState(LED_YELLOW_GPIO, 1);
                SetLedState(LED_RED_GPIO,    1);
            } else {
                if (rack == 0 || rack == 3) {
                    SetLedState(LED_GREEN_GPIO, 1); SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 0);
                } else if (rack == 2 || rack == 1) {
                    SetLedState(LED_GREEN_GPIO, 0); SetLedState(LED_YELLOW_GPIO, 1); SetLedState(LED_RED_GPIO, 0);
                } else if (rack == 4) {
                    SetLedState(LED_GREEN_GPIO, 0); SetLedState(LED_YELLOW_GPIO, 0); SetLedState(LED_RED_GPIO, 1);
                }
            }
            last_lock_state = ch_lock;
            last_rack_state = rack;
        }

        /* --- 4. 蜂鸣器发声与报警动画 --- */
        if (buzz == 1) {
            Buzzer_Hardware_Start(1333); 
            for (int f = 0; f < 10; f++) {
                UpdateOledDisplay_Safe(rack, t, h, lx, f);
                osal_msleep(100);
            }
            Buzzer_Hardware_Stop();
        } 
        else if (buzz == 2) {
            Buzzer_Hardware_Start(2500); 
            for (int f = 0; f < 4; f++) {
                UpdateOledDisplay_Safe(rack, t, h, lx, f);
                osal_msleep(100);
            }
            Buzzer_Hardware_Stop();
        }

        /* --- 5. 正常周期性 UI 刷新 --- */
        UpdateOledDisplay_Safe(rack, t, h, lx, ui_ticks++);
        osal_msleep(100);
    }
    return 0;
}

/**
 * @brief 星闪写请求确认后的窗口状态同步
 * @note 这是通信写确认，不是远端机械位置传感器反馈。
 */
void SleWindow_OnCommandResult(window_command_t command, errcode_t status)
{
    SysData_Lock();
    if (status == ERRCODE_SUCC) {
        g_sys_data.window_state = (command == WINDOW_CMD_OPEN) ? 2 : 0;
    } else {
        /* 写失败时恢复到发出命令前的稳定状态，允许用户或雨滴逻辑重试。 */
        g_sys_data.window_state = (command == WINDOW_CMD_OPEN) ? 0 : 2;
    }
    SysData_Unlock();

    printf("[SLE Window] command=0x%x, status=0x%x\r\n", command, status);
}

/* ==============================================================================
 * 6. 系统初始化主入口与多任务调度启动
 * ============================================================================== */

/**
 * @brief 应用执行主入口
 * @details 完成底层各模块自检与初始化后，创建并启动高、中、低优先级三个业务内核线程
 */
void main_entry(void)
{
    osal_kthread_lock(); // 临时挂起任务调度器，确保所有初始化过程不被抢占
    
    /* 1. 锁与通信总线初始化 */
    osal_mutex_init(&g_data_mutex);
    app_i2c_init_pin();
    errcode_t ret = uapi_i2c_master_init(1, I2C_SET_BANDRATE, I2C_MASTER_ADDR);
    if (ret != 0) printf("i2c init failed, ret = %0x\r\n", ret);
    
    /* 2. 基础模拟及数字外设初始化 */
    uapi_adc_init(ADC_CLOCK_NONE);
    LedsInit();
    KeyInit();
    PirInterruptInit();
    
    /* 【修改】：完全移除了 AHT20 和 BH1750 的校准与初始化代码 */
    printf(">>> AHT20 & BH1750 disabled! Default values forced to 20. <<<\r\n");
    
    /* 5. 实例化系统三个并发线程（优先级的划分遵循响应时效性要求） */
    
    // 最高优先级：电机控制线程（强实时，要求对限位、急停有最高响应速度）
    osal_task *task_motion = osal_kthread_create(Motion_Task_Handler, NULL, "Task_Motion", MAIN_TASK_STACK_SIZE);
    osal_kthread_set_priority(task_motion, MAIN_TASK_PRIO + 1);
    
    // 中等优先级：传感器数据轮询线程（确保按周期完成模数转换与通讯）
    osal_task *task_sensor = osal_kthread_create(Sensor_Task_Handler, NULL, "Task_Sensor", MAIN_TASK_STACK_SIZE);
    osal_kthread_set_priority(task_sensor, MAIN_TASK_PRIO);
    
    // 最低优先级：用户交互 UI 渲染线程（OLED 通信耗时长，允许被高优先级插队）
    osal_task *task_ui = osal_kthread_create(UI_Task_Handler, NULL, "Task_UI", MAIN_TASK_STACK_SIZE);
    osal_kthread_set_priority(task_ui, MAIN_TASK_PRIO - 1);

    /* 创建星闪 Client 与窗口命令发送任务；窗户舵机本体位于 Server。 */
    if (SleWindow_Client_Start() != ERRCODE_SUCC) {
        printf("[SLE Window] client task creation failed.\r\n");
    }
    
    osal_kthread_unlock(); // 恢复任务调度器，并发系统正式运转
}

/* 注册为系统启动钩子函数 */
app_run(main_entry);