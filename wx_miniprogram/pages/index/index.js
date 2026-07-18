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