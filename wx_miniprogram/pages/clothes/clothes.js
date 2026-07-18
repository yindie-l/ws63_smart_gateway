// pages/clothes/clothes.js - 晾衣架 & 窗户控制页（使用全局 MQTT 实例，带兜底创建）
const MQTTManager = require('../../utils/mqtt.js');
const app = getApp();

Page({
  data: {
    mqttConnected: false,
    connecting: false,

    hangerStatus: '--',
    hangerText: '--',
    windowStatus: '--',
    windowText: '--',

    pir: 0,
    showWarning: false,
    warningTimer: null,

    sendingCmd: false,
    btnHangerActive: false,
    btnWindowActive: false,

    // ========== 新增：童锁 ==========
    childLock: false,

    // ========== 新增：定时自动关闭 ==========
    timerEnabled: false,
    timerHour: '08',
    timerMinute: '00',
    timerText: ''
  },

  mqtt: null,
  upTopic: null,
  _manualDisconnect: false,
  _timerHandle: null,

  onLoad() {
    // 兜底：如果全局没有 MQTT 实例，则创建并保存
    let mqtt = app.globalData.mqttManager;
    if (!mqtt) {
      console.warn('[Clothes] 全局 MQTT 实例不存在，创建新实例');
      mqtt = new MQTTManager();
      app.globalData.mqttManager = mqtt;
      // 自动连接
      mqtt.connect().catch(err => {
        console.error('[Clothes] MQTT 初始连接失败:', err);
      });
    }
    this.mqtt = mqtt;
    this.upTopic = app.globalData.mqtt.topics.up;

    // 注册数据回调
    this.mqtt.onDataReceived((data) => {
      this.handleTelemetry(data);
    });

    // 注册连接状态回调
    this.mqtt.onConnectionChange((connected) => {
      this.setData({ mqttConnected: connected, connecting: false });
      if (connected) {
        wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
      }
    });

    // 如果全局已连接，同步数据
    if (app.globalData.mqttConnected && app.globalData.latestData) {
      this.setData({ mqttConnected: true });
      this.handleTelemetry(app.globalData.latestData);
    }
  },

  onShow() {
    this._manualDisconnect = false;
    if (app.globalData.mqttConnected && !this.data.mqttConnected) {
      this.setData({ mqttConnected: true });
      if (app.globalData.latestData) {
        this.handleTelemetry(app.globalData.latestData);
      }
    }
  },

  onHide() {
    if (this.data.warningTimer) clearTimeout(this.data.warningTimer);
  },

  onUnload() {
    this._manualDisconnect = true;
    if (this.data.warningTimer) clearTimeout(this.data.warningTimer);
    // 清理未执行的定时任务
    if (this._timerHandle) {
      clearTimeout(this._timerHandle);
      this._timerHandle = null;
    }
    // 不主动断开全局连接
  },

  // ========== 数据处理 ==========
  handleTelemetry(data) {
    const updateData = {};
    if (data.hanger !== undefined) {
      updateData.hangerStatus = data.hanger;
      updateData.hangerText = data.hanger === 'extended' ? '已伸出' : '已收回';
    }
    if (data.pir !== undefined) {
      const p = parseInt(data.pir);
      updateData.pir = p;
      if (p === 1 && data.hanger === 'extended') {
        this.showPirWarning();
      }
    }
    this.setData(updateData);
  },

  // ========== 红外预警 ==========
  showPirWarning() {
    this.setData({ showWarning: true });
    wx.vibrateLong({ fail: () => {} });
    if (this.data.warningTimer) clearTimeout(this.data.warningTimer);
    const timer = setTimeout(() => this.setData({ showWarning: false }), 3000);
    this.setData({ warningTimer: timer });
  },

  // ========== 控制按钮 ==========
  async onRetractHanger() {
    if (!this.checkConnection()) return;
    if (this.data.sendingCmd) return;
    await this.sendCommand('hanger', 'retract', '收回衣架');
  },

  async onExtendHanger() {
    if (!this.checkConnection()) return;
    if (this.data.sendingCmd) return;
    await this.sendCommand('hanger', 'extend', '伸出衣架');
  },

  async onCloseWindow() {
    if (!this.checkConnection()) return;
    if (this.data.sendingCmd) return;
    await this.sendCommand('window', 'retract', '关闭窗户');
  },

  async onOpenWindow() {
    if (!this.checkConnection()) return;
    if (this.data.sendingCmd) return;
    await this.sendCommand('window', 'extend', '打开窗户');
  },

  // ========== 通用发送命令方法 ==========
  async sendCommand(target, action, label) {
    // ========== 童锁拦截：开启后所有控制指令失效 ==========
    if (this.data.childLock) {
      wx.showToast({ title: '童锁已开启，操作被禁止', icon: 'none', duration: 1500 });
      return;
    }

    this.setData({
      sendingCmd: true,
      btnHangerActive: target === 'hanger',
      btnWindowActive: target === 'window'
    });

    try {
      await this.mqtt.publish(this.upTopic, { target, action });

      if (target === 'hanger') {
        this.setData({
          hangerStatus: action === 'extend' ? 'extended' : 'retracted',
          hangerText: action === 'extend' ? '已伸出' : '已收回'
        });
      } else {
        this.setData({
          windowStatus: action === 'extend' ? 'extended' : 'retracted',
          windowText: action === 'extend' ? '已打开' : '已关闭'
        });
      }

      wx.showToast({ title: label + '指令已发送', icon: 'success', duration: 1500 });
    } catch (err) {
      console.error('[Clothes] 指令发送失败:', err);
      wx.showToast({ title: '指令发送失败', icon: 'error', duration: 2000 });
    } finally {
      this.setData({
        sendingCmd: false,
        btnHangerActive: false,
        btnWindowActive: false
      });
    }
  },

  // ========== 连接管理 ==========
  checkConnection() {
    if (!this.data.mqttConnected) {
      wx.showToast({ title: '请先连接MQTT', icon: 'none', duration: 2000 });
      return false;
    }
    return true;
  },

  onReconnect() {
    if (this.mqtt) {
      this.mqtt.disconnect();
      this.mqtt.connect().catch(err => {
        console.error('[Clothes] 重连失败:', err);
      });
    }
    this.setData({
      mqttConnected: false,
      hangerStatus: '--',
      hangerText: '--',
      windowStatus: '--',
      windowText: '--'
    });
  },

  // ========== 新增功能一：童锁 ==========
  // 开启后所有控制功能（衣架/窗户/定时自动关闭）均失效，无法触发任何指令。
  // 童锁开关本身不受限制，以便用户可随时解除锁定。
  onToggleChildLock(e) {
    const locked = e && typeof e.detail === 'object' && e.detail !== null && e.detail.value !== undefined
      ? !!e.detail.value
      : !this.data.childLock;

    this.setData({ childLock: locked });

    if (locked) {
      // 开启童锁时，取消尚未执行的定时任务
      if (this._timerHandle) {
        clearTimeout(this._timerHandle);
        this._timerHandle = null;
      }
      this.setData({ timerEnabled: false, timerText: '' });
      wx.showToast({ title: '童锁已开启', icon: 'none', duration: 1500 });
    } else {
      wx.showToast({ title: '童锁已关闭', icon: 'none', duration: 1500 });
    }

    // 上报童锁状态到设备（与伸出衣架 {target, action} 同格式）
    // 注意：直接 publish，绕过 sendCommand 的童锁拦截，否则开启后会被自身挡住
    if (this.mqtt && this.upTopic) {
      this.mqtt.publish(this.upTopic, { target: 'lock', action: locked ? 'lock' : 'unlock' })
        .then(() => console.log('[Clothes] 童锁状态已上报:', locked ? 'lock' : 'unlock'))
        .catch(err => console.error('[Clothes] 童锁状态上报失败:', err));
    }
  },

  // ========== 新增功能二：定时自动关闭 ==========
  // 用户可自定定时时间（精确到小时:分钟），到点后自动收回晾衣架并关闭窗户。
  // 适配 <picker mode="time">，e.detail.value 形如 "08:30"
  onSetTimer(e) {
    if (this.data.childLock) {
      wx.showToast({ title: '童锁已开启，无法设置定时', icon: 'none', duration: 1500 });
      return;
    }
    let value = (e && e.detail && typeof e.detail.value === 'string') ? e.detail.value : null;
    if (!value) {
      value = `${this.data.timerHour}:${this.data.timerMinute}`;
    }
    const parts = value.split(':');
    if (parts.length < 2) {
      wx.showToast({ title: '时间格式错误', icon: 'none', duration: 1500 });
      return;
    }
    const hour = parseInt(parts[0], 10);
    const minute = parseInt(parts[1], 10);
    if (isNaN(hour) || isNaN(minute) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      wx.showToast({ title: '时间超出范围', icon: 'none', duration: 1500 });
      return;
    }
    this.scheduleClose(hour, minute);
  },

  // 计算到目标时间的延迟并设置定时器（今天已过则顺延到明天）
  scheduleClose(hour, minute) {
    if (this._timerHandle) {
      clearTimeout(this._timerHandle);
      this._timerHandle = null;
    }

    const now = new Date();
    const target = new Date();
    target.setHours(hour, minute, 0, 0);
    if (target.getTime() <= now.getTime()) {
      target.setDate(target.getDate() + 1);
    }
    const delay = target.getTime() - now.getTime();
    const text = `${String(hour).padStart(2, '0')}:${String(minute).padStart(2, '0')}`;

    this._timerHandle = setTimeout(() => {
      this._timerHandle = null;
      this.setData({ timerEnabled: false, timerText: '' });
      this.executeScheduledClose();
    }, delay);

    this.setData({
      timerEnabled: true,
      timerHour: String(hour).padStart(2, '0'),
      timerMinute: String(minute).padStart(2, '0'),
      timerText: text
    });

    wx.showToast({ title: `已设定 ${text} 自动关闭`, icon: 'none', duration: 1500 });
  },

  // 定时到点后执行：收回晾衣架 + 关闭窗户（受童锁约束）
  executeScheduledClose() {
    Promise.resolve()
      .then(() => this.sendCommand('hanger', 'retract', '定时收衣架'))
      .then(() => this.sendCommand('window', 'retract', '定时关窗'))
      .then(() => {
        wx.showToast({ title: '定时任务：已关闭衣架和窗户', icon: 'none', duration: 2000 });
      })
      .catch((err) => {
        console.error('[Clothes] 定时关闭执行失败:', err);
      });
  },

  // 取消已设定的定时任务
  onClearTimer() {
    if (this._timerHandle) {
      clearTimeout(this._timerHandle);
      this._timerHandle = null;
    }
    this.setData({ timerEnabled: false, timerText: '' });
    wx.showToast({ title: '已取消定时', icon: 'none', duration: 1500 });
  }
});