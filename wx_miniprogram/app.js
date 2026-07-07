// app.js - 微信小程序入口
App({
    onLaunch() {
      console.log('[App] 小程序启动');
      this.globalData.mqttConnected = false;
      this.globalData.latestData = null;
    },
  
    globalData: {
      mqttConnected: false,
      latestData: null,
      latestTimestamp: 0,
  
      // ========== 华为云 IoTDA MQTT 配置 ==========
      mqtt: {
        // 华为云 WSS 接入地址
        brokerUrl: 'wss://7b25aad268.st1.iotda-device.cn-north-4.myhuaweicloud.com:443',
        clientId: '6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F_0_1_2026070607',
        username: '6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F',
        password: '404795c2b5cf1a9a6b53151afee2a9e68267e48ada3a3827551eaa2879edbafd',
  
        protocolVersion: 3,        // MQTT v3.1.1
        qos: 1,
        keepalive: 30,
        connectTimeout: 10000,
  
        // 重连配置
        reconnectInterval: 3000,
        maxReconnectAttempts: 10,
  
        // 华为云 Topic 定义（仅订阅下行）
        topics: {
            down: '$oc/devices/6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F/sys/messages/down',
            up:   '$oc/devices/6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F/sys/messages/up'
          }
          
      }
    }
  });