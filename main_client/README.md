# HI3863E 星闪分布式窗户舵机改造包

本包把原智能家居工程拆分成：

- `client_replacements/`：原开发板。保留按钮、雨滴检测和晾衣架舵机，通过星闪发送窗户命令。
- `server_project/`：新开发板。接收星闪命令，在 GPIO0 输出窗户舵机模拟 PWM。

## 命令协议

- `0x01`：开窗到 90°。
- `0x02`：关窗到 0°。

## 重要接线

Server 的 `SLE_WiFi_Coexist_Server/inc/window_server_config.h` 当前配置 `BSP_WINDOW_SERVO = 0`，沿用原工程 GPIO0。舵机应使用独立 5V 电源，舵机电源地与 HI3863E GND 共地，信号线连接 GPIO0。

## 使用顺序

1. 先编译并烧录 Server，确认串口出现 `ready`。
2. 再编译并烧录 Client，等待约 5 秒完成扫描、连接、配对和服务发现。
3. Client 串口出现 `ready, property handle=...` 后，短按 S1 开窗，短按 S2 关窗。
4. 检测到降雨时，Client 会向 Server 发送关窗命令。

当前 Client 的窗口状态依据 SSAP 写确认更新；这表示 Server 已接收命令，不是额外位置传感器的机械闭环反馈。
