// // pages/clothes/clothes.js - 晾衣架 & 窗户控制页
// // 按接口文档：向 /device/ws63/command 发布 {target, action} 格式的指令

// const MQTTManager = require('../../utils/mqtt.js');
// const app = getApp();

// Page({
//   data: {
//     // 连接状态
//     mqttConnected: false,
//     connecting: false,

//     // 衣架状态（来自遥测数据）
//     hangerStatus: '--',       // "retracted" / "extended"
//     hangerText: '--',

//     // 窗户状态
//     windowStatus: '--',
//     windowText: '--',

//     // 红外传感器
//     pir: 0,
//     showWarning: false,
//     warningTimer: null,

//     // 按钮状态
//     sendingCmd: false,
//     btnHangerActive: false,
//     btnWindowActive: false
//   },

//   mqtt: null,

//   onLoad() {
//     this.mqtt = new MQTTManager();

//     // 接收遥测数据回调
//     this.mqtt.onDataReceived((data) => {
//       this.handleTelemetry(data);
//     });

//     // 设备状态回调
//     this.mqtt.onStatusReceived((data) => {
//       console.log('[Clothes] 设备状态更新:', data);
//     });

//     // 连接状态回调
//     this.mqtt.onConnectionChange((connected) => {
//       this.setData({ mqttConnected: connected, connecting: false });
//       if (connected) {
//         wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
//       }
//     });

//     // 如果全局已连接，同步状态
//     if (app.globalData.mqttConnected) {
//       this.setData({ mqttConnected: true });
//       if (app.globalData.latestData) {
//         this.handleTelemetry(app.globalData.latestData);
//       }
//     } else {
//       this.startConnection();
//     }
//   },

//   onShow() {
//     this._manualDisconnect = false;
//     if (app.globalData.mqttConnected && !this.data.mqttConnected) {
//       this.setData({ mqttConnected: true });
//       if (app.globalData.latestData) {
//         this.handleTelemetry(app.globalData.latestData);
//       }
//     }
//   },

//   onHide() {
//     if (this.data.warningTimer) {
//       clearTimeout(this.data.warningTimer);
//     }
//   },

//   onUnload() {
//     this._manualDisconnect = true;
//     if (this.data.warningTimer) clearTimeout(this.data.warningTimer);
//     if (this.mqtt) this.mqtt.disconnect();
//   },

//   // ==================== 数据处理 ====================

//   /**
//    * 处理遥测数据
//    * 提取衣架状态(hanger)和红外(pir)
//    */
//   handleTelemetry(data) {
//     const updateData = {};

//     // 衣架状态（hanger: "retracted" / "extended"）
//     if (data.hanger !== undefined) {
//       updateData.hangerStatus = data.hanger;
//       updateData.hangerText = data.hanger === 'extended' ? '已伸出' : '已收回';
//     }

//     // 红外感应（pir: 0/1）
//     if (data.pir !== undefined) {
//       updateData.pir = data.pir;
//       // 有人靠近 + 衣架伸出 → 自动预警
//       if (data.pir === 1 && data.hanger === 'extended') {
//         this.showPirWarning();
//       }
//     }

//     this.setData(updateData);
//   },

//   // ==================== 红外预警 ====================

//   showPirWarning() {
//     this.setData({ showWarning: true });
//     wx.vibrateLong({ fail: () => {} });

//     if (this.data.warningTimer) clearTimeout(this.data.warningTimer);
//     const timer = setTimeout(() => this.setData({ showWarning: false }), 3000);
//     this.setData({ warningTimer: timer });
//   },

//   // ==================== 衣架控制 ====================

//   /**
//    * 收回衣架
//    * 按接口文档: { target: "hanger", action: "retract" }
//    */
//   onRetractHanger() {
//     if (!this.checkConnection()) return;
//     if (this.data.sendingCmd) return;

//     this.sendCommand('hanger', 'retract', '收回衣架');
//   },

//   /**
//    * 伸出衣架
//    * 按接口文档: { target: "hanger", action: "extend" }
//    */
//   onExtendHanger() {
//     if (!this.checkConnection()) return;
//     if (this.data.sendingCmd) return;

//     this.sendCommand('hanger', 'extend', '伸出衣架');
//   },

//   // ==================== 窗户控制 ====================

//   /**
//    * 关闭窗户
//    * 按接口文档: { target: "window", action: "retract" }
//    */
//   onCloseWindow() {
//     if (!this.checkConnection()) return;
//     if (this.data.sendingCmd) return;

//     this.sendCommand('window', 'retract', '关闭窗户');
//   },

//   /**
//    * 打开窗户
//    * 按接口文档: { target: "window", action: "extend" }
//    */
//   onOpenWindow() {
//     if (!this.checkConnection()) return;
//     if (this.data.sendingCmd) return;

//     this.sendCommand('window', 'extend', '打开窗户');
//   },

//   // ==================== 通用指令发送 ====================

//   /**
//    * 发送控制指令到 /device/ws63/command
//    * @param {string} target - 控制对象: "hanger" 或 "window"
//    * @param {string} action - 执行动作: "retract" 或 "extend"
//    * @param {string} label  - 用于Toast显示的标签
//    */
//   async sendCommand(target, action, label) {
//     this.setData({
//       sendingCmd: true,
//       btnHangerActive: target === 'hanger',
//       btnWindowActive: target === 'window'
//     });

//     try {
//       await this.mqtt.publish(this.mqtt.commandTopic, {
//         target: target,
//         action: action
//       });

//       // 乐观更新UI
//       if (target === 'hanger') {
//         this.setData({
//           hangerStatus: action === 'extend' ? 'extended' : 'retracted',
//           hangerText: action === 'extend' ? '已伸出' : '已收回'
//         });
//       } else {
//         this.setData({
//           windowStatus: action === 'extend' ? 'extended' : 'retracted',
//           windowText: action === 'extend' ? '已打开' : '已关闭'
//         });
//       }

//       wx.showToast({
//         title: label + '成功',
//         icon: 'success',
//         duration: 1500
//       });
//     } catch (err) {
//       console.error('[Clothes] 指令发送失败:', err);
//       wx.showToast({
//         title: '指令发送失败',
//         icon: 'error',
//         duration: 2000
//       });
//     } finally {
//       this.setData({
//         sendingCmd: false,
//         btnHangerActive: false,
//         btnWindowActive: false
//       });
//     }
//   },

//   // ==================== 连接管理 ====================

//   checkConnection() {
//     if (!this.data.mqttConnected) {
//       wx.showToast({ title: '请先连接MQTT Broker', icon: 'none', duration: 2000 });
//       return false;
//     }
//     return true;
//   },

//   async startConnection() {
//     if (this.data.connecting || this.data.mqttConnected) return;
//     this.setData({ connecting: true });

//     try {
//       await this.mqtt.connect();
//     } catch (err) {
//       console.error('[Clothes] MQTT连接失败:', err);
//       this.setData({ connecting: false });
//       wx.showToast({
//         title: '连接失败，请检查Broker',
//         icon: 'none',
//         duration: 2000
//       });
//     }
//   },

//   onReconnect() {
//     if (this.mqtt) this.mqtt.disconnect();
//     this.setData({
//       mqttConnected: false,
//       hangerStatus: '--',
//       hangerText: '--',
//       windowStatus: '--',
//       windowText: '--'
//     });
//     setTimeout(() => this.startConnection(), 500);
//   }
// });
// pages/clothes/clothes.js - 晾衣架 & 窗户状态展示（仅接收，控制功能暂不启用）
// pages/clothes/clothes.js - 晾衣架 & 窗户控制页（包含发送命令）
// pages/clothes/clothes.js - 晾衣架 & 窗户控制页（包含发送命令）
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
    btnWindowActive: false
  },

  mqtt: null,
  upTopic: null, // 保存上行主题

  onLoad() {
    this.mqtt = new MQTTManager();
    this.upTopic = app.globalData.mqtt.topics.up; // 获取上行主题

    this.mqtt.onDataReceived((data) => {
      this.handleTelemetry(data);
    });

    this.mqtt.onConnectionChange((connected) => {
      this.setData({ mqttConnected: connected, connecting: false });
      if (connected) {
        wx.showToast({ title: 'MQTT已连接', icon: 'success', duration: 1500 });
      }
    });

    if (app.globalData.mqttConnected) {
      this.setData({ mqttConnected: true });
      if (app.globalData.latestData) {
        this.handleTelemetry(app.globalData.latestData);
      }
    } else {
      this.startConnection();
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
    if (this.mqtt) this.mqtt.disconnect();
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

  // ========== 控制按钮（实际发送命令） ==========
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
    this.setData({
      sendingCmd: true,
      btnHangerActive: target === 'hanger',
      btnWindowActive: target === 'window'
    });

    try {
      // 发布到华为云上行主题
      await this.mqtt.publish(this.upTopic, { target, action });

      // 乐观更新UI（本地先显示，后续遥测数据会覆盖）
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

  async startConnection() {
    if (this.data.connecting || this.data.mqttConnected) return;
    this.setData({ connecting: true });
    try {
      await this.mqtt.connect();
    } catch (err) {
      console.error('[Clothes] MQTT连接失败:', err);
      this.setData({ connecting: false });
      wx.showToast({ title: '连接失败，请检查配置', icon: 'none', duration: 2000 });
    }
  },

  onReconnect() {
    if (this.mqtt) this.mqtt.disconnect();
    this.setData({
      mqttConnected: false,
      hangerStatus: '--',
      hangerText: '--',
      windowStatus: '--',
      windowText: '--'
    });
    setTimeout(() => this.startConnection(), 500);
  }
});