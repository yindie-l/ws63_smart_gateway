// app.js - 微信小程序入口
App({
    onLaunch() {
      console.log('[App] 小程序启动');
      this.globalData.mqttConnected = false;
      this.globalData.latestData = null;
      this.globalData.latestTimestamp = 0;
      // ❌ 删除 MQTT 实例创建，由页面负责创建
    },
  
    globalData: {
      mqttConnected: false,
      latestData: null,
      latestTimestamp: 0,
      mqttManager: null,  // 页面创建后存入此处，供其他页面共享
  
      // ========== 华为云 IoTDA MQTT 配置 ==========
      mqtt: {
        brokerUrl: 'wss://7b25aad268.st1.iotda-device.cn-north-4.myhuaweicloud.com:443',
        clientId: '6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F_0_1_2026070813',
        username: '6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F',
        password: '7a9ab7a5deb221adb12727d2e024163095cab3d11b222cad798e42236f004b3c',
  
        protocolVersion: 3,
        qos: 1,
        keepalive: 30,
        connectTimeout: 10000,
        reconnectInterval: 3000,
        maxReconnectAttempts: 10,
  
        topics: {
          down: '$oc/devices/6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F/sys/messages/down',
          up:   '$oc/devices/6a4610777f2e6c302f812758_F4-3B-D8-94-5B-6F/sys/messages/up'
        }
      }
    }
  });