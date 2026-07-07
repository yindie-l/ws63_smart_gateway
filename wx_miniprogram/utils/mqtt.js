// // utils/mqtt.js - MQTT通信工具模块
// //
// // 协议：MQTT v3.1.1 over WebSocket
// // 依赖：需要在项目中引入 mqtt.min.js（MQTT.js 的微信小程序适配版）
// //
// // 安装方式（二选一）：
// //   1. npm:  npm install mqtt --save，构建npm后在页面 require('mqtt/dist/mqtt.min.js')
// //   2. 手动：下载 mqtt.min.js 放到 utils/ 目录下，使用下方 require
// //
// // 微信小程序 MQTT 配置要点：
// //   1. 微信小程序不支持原生TCP，必须通过 WebSocket 连接 MQTT Broker
// //   2. 连接URL格式: wxs://broker-host:port/mqtt
// //   3. 开发阶段在「详情 → 本地设置」中勾选「不校验合法域名」

// const app = getApp();

// class MQTTManager {
//   constructor() {
//     this._client = null;
//     this._connected = false;
//     this._reconnectAttempts = 0;
//     this._reconnectTimer = null;
//     this._onDataReceived = null;
//     this._onStatusReceived = null;
//     this._onConnectionChange = null;
//     this._topics = app.globalData.mqtt.topics;
//     this._config = app.globalData.mqtt;
//     this._lastTimestamp = 0; // 用于消息去重/乱序处理
//   }

//   // ==================== 连接管理 ====================

//   /**
//    * 连接 MQTT Broker
//    * 使用 mqtt.js 库连接（微信小程序适配版）
//    */
//   connect() {
//     return new Promise((resolve, reject) => {
//       if (this._connected) {
//         resolve();
//         return;
//       }

//       const config = this._config;
//       console.log('[MQTT] 正在连接 Broker:', config.brokerUrl);
//       console.log('[MQTT] Client ID:', config.clientId);
//       console.log('[MQTT] 协议版本: MQTT v3.1.1');

//       try {
//         // 使用 mqtt.js 连接
//         const mqtt = require('mqtt/dist/mqtt.min.js');
//         this._client = mqtt.connect(config.brokerUrl, {
//           clientId: config.clientId,
//           username: config.username || undefined,
//           password: config.password || undefined,
//           clean: true,
//           reconnectPeriod: 0,       // 手动管理重连
//           connectTimeout: config.connectTimeout || 10000,
//           keepalive: config.keepalive || 30,
//           protocolVersion: config.protocolVersion || 3,
//           // 微信小程序 WebSocket 适配
//           wsOptions: {}
//         });

//         // 连接成功
//         this._client.on('connect', () => {
//           console.log('[MQTT] 连接成功');
//           this._connected = true;
//           app.globalData.mqttConnected = true;
//           this._reconnectAttempts = 0;
//           this._notifyConnectionChange(true);

//           // 连接成功后立即订阅所有上行主题
//           this._subscribeAll();
//           resolve();
//         });

//         // 收到消息
//         this._client.on('message', (topic, payload) => {
//           this._handleMessage(topic, payload);
//         });

//         // 连接关闭
//         this._client.on('close', () => {
//           console.log('[MQTT] 连接已关闭');
//           this._connected = false;
//           app.globalData.mqttConnected = false;
//           this._notifyConnectionChange(false);
//           this._tryReconnect();
//         });

//         // 连接错误
//         this._client.on('error', (err) => {
//           console.error('[MQTT] 连接错误:', err);
//           this._connected = false;
//           app.globalData.mqttConnected = false;
//           this._notifyConnectionChange(false);
//           reject(err);
//         });

//         // 离线消息（遗嘱消息触发时）
//         this._client.on('offline', () => {
//           console.log('[MQTT] 客户端离线');
//         });

//         // 重连事件（mqtt.js 内置重连）
//         this._client.on('reconnect', () => {
//           console.log('[MQTT] mqtt.js 正在自动重连...');
//         });

//       } catch (err) {
//         console.error('[MQTT] 连接异常:', err);
//         reject(err);
//       }
//     });
//   }

//   /**
//    * 订阅所有上行主题
//    * 按照接口文档：订阅 /device/ws63/telemetry 和 /device/ws63/status
//    */
//   _subscribeAll() {
//     const topics = this._topics;
//     const qos = this._config.qos || 1;

//     // 订阅遥测数据主题
//     this._client.subscribe(topics.telemetry, { qos: qos }, (err) => {
//       if (err) {
//         console.error('[MQTT] 订阅遥测主题失败:', topics.telemetry, err);
//       } else {
//         console.log('[MQTT] 订阅遥测主题成功:', topics.telemetry);
//       }
//     });

//     // 订阅设备状态主题（遗嘱消息/在线状态）
//     this._client.subscribe(topics.status, { qos: qos }, (err) => {
//       if (err) {
//         console.error('[MQTT] 订阅状态主题失败:', topics.status, err);
//       } else {
//         console.log('[MQTT] 订阅状态主题成功:', topics.status);
//       }
//     });
//   }

//   /**
//    * 断开连接
//    * 小程序切到后台时调用，优雅断开 MQTT
//    */
//   disconnect() {
//     if (this._reconnectTimer) {
//       clearTimeout(this._reconnectTimer);
//       this._reconnectTimer = null;
//     }
//     // 阻止重连
//     this._reconnectAttempts = this._config.maxReconnectAttempts;

//     if (this._client) {
//       this._client.end(true, () => {
//         console.log('[MQTT] 已主动断开');
//       });
//       this._client = null;
//     }
//     this._connected = false;
//     app.globalData.mqttConnected = false;
//     this._notifyConnectionChange(false);
//   }

//   // ==================== 订阅/发布 ====================

//   /**
//    * 订阅指定主题
//    */
//   subscribe(topic) {
//     if (!this._connected || !this._client) {
//       console.warn('[MQTT] 未连接，无法订阅:', topic);
//       return;
//     }

//     const qos = this._config.qos || 1;
//     console.log('[MQTT] 订阅主题:', topic, 'QoS:', qos);

//     this._client.subscribe(topic, { qos: qos }, (err) => {
//       if (err) {
//         console.error('[MQTT] 订阅失败:', topic, err);
//       } else {
//         console.log('[MQTT] 订阅成功:', topic);
//       }
//     });
//   }

//   /**
//    * 发布消息到指定主题
//    * @param {string} topic - 目标主题
//    * @param {object} data  - 要发送的数据对象
//    */
//   publish(topic, data) {
//     if (!this._connected || !this._client) {
//       console.error('[MQTT] 未连接，无法发布到:', topic);
//       return Promise.reject(new Error('MQTT 未连接'));
//     }

//     const payload = JSON.stringify(data);
//     const qos = this._config.qos || 1;
//     console.log('[MQTT] 发布消息 →', topic, '| QoS:', qos, '| Payload:', payload);

//     return new Promise((resolve, reject) => {
//       this._client.publish(topic, payload, { qos: qos }, (err) => {
//         if (err) {
//           console.error('[MQTT] 发布失败:', err);
//           reject(err);
//         } else {
//           console.log('[MQTT] 发布成功');
//           resolve();
//         }
//       });
//     });
//   }

//   // ==================== 数据处理 ====================

//   /**
//    * 处理接收到的 MQTT 消息
//    * @param {string} topic   - 消息来源主题
//    * @param {Buffer} payload - 消息内容
//    */
//   _handleMessage(topic, payload) {
//     const raw = payload.toString();
//     console.log('[MQTT] 收到消息 ←', topic, '|', raw);

//     try {
//       const data = JSON.parse(raw);

//       // 根据主题区分消息类型
//       if (topic === this._topics.telemetry) {
//         this._handleTelemetry(data);
//       } else if (topic === this._topics.status) {
//         this._handleStatus(data);
//       } else {
//         console.warn('[MQTT] 收到未知主题消息:', topic);
//       }
//     } catch (e) {
//       console.error('[MQTT] 消息解析失败:', e, '原始数据:', raw);
//     }
//   }

//   /**
//    * 处理遥测数据（/device/ws63/telemetry）
//    *
//    * 字段定义：
//    *   timestamp - Unix时间戳(秒)
//    *   temp      - 温度(℃)，异常时=-999
//    *   humidity  - 相对湿度(%RH)
//    *   rain      - 雨量: 0=无雨, 1=有雨
//    *   hanger    - 衣架状态: "retracted"=收回, "extended"=伸出
//    *   pir       - 红外: 0=无人, 1=有人
//    */
//   _handleTelemetry(data) {
//     const ts = data.timestamp || 0;

//     // 防抖处理：丢弃比当前时间戳更旧的数据包（处理消息乱序）
//     if (ts > 0 && ts < this._lastTimestamp) {
//       console.log('[MQTT] 丢弃旧数据, timestamp:', ts, '<', this._lastTimestamp);
//       return;
//     }
//     this._lastTimestamp = ts;

//     // 温度异常值处理：-999 表示传感器异常
//     if (data.temp !== undefined && data.temp === -999) {
//       data.temp = null;
//       console.warn('[MQTT] 温度传感器异常');
//     }

//     // 更新全局最新数据
//     app.globalData.latestData = data;
//     app.globalData.latestTimestamp = ts;

//     // 回调通知页面
//     if (this._onDataReceived) {
//       this._onDataReceived(data);
//     }
//   }

//   /**
//    * 处理设备状态消息（/device/ws63/status）
//    * 遗嘱消息，用于感知设备在线/离线
//    */
//   _handleStatus(data) {
//     console.log('[MQTT] 设备状态:', JSON.stringify(data));

//     if (this._onStatusReceived) {
//       this._onStatusReceived(data);
//     }
//   }

//   // ==================== 重连机制 ====================

//   /**
//    * 手动重连逻辑
//    * 当 mqtt.js 内置重连不够用时的补充
//    */
//   _tryReconnect() {
//     if (this._reconnectAttempts >= this._config.maxReconnectAttempts) {
//       console.log('[MQTT] 已达最大重连次数(' + this._config.maxReconnectAttempts + ')，停止重连');
//       wx.showToast({
//         title: '网络异常，请检查 Wi-Fi 连接',
//         icon: 'none',
//         duration: 3000
//       });
//       return;
//     }

//     this._reconnectAttempts++;
//     const delay = this._config.reconnectInterval;
//     console.log('[MQTT] ' + delay / 1000 + 's 后进行第 ' + this._reconnectAttempts + ' 次重连...');

//     this._reconnectTimer = setTimeout(() => {
//       if (this._connected) return; // 可能 mqtt.js 已经重连成功

//       this.connect().catch((err) => {
//         console.error('[MQTT] 重连失败:', err);
//         // _tryReconnect 会在 close 事件中再次被调用
//       });
//     }, delay);
//   }

//   // ==================== 回调注册 ====================

//   /**
//    * 设置遥测数据接收回调
//    */
//   onDataReceived(callback) {
//     this._onDataReceived = callback;
//   }

//   /**
//    * 设置设备状态接收回调
//    */
//   onStatusReceived(callback) {
//     this._onStatusReceived = callback;
//   }

//   /**
//    * 设置连接状态变化回调
//    */
//   onConnectionChange(callback) {
//     this._onConnectionChange = callback;
//   }

//   _notifyConnectionChange(connected) {
//     if (this._onConnectionChange) {
//       this._onConnectionChange(connected);
//     }
//   }

//   // ==================== 属性访问 ====================

//   /**
//    * 获取连接状态
//    */
//   get isConnected() {
//     return this._connected;
//   }

//   /**
//    * 获取命令下发主题
//    */
//   get commandTopic() {
//     return this._topics.command;
//   }

//   /**
//    * 获取遥测数据主题
//    */
//   get telemetryTopic() {
//     return this._topics.telemetry;
//   }
// }

// module.exports = MQTTManager;
// utils/mqtt.js - 原生 WebSocket MQTT 客户端（适配华为云 IoTDA）

const app = getApp();

class MQTTManager {
  constructor() {
    this._socket = null;
    this._connected = false;
    this._reconnectAttempts = 0;
    this._reconnectTimer = null;
    this._onDataReceived = null;
    this._onConnectionChange = null;
    this._config = app.globalData.mqtt;
    this._topic = this._config.topics.down;
    this._keepaliveTimer = null;
    this._pingTimer = null;
    this._pendingPing = false;
    this._isManualDisconnect = false;
    this._connackTimer = null;
  }

  // ========== 连接 ==========
  connect() {
    return new Promise((resolve, reject) => {
      if (this._connected) {
        resolve();
        return;
      }

      const config = this._config;
      // 确保 URL 包含路径（华为云通常需要 /mqtt）
      let url = config.brokerUrl;
      if (!url.includes('/mqtt') && !url.endsWith('/')) {
        url = url + '/mqtt';
      }

      console.log('[MQTT] 连接华为云:', url);
      console.log('[MQTT] Client ID:', config.clientId);

      // 构建 CONNECT 报文
      const connectPacket = this._buildConnectPacket(config);
      if (!connectPacket) {
        reject(new Error('构建 CONNECT 报文失败'));
        return;
      }

      try {
        this._socket = wx.connectSocket({
          url: url,
          protocols: ['mqttv3.1'],  // 华为云支持 mqttv3.1
          success: () => {
            console.log('[MQTT] WebSocket 连接创建成功');
          },
          fail: (err) => {
            console.error('[MQTT] WebSocket 连接创建失败:', err);
            reject(err);
          }
        });

        this._socket.onOpen(() => {
          console.log('[MQTT] WebSocket 已打开，发送 CONNECT 报文');
          this._socket.send({
            data: connectPacket,
            success: () => {
              console.log('[MQTT] CONNECT 报文已发送');
            },
            fail: (err) => {
              console.error('[MQTT] CONNECT 发送失败:', err);
              this._cleanup();
              reject(err);
            }
          });
        });

        this._socket.onMessage((res) => {
          this._handleMqttMessage(res.data);
        });

        this._socket.onError((err) => {
          console.error('[MQTT] WebSocket 错误:', err);
          if (!this._isManualDisconnect) {
            this._cleanup();
            this._tryReconnect();
          }
          reject(err);
        });

        this._socket.onClose(() => {
          console.log('[MQTT] WebSocket 已关闭');
          if (!this._isManualDisconnect) {
            this._cleanup();
            this._tryReconnect();
          }
        });

        // CONNACK 超时
        this._connackTimer = setTimeout(() => {
          console.error('[MQTT] CONNACK 超时');
          this._cleanup();
          reject(new Error('CONNACK timeout'));
        }, config.connectTimeout || 10000);

        // 临时保存回调
        this._resolveConnect = resolve;
        this._rejectConnect = reject;

      } catch (err) {
        console.error('[MQTT] 连接异常:', err);
        reject(err);
      }
    });
  }

  // ========== 构建 CONNECT 报文（支持变长编码） ==========
  _buildConnectPacket(config) {
    const protocolName = 'MQTT';
    const protocolLevel = 0x04; // v3.1.1
    const keepAlive = config.keepalive || 30;
    const clientId = config.clientId || 'wx_mini_' + Date.now();
    const username = config.username || '';
    const password = config.password || '';

    // 计算标志位
    let flags = 0x02; // Clean Session
    if (username) flags |= 0x80;
    if (password) flags |= 0x40;

    // 构建各部分字节数组
    const parts = [];

    // 协议名
    const protoNameBytes = this._stringToBytes(protocolName);
    parts.push(this._uint16ToBytes(protocolName.length));
    parts.push(protoNameBytes);

    // 协议级别
    parts.push(new Uint8Array([protocolLevel]));

    // 标志位
    parts.push(new Uint8Array([flags]));

    // Keep Alive
    parts.push(this._uint16ToBytes(keepAlive));

    // Client ID
    const clientIdBytes = this._stringToBytes(clientId);
    parts.push(this._uint16ToBytes(clientId.length));
    parts.push(clientIdBytes);

    // Username
    if (username) {
      const usernameBytes = this._stringToBytes(username);
      parts.push(this._uint16ToBytes(username.length));
      parts.push(usernameBytes);
    }

    // Password
    if (password) {
      const passwordBytes = this._stringToBytes(password);
      parts.push(this._uint16ToBytes(password.length));
      parts.push(passwordBytes);
    }

    // 计算总长度
    let totalLen = 0;
    for (const p of parts) {
      totalLen += p.length;
    }

    // 变长编码剩余长度
    const lenBytes = this._encodeRemainingLength(totalLen);

    // 固定头 0x10
    const header = new Uint8Array([0x10]);

    // 组装完整报文
    const full = new Uint8Array(1 + lenBytes.length + totalLen);
    let pos = 0;
    full.set(header, pos);
    pos += header.length;
    full.set(lenBytes, pos);
    pos += lenBytes.length;
    for (const p of parts) {
      full.set(p, pos);
      pos += p.length;
    }

    return full.buffer;
  }

  // ========== 工具函数 ==========
  _stringToBytes(str) {
    return new TextEncoder().encode(str);
  }

  _uint16ToBytes(num) {
    return new Uint8Array([(num >> 8) & 0xFF, num & 0xFF]);
  }

  _encodeRemainingLength(len) {
    const bytes = [];
    do {
      let digit = len % 128;
      len = Math.floor(len / 128);
      if (len > 0) digit |= 0x80;
      bytes.push(digit);
    } while (len > 0);
    return new Uint8Array(bytes);
  }

  // ========== 处理 MQTT 消息 ==========
  _handleMqttMessage(data) {
    const buffer = data instanceof ArrayBuffer ? new Uint8Array(data) : data;
    if (!buffer || buffer.length === 0) return;
  
    const cmd = buffer[0] >> 4;
    const flags = buffer[0] & 0x0F; // 获取标志位
  
    // 解析剩余长度（变长编码）
    let pos = 1;
    let multiplier = 1;
    let remainingLen = 0;
    let digit;
    do {
      if (pos >= buffer.length) return;
      digit = buffer[pos++];
      remainingLen += (digit & 0x7F) * multiplier;
      multiplier *= 128;
      if (multiplier > 128 * 128 * 128) return;
    } while ((digit & 0x80) !== 0);
  
    if (cmd === 0x02) { // CONNACK
      // ... 原有代码保持不变 ...
      if (remainingLen >= 2 && buffer.length >= pos + 2) {
        const sessionPresent = buffer[pos] & 0x01;
        const returnCode = buffer[pos + 1];
        console.log(`[MQTT] CONNACK: sessionPresent=${sessionPresent}, returnCode=${returnCode}`);
        if (this._connackTimer) {
          clearTimeout(this._connackTimer);
          this._connackTimer = null;
        }
        if (returnCode === 0) {
          this._connected = true;
          app.globalData.mqttConnected = true;
          this._reconnectAttempts = 0;
          this._notifyConnectionChange(true);
          this._subscribe();
          this._startKeepalive();
          if (this._resolveConnect) {
            this._resolveConnect();
            this._resolveConnect = null;
            this._rejectConnect = null;
          }
        } else {
          const errMsg = `CONNACK rejected, code: ${returnCode}`;
          console.error('[MQTT]', errMsg);
          if (this._rejectConnect) {
            this._rejectConnect(new Error(errMsg));
            this._resolveConnect = null;
            this._rejectConnect = null;
          }
          this._cleanup();
        }
      }
      return;
    }
  
    if (cmd === 0x03) { // PUBLISH
      if (remainingLen < 2) return;
      // 读取 Topic 长度
      const topicLen = (buffer[pos] << 8) | buffer[pos + 1];
      pos += 2;
      if (pos + topicLen > buffer.length) return;
      const topic = new TextDecoder().decode(buffer.slice(pos, pos + topicLen));
      pos += topicLen;
  
      // 判断 QoS（从 flags 中提取，bit1-0 表示 QoS）
      const qos = (flags & 0x06) >> 1;
      if (qos > 0) {
        // 跳过 Message ID（2 字节）
        if (pos + 2 > buffer.length) return;
        pos += 2;
      }
  
      // 剩余部分即为 Payload
      const payload = buffer.slice(pos);
      const payloadStr = new TextDecoder().decode(payload);
      console.log('[MQTT] 收到 PUBLISH 主题:', topic, 'payload:', payloadStr);
  
      try {
        const data = JSON.parse(payloadStr);
        if (topic === this._topic) {
          this._handleDownMessage(data);
        } else {
          console.warn('[MQTT] 未知主题:', topic);
        }
      } catch (e) {
        console.error('[MQTT] JSON解析失败:', e);
      }
      return;
    }
  
    if (cmd === 0x09) { // SUBACK（订阅确认）
      console.log('[MQTT] SUBACK 收到');
      return;
    }
  
    if (cmd === 0x0D) { // PINGRESP
      console.log('[MQTT] PINGRESP 收到');
      this._pendingPing = false;
      return;
    }
  
    console.log('[MQTT] 收到未处理命令:', cmd);
  }

  // ========== 订阅 ==========
  _subscribe() {
    if (!this._socket || !this._connected) {
      console.warn('[MQTT] 无法订阅：未连接');
      return;
    }
    const topic = this._topic;
    if (!topic) {
      console.warn('[MQTT] 未配置订阅主题');
      return;
    }

    const packetId = Math.floor(Math.random() * 65535) + 1;
    const topicBytes = new TextEncoder().encode(topic);
    // 固定头 0x82 (SUBSCRIBE QoS=1)
    const remainingLen = 2 + 2 + topicBytes.length + 1; // packetId + topic len + topic + QoS
    const lenBytes = this._encodeRemainingLength(remainingLen);
    const buffer = new Uint8Array(1 + lenBytes.length + remainingLen);
    let pos = 0;
    buffer[pos++] = 0x82;
    buffer.set(lenBytes, pos);
    pos += lenBytes.length;
    // packetId
    buffer[pos++] = (packetId >> 8) & 0xFF;
    buffer[pos++] = packetId & 0xFF;
    // topic len
    buffer[pos++] = (topicBytes.length >> 8) & 0xFF;
    buffer[pos++] = topicBytes.length & 0xFF;
    // topic
    buffer.set(topicBytes, pos);
    pos += topicBytes.length;
    // QoS = 1
    buffer[pos++] = 0x01;

    this._socket.send({
      data: buffer.buffer,
      success: () => {
        console.log('[MQTT] SUBSCRIBE 发送成功:', topic);
      },
      fail: (err) => {
        console.error('[MQTT] SUBSCRIBE 发送失败:', err);
      }
    });
  }

  _subscribeDown() {
    const topic = this._topics.down;
    if (!topic) return;
    this._client.subscribe(topic, { qos: 1 }, (err) => {
      if (err) {
        console.error('[MQTT] 订阅失败:', topic, err);
      } else {
        console.log('[MQTT] ✅ 订阅成功:', topic);  // ← 应该看到这条日志
      }
    });
  }
  /**
   * 发布消息到指定主题（用于发送命令）
   * @param {string} topic   - 目标主题（通常为上行主题）
   * @param {object} data    - 要发送的数据对象
   * @param {number} qos     - QoS 等级（默认 1）
   * @returns {Promise}
   */
  publish(topic, data, qos = 1) {
    return new Promise((resolve, reject) => {
      if (!this._socket || !this._connected) {
        reject(new Error('MQTT 未连接'));
        return;
      }

      const payload = JSON.stringify(data);
      const payloadBytes = new TextEncoder().encode(payload);
      const topicBytes = new TextEncoder().encode(topic);

      // 生成 Packet ID
      const packetId = qos > 0 ? Math.floor(Math.random() * 65535) + 1 : 0;

      // 固定头：0x30 (PUBLISH)，根据 qos 设置对应 bit
      let fixedHeader = 0x30;
      if (qos === 1) fixedHeader |= 0x02;
      if (qos === 2) fixedHeader |= 0x04;

      // 计算剩余长度
      let remainingLen = 2 + topicBytes.length + (qos > 0 ? 2 : 0) + payloadBytes.length;
      const lenBytes = this._encodeRemainingLength(remainingLen);

      // 组装报文
      const buffer = new Uint8Array(1 + lenBytes.length + remainingLen);
      let pos = 0;
      buffer[pos++] = fixedHeader;
      buffer.set(lenBytes, pos);
      pos += lenBytes.length;

      // Topic 长度 + Topic
      buffer[pos++] = (topicBytes.length >> 8) & 0xFF;
      buffer[pos++] = topicBytes.length & 0xFF;
      buffer.set(topicBytes, pos);
      pos += topicBytes.length;

      // Packet ID (QoS > 0)
      if (qos > 0) {
        buffer[pos++] = (packetId >> 8) & 0xFF;
        buffer[pos++] = packetId & 0xFF;
      }

      // Payload
      buffer.set(payloadBytes, pos);
      pos += payloadBytes.length;

      // 发送
      this._socket.send({
        data: buffer.buffer,
        success: () => {
          console.log('[MQTT] PUBLISH 发送成功:', topic, data);
          resolve();
        },
        fail: (err) => {
          console.error('[MQTT] PUBLISH 发送失败:', err);
          reject(err);
        }
      });
    });
  }

  // 添加上行主题 getter
  get upTopic() {
    return this._config.topics.up;
  }
  // ========== 数据处理 ==========
  _handleDownMessage(data) {
    // 兼容两种数据格式：
    // 格式1: { services: [ { service_id, properties } ] }
    // 格式2: { content: { services: [ { service_id, properties } ] } }
    let services = null;
    if (data.services && Array.isArray(data.services)) {
      services = data.services;
    } else if (data.content && data.content.services && Array.isArray(data.content.services)) {
      services = data.content.services;
    }
  
    if (!services || services.length === 0) {
      console.warn('[MQTT] 未找到有效 services 字段', data);
      return;
    }
  
    // 取第一个 service（通常只有一个）
    const svc = services[0];
    let props = svc.properties;
    if (!props) {
      console.warn('[MQTT] 未找到有效 properties 字段', svc);
      return;
    }
  
    // 转换字符串数值为数字
    const normalized = {};
    for (const key in props) {
      const val = props[key];
      if (typeof val === 'string') {
        const num = parseFloat(val);
        if (!isNaN(num) && val.trim() !== '') {
          normalized[key] = num;
        } else {
          normalized[key] = val;
        }
      } else {
        normalized[key] = val;
      }
    }
  
    if (normalized.temp !== undefined && normalized.temp === -999) {
      normalized.temp = null;
    }
  
    app.globalData.latestData = normalized;
    app.globalData.latestTimestamp = Date.now();
  
    if (this._onDataReceived) {
      this._onDataReceived(normalized);
    }
  }

  // ========== 心跳 ==========
  _startKeepalive() {
    this._stopKeepalive();
    const interval = (this._config.keepalive || 30) * 1000;
    // 每 1.5 个 keepalive 间隔发送 ping
    this._pingTimer = setInterval(() => {
      if (this._connected && this._socket) {
        const pingBuf = new Uint8Array([0xC0, 0x00]);
        this._socket.send({
          data: pingBuf.buffer,
          success: () => {
            console.log('[MQTT] PINGREQ 发送');
          },
          fail: (err) => {
            console.error('[MQTT] PINGREQ 发送失败:', err);
          }
        });
        this._pendingPing = true;
        setTimeout(() => {
          if (this._pendingPing) {
            console.warn('[MQTT] PINGRESP 超时，断开');
            this._cleanup();
            this._tryReconnect();
          }
        }, interval / 2);
      }
    }, interval);
  }

  _stopKeepalive() {
    if (this._pingTimer) {
      clearInterval(this._pingTimer);
      this._pingTimer = null;
    }
    this._pendingPing = false;
  }

  // ========== 清理 ==========
  _cleanup() {
    this._stopKeepalive();
    if (this._connackTimer) {
      clearTimeout(this._connackTimer);
      this._connackTimer = null;
    }
    if (this._socket) {
      try {
        this._socket.close();
      } catch (e) {}
      this._socket = null;
    }
    this._connected = false;
    app.globalData.mqttConnected = false;
    this._notifyConnectionChange(false);
    if (this._resolveConnect) {
      this._resolveConnect = null;
      this._rejectConnect = null;
    }
  }

  // ========== 断开 ==========
  disconnect() {
    this._isManualDisconnect = true;
    this._cleanup();
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    this._reconnectAttempts = this._config.maxReconnectAttempts;
  }

  // ========== 重连 ==========
  _tryReconnect() {
    if (this._isManualDisconnect) return;
    if (this._reconnectAttempts >= this._config.maxReconnectAttempts) {
      console.log('[MQTT] 达到最大重连次数，停止');
      wx.showToast({
        title: '网络异常，请检查 Wi-Fi',
        icon: 'none',
        duration: 3000
      });
      return;
    }
    this._reconnectAttempts++;
    const delay = this._config.reconnectInterval || 3000;
    console.log(`[MQTT] ${delay/1000}s 后第 ${this._reconnectAttempts} 次重连...`);
    this._reconnectTimer = setTimeout(() => {
      if (this._connected) return;
      this.connect().catch(err => {
        console.error('[MQTT] 重连失败:', err);
      });
    }, delay);
  }

  // ========== 回调 ==========
  onDataReceived(callback) {
    this._onDataReceived = callback;
  }

  onConnectionChange(callback) {
    this._onConnectionChange = callback;
  }

  _notifyConnectionChange(connected) {
    if (this._onConnectionChange) {
      this._onConnectionChange(connected);
    }
  }

  get isConnected() {
    return this._connected;
  }
}

module.exports = MQTTManager;