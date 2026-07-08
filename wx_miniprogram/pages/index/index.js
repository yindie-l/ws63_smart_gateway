// // // pages/index/index.js - 环境监测主页
// // // 显示温度、湿度、雨量、衣架状态、红外感应数据

// // const MQTTManager = require('../../utils/mqtt.js');

// // Page({
// //   data: {
// //     // 连接状态
// //     mqttConnected: false,
// //     connecting: false,

// //     // 传感器数据（对应接口文档字段）
// //     temperature: '--',      // temp: 温度(℃)
// //     humidity: '--',         // humidity: 相对湿度(%RH)
// //     rain: 0,                // rain: 0=无雨, 1=有雨
// //     rainText: '干燥',       // 雨量文本
// //     hanger: '--',           // hanger: "retracted"/"extended"
// //     hangerText: '--',       // 衣架状态文本
// //     pir: 0,                 // pir: 0=无人, 1=有人
// //     pirText: '无人',        // 红外文本

// //     // 异常状态
// //     tempAbnormal: false,    // 温度传感器异常(-999)

// //     // 数据更新时间
// //     lastUpdate: '',

// //     // 趋势指示
// //     tempTrend: 0,           // 1=上升, -1=下降, 0=不变
// //     humiTrend: 0
// //   },

// //   mqtt: null,

// //   onLoad() {
// //     this.mqtt = new MQTTManager();

// //     // 接收遥测数据回调
// //     this.mqtt.onDataReceived((data) => {
// //       this.updateSensorData(data);
// //     });

// //     // 设备状态回调
// //     this.mqtt.onStatusReceived((data) => {
// //       console.log('[Index] 设备状态更新:', data);
// //     });

// //     // 连接状态回调
// //     this.mqtt.onConnectionChange((connected) => {
// //       this.setData({ mqttConnected: connected, connecting: false });
// //       if (connected) {
// //         wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
// //       } else if (!this._manualDisconnect) {
// //         wx.showToast({ title: '连接断开，自动重连中', icon: 'none', duration: 2000 });
// //       }
// //     });

// //     // 自动连接
// //     this.startConnection();
// //   },

// //   onShow() {
// //     this._manualDisconnect = false;
// //     if (!this.data.mqttConnected && !this.data.connecting) {
// //       this.startConnection();
// //     }
// //   },

// //   onHide() {
// //     // 页面隐藏时不主动断连，保持数据同步
// //   },

// //   onUnload() {
// //     this._manualDisconnect = true;
// //     if (this.mqtt) {
// //       this.mqtt.disconnect();
// //     }
// //   },

// //   // ==================== 连接管理 ====================

// //   /**
// //    * 开始连接 MQTT Broker
// //    */
// //   async startConnection() {
// //     if (this.data.connecting || this.data.mqttConnected) return;
// //     this.setData({ connecting: true });

// //     try {
// //       await this.mqtt.connect();
// //     } catch (err) {
// //       console.error('[Index] MQTT连接失败:', err);
// //       this.setData({ connecting: false });
// //       wx.showToast({
// //         title: '连接失败，请检查网络和Broker配置',
// //         icon: 'none',
// //         duration: 2500
// //       });
// //     }
// //   },

// //   /**
// //    * 手动重连
// //    */
// //   onReconnect() {
// //     if (this.mqtt) this.mqtt.disconnect();
// //     this.setData({
// //       mqttConnected: false,
// //       temperature: '--',
// //       humidity: '--',
// //       rain: 0,
// //       rainText: '干燥',
// //       hanger: '--',
// //       hangerText: '--',
// //       pir: 0,
// //       pirText: '无人',
// //       tempAbnormal: false
// //     });
// //     setTimeout(() => this.startConnection(), 500);
// //   },

// //   // ==================== 数据处理 ====================

// //   /**
// //    * 更新传感器数据（按接口文档字段映射）
// //    *
// //    * 字段映射：
// //    *   timestamp → 更新时间显示
// //    *   temp      → temperature（-999表示异常）
// //    *   humidity  → humidity
// //    *   rain      → rain（0/1）
// //    *   hanger    → hanger（"retracted"/"extended"）
// //    *   pir       → pir（0/1）
// //    */
// //   updateSensorData(data) {
// //     const updateData = {};

// //     // 更新时间
// //     if (data.timestamp) {
// //       const date = new Date(data.timestamp * 1000);
// //       updateData.lastUpdate = [
// //         String(date.getHours()).padStart(2, '0'),
// //         String(date.getMinutes()).padStart(2, '0'),
// //         String(date.getSeconds()).padStart(2, '0')
// //       ].join(':');
// //     } else {
// //       const now = new Date();
// //       updateData.lastUpdate = [
// //         String(now.getHours()).padStart(2, '0'),
// //         String(now.getMinutes()).padStart(2, '0'),
// //         String(now.getSeconds()).padStart(2, '0')
// //       ].join(':');
// //     }

// //     // 温度（temp → temperature）
// //     if (data.temp !== undefined) {
// //       if (data.temp === -999 || data.temp === null) {
// //         // 传感器异常
// //         updateData.temperature = '--';
// //         updateData.tempAbnormal = true;
// //       } else {
// //         updateData.temperature = parseFloat(data.temp).toFixed(1);
// //         updateData.tempAbnormal = false;

// //         // 计算趋势
// //         const prevTemp = this.data.temperature !== '--' ? parseFloat(this.data.temperature) : null;
// //         if (prevTemp !== null) {
// //           updateData.tempTrend = data.temp > prevTemp ? 1 : (data.temp < prevTemp ? -1 : 0);
// //         }
// //       }
// //     }

// //     // 湿度（humidity → humidity）
// //     if (data.humidity !== undefined) {
// //       updateData.humidity = parseFloat(data.humidity).toFixed(1);

// //       // 计算趋势
// //       const prevHumi = this.data.humidity !== '--' ? parseFloat(this.data.humidity) : null;
// //       if (prevHumi !== null) {
// //         updateData.humiTrend = data.humidity > prevHumi ? 1 : (data.humidity < prevHumi ? -1 : 0);
// //       }
// //     }

// //     // 雨量（rain: 0/1）
// //     if (data.rain !== undefined) {
// //       updateData.rain = data.rain;
// //       updateData.rainText = data.rain === 1 ? '有雨' : '干燥';
// //     }

// //     // 衣架状态（hanger: "retracted"/"extended"）
// //     if (data.hanger !== undefined) {
// //       updateData.hanger = data.hanger;
// //       updateData.hangerText = data.hanger === 'extended' ? '已伸出' : '已收回';
// //     }

// //     // 红外感应（pir: 0/1）
// //     if (data.pir !== undefined) {
// //       updateData.pir = data.pir;
// //       updateData.pirText = data.pir === 1 ? '有人' : '无人';
// //     }

// //     this.setData(updateData);
// //   }
// // });
// // pages/index/index.js - 环境监测主页（只接收展示）
// // pages/index/index.js - 环境监测主页（只接收展示）
// const MQTTManager = require('../../utils/mqtt.js');

// Page({
//   data: {
//     mqttConnected: false,
//     connecting: false,

//     temperature: '--',
//     humidity: '--',
//     rain: 0,
//     rainText: '干燥',
//     hanger: '--',
//     hangerText: '--',
//     pir: 0,
//     pirText: '无人',

//     light: 326,
//     lightPercent: 50,  

//     tempAbnormal: false,
//     lastUpdate: '',
//     tempTrend: 0,
//     humiTrend: 0
//   },

//   mqtt: null,

//   onLoad() {
//     this.mqtt = new MQTTManager();

//     this.mqtt.onDataReceived((data) => {
//       this.updateSensorData(data);
//     });

//     this.mqtt.onConnectionChange((connected) => {
//       this.setData({ mqttConnected: connected, connecting: false });
//       if (connected) {
//         wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
//       } else if (!this._manualDisconnect) {
//         wx.showToast({ title: '连接断开，自动重连中', icon: 'none', duration: 2000 });
//       }
//     });

//     this.startConnection();
//   },

//   onShow() {
//     this._manualDisconnect = false;
//     if (!this.data.mqttConnected && !this.data.connecting) {
//       this.startConnection();
//     }
//   },

//   onHide() {},

//   onUnload() {
//     this._manualDisconnect = true;
//     if (this.mqtt) {
//       this.mqtt.disconnect();
//     }
//   },

//   async startConnection() {
//     if (this.data.connecting || this.data.mqttConnected) return;
//     this.setData({ connecting: true });
//     try {
//       await this.mqtt.connect();
//     } catch (err) {
//       console.error('[Index] 连接失败:', err);
//       this.setData({ connecting: false });
//       wx.showToast({
//         title: '连接失败，请检查网络和配置',
//         icon: 'none',
//         duration: 2500
//       });
//     }
//   },

//   onReconnect() {
//     if (this.mqtt) this.mqtt.disconnect();
//     this.setData({
//       mqttConnected: false,
//       temperature: '--',
//       humidity: '--',
//       rain: 0,
//       rainText: '干燥',
//       hanger: '--',
//       hangerText: '--',
//       pir: 0,
//       pirText: '无人',
//       tempAbnormal: false
//     });
//     setTimeout(() => this.startConnection(), 500);
//   },

//   // ========== 数据更新（适配华为云格式） ==========
//   updateSensorData(data) {
//     const updateData = {};

//     // 更新时间
//     const now = new Date();
//     updateData.lastUpdate = [
//       String(now.getHours()).padStart(2, '0'),
//       String(now.getMinutes()).padStart(2, '0'),
//       String(now.getSeconds()).padStart(2, '0')
//     ].join(':');

//     // 温度
//     if (data.temp !== undefined) {
//       if (data.temp === null || data.temp === -999) {
//         updateData.temperature = '--';
//         updateData.tempAbnormal = true;
//       } else {
//         const val = parseFloat(data.temp);
//         updateData.temperature = val.toFixed(1);
//         updateData.tempAbnormal = false;
//         const prev = parseFloat(this.data.temperature);
//         if (!isNaN(prev)) {
//           updateData.tempTrend = val > prev ? 1 : (val < prev ? -1 : 0);
//         }
//       }
//     }

//     // 湿度
//     if (data.humidity !== undefined) {
//       const val = parseFloat(data.humidity);
//       updateData.humidity = val.toFixed(1);
//       const prev = parseFloat(this.data.humidity);
//       if (!isNaN(prev)) {
//         updateData.humiTrend = val > prev ? 1 : (val < prev ? -1 : 0);
//       }
//     }

//     // 雨量
//     if (data.rain !== undefined) {
//       const r = parseInt(data.rain);
//       updateData.rain = r;
//       updateData.rainText = r === 1 ? '有雨' : '干燥';
//     }

//     // 衣架
//     if (data.hanger !== undefined) {
//       updateData.hanger = data.hanger;
//       updateData.hangerText = data.hanger === 'extended' ? '已伸出' : '已收回';
//     }

//     // 红外
//     if (data.pir !== undefined) {
//       const p = parseInt(data.pir);
//       updateData.pir = p;
//       updateData.pirText = p === 1 ? '有人' : '无人';
//     }
//     this.setData(updateData);
//   }
// });
// pages/index/index.js - 环境监测主页（使用全局 MQTT 实例）
// pages/index/index.js - 环境监测主页（使用全局 MQTT 实例，带兜底创建）
const MQTTManager = require('../../utils/mqtt.js');
const app = getApp();

Page({
  data: {
    mqttConnected: false,
    connecting: false,

    temperature: '--',
    humidity: '--',
    rain: 0,
    rainText: '干燥',
    hanger: '--',
    hangerText: '--',
    pir: 0,
    pirText: '无人',

    light: 326,
    lightPercent: 70,

    tempAbnormal: false,
    lastUpdate: '',
    tempTrend: 0,
    humiTrend: 0
  },

  mqtt: null,
  _manualDisconnect: false,

  onLoad() {
    // 兜底：如果全局没有 MQTT 实例，则创建并保存
    let mqtt = app.globalData.mqttManager;
    if (!mqtt) {
      console.warn('[Index] 全局 MQTT 实例不存在，创建新实例');
      mqtt = new MQTTManager();
      app.globalData.mqttManager = mqtt;
      // 自动连接
      mqtt.connect().catch(err => {
        console.error('[Index] MQTT 初始连接失败:', err);
      });
    }
    this.mqtt = mqtt;

    // 注册数据回调
    this.mqtt.onDataReceived((data) => {
      this.updateSensorData(data);
    });

    // 注册连接状态回调
    this.mqtt.onConnectionChange((connected) => {
      this.setData({ mqttConnected: connected, connecting: false });
      if (connected) {
        wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
      } else if (!this._manualDisconnect) {
        wx.showToast({ title: '连接断开，自动重连中', icon: 'none', duration: 2000 });
      }
    });

    // 如果全局已连接，立即同步数据
    if (app.globalData.mqttConnected && app.globalData.latestData) {
      this.updateSensorData(app.globalData.latestData);
      this.setData({ mqttConnected: true });
    }
  },

  onShow() {
    this._manualDisconnect = false;
    if (app.globalData.mqttConnected && !this.data.mqttConnected) {
      this.setData({ mqttConnected: true });
      if (app.globalData.latestData) {
        this.updateSensorData(app.globalData.latestData);
      }
    }
  },

  onHide() {},

  onUnload() {
    this._manualDisconnect = true;
    // 不主动断开全局连接
  },

  onReconnect() {
    if (this.mqtt) {
      this.mqtt.disconnect();
      this.mqtt.connect().catch(err => {
        console.error('[Index] 重连失败:', err);
      });
    }
    this.setData({
      mqttConnected: false,
      temperature: '--',
      humidity: '--',
      rain: 0,
      rainText: '干燥',
      hanger: '--',
      hangerText: '--',
      pir: 0,
      pirText: '无人',
      tempAbnormal: false
    });
  },

  // ========== 数据更新 ==========
  updateSensorData(data) {
    const updateData = {};

    const now = new Date();
    updateData.lastUpdate = [
      String(now.getHours()).padStart(2, '0'),
      String(now.getMinutes()).padStart(2, '0'),
      String(now.getSeconds()).padStart(2, '0')
    ].join(':');

    if (data.temp !== undefined) {
      if (data.temp === null || data.temp === -999) {
        updateData.temperature = '--';
        updateData.tempAbnormal = true;
      } else {
        const val = parseFloat(data.temp);
        updateData.temperature = val.toFixed(1);
        updateData.tempAbnormal = false;
        const prev = parseFloat(this.data.temperature);
        if (!isNaN(prev)) {
          updateData.tempTrend = val > prev ? 1 : (val < prev ? -1 : 0);
        }
      }
    }

    if (data.humidity !== undefined) {
      const val = parseFloat(data.humidity);
      updateData.humidity = val.toFixed(1);
      const prev = parseFloat(this.data.humidity);
      if (!isNaN(prev)) {
        updateData.humiTrend = val > prev ? 1 : (val < prev ? -1 : 0);
      }
    }

    if (data.rain !== undefined) {
      const r = parseInt(data.rain);
      updateData.rain = r;
      updateData.rainText = r === 1 ? '有雨' : '干燥';
    }

    if (data.hanger !== undefined) {
      updateData.hanger = data.hanger;
      updateData.hangerText = data.hanger === 'extended' ? '已伸出' : '已收回';
    }

    if (data.pir !== undefined) {
      const p = parseInt(data.pir);
      updateData.pir = p;
      updateData.pirText = p === 1 ? '有人' : '无人';
    }

    this.setData(updateData);
  }
});