// utils/mqtt.js - 原生 WebSocket MQTT 客户端（适配华为云 IoTDA）
// ============ TextEncoder / TextDecoder Polyfill ============
if (typeof TextEncoder === 'undefined') {
    global.TextEncoder = function() {
      this.encode = function(str) {
        const utf8 = unescape(encodeURIComponent(str));
        const buf = new Uint8Array(utf8.length);
        for (let i = 0; i < utf8.length; i++) {
          buf[i] = utf8.charCodeAt(i);
        }
        return buf;
      };
    };
  }
  
  if (typeof TextDecoder === 'undefined') {
    global.TextDecoder = function() {
      this.decode = function(buffer) {
        const arr = buffer instanceof ArrayBuffer ? new Uint8Array(buffer) : buffer;
        let str = '';
        for (let i = 0; i < arr.length; i++) {
          str += String.fromCharCode(arr[i]);
        }
        try {
          return decodeURIComponent(escape(str));
        } catch (e) {
          return str;
        }
      };
    };
  }
  // ============ Polyfill 结束 ============
  
  // ❌ 删除这一行：const app = getApp();
  
  class MQTTManager {
    constructor() {
      // ✅ 在构造函数内获取 app 实例
      this._app = getApp();
      if (!this._app) {
        console.error('[MQTT] getApp() 失败，请确保小程序已启动');
        // 抛出错误或降级处理
      }
  
      this._socket = null;
      this._connected = false;
      this._reconnectAttempts = 0;
      this._reconnectTimer = null;
      this._onDataReceivedCallbacks = [];
      this._onConnectionChangeCallbacks = [];
      this._config = this._app ? this._app.globalData.mqtt : null;
      this._topic = this._config ? this._config.topics.down : null;
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
        if (!this._config) {
          reject(new Error('配置无效，请检查 app.globalData.mqtt'));
          return;
        }
  
        const config = this._config;
        let url = config.brokerUrl;
        if (!url.includes('/mqtt') && !url.endsWith('/')) {
          url = url + '/mqtt';
        }
  
        console.log('[MQTT] 连接华为云:', url);
        console.log('[MQTT] Client ID:', config.clientId);
  
        const connectPacket = this._buildConnectPacket(config);
        if (!connectPacket) {
          reject(new Error('构建 CONNECT 报文失败'));
          return;
        }
  
        try {
          this._socket = wx.connectSocket({
            url: url,
            protocols: ['mqttv3.1'],
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
  
          this._connackTimer = setTimeout(() => {
            console.error('[MQTT] CONNACK 超时');
            this._cleanup();
            reject(new Error('CONNACK timeout'));
          }, config.connectTimeout || 10000);
  
          this._resolveConnect = resolve;
          this._rejectConnect = reject;
  
        } catch (err) {
          console.error('[MQTT] 连接异常:', err);
          reject(err);
        }
      });
    }
  
    // ========== 构建 CONNECT 报文 ==========
    _buildConnectPacket(config) {
      const protocolName = 'MQTT';
      const protocolLevel = 0x04;
      const keepAlive = config.keepalive || 30;
      const clientId = config.clientId || 'wx_mini_' + Date.now();
      const username = config.username || '';
      const password = config.password || '';
  
      let flags = 0x02;
      if (username) flags |= 0x80;
      if (password) flags |= 0x40;
  
      const parts = [];
      const protoNameBytes = this._stringToBytes(protocolName);
      parts.push(this._uint16ToBytes(protocolName.length));
      parts.push(protoNameBytes);
      parts.push(new Uint8Array([protocolLevel]));
      parts.push(new Uint8Array([flags]));
      parts.push(this._uint16ToBytes(keepAlive));
  
      const clientIdBytes = this._stringToBytes(clientId);
      parts.push(this._uint16ToBytes(clientId.length));
      parts.push(clientIdBytes);
  
      if (username) {
        const usernameBytes = this._stringToBytes(username);
        parts.push(this._uint16ToBytes(username.length));
        parts.push(usernameBytes);
      }
      if (password) {
        const passwordBytes = this._stringToBytes(password);
        parts.push(this._uint16ToBytes(password.length));
        parts.push(passwordBytes);
      }
  
      let totalLen = 0;
      for (const p of parts) totalLen += p.length;
  
      const lenBytes = this._encodeRemainingLength(totalLen);
      const header = new Uint8Array([0x10]);
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
      const flags = buffer[0] & 0x0F;
  
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
            if (this._app) {
              this._app.globalData.mqttConnected = true;
            }
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
        if (!this._connected) {
          this._connected = true;
          if (this._app) {
            this._app.globalData.mqttConnected = true;
          }
          this._notifyConnectionChange(true);
        }
        if (remainingLen < 2) return;
        const topicLen = (buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
        if (pos + topicLen > buffer.length) return;
        const topic = new TextDecoder().decode(buffer.slice(pos, pos + topicLen));
        pos += topicLen;
  
        const qos = (flags & 0x06) >> 1;
        if (qos > 0) {
          if (pos + 2 > buffer.length) return;
          pos += 2;
        }
  
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
  
      if (cmd === 0x09) { // SUBACK
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
      const remainingLen = 2 + 2 + topicBytes.length + 1;
      const lenBytes = this._encodeRemainingLength(remainingLen);
      const buffer = new Uint8Array(1 + lenBytes.length + remainingLen);
      let pos = 0;
      buffer[pos++] = 0x82;
      buffer.set(lenBytes, pos);
      pos += lenBytes.length;
      buffer[pos++] = (packetId >> 8) & 0xFF;
      buffer[pos++] = packetId & 0xFF;
      buffer[pos++] = (topicBytes.length >> 8) & 0xFF;
      buffer[pos++] = topicBytes.length & 0xFF;
      buffer.set(topicBytes, pos);
      pos += topicBytes.length;
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
  
    // ========== 发布 ==========
    publish(topic, data, qos = 1) {
      return new Promise((resolve, reject) => {
        if (!this._connected || !this._socket) {
          reject(new Error('MQTT 未连接'));
          return;
        }
  
        const payload = JSON.stringify(data);
        const payloadBytes = new TextEncoder().encode(payload);
        const topicBytes = new TextEncoder().encode(topic);
  
        const packetId = qos > 0 ? Math.floor(Math.random() * 65535) + 1 : 0;
        let fixedHeader = 0x30;
        if (qos === 1) fixedHeader |= 0x02;
        if (qos === 2) fixedHeader |= 0x04;
  
        let remainingLen = 2 + topicBytes.length + (qos > 0 ? 2 : 0) + payloadBytes.length;
        const lenBytes = this._encodeRemainingLength(remainingLen);
  
        const buffer = new Uint8Array(1 + lenBytes.length + remainingLen);
        let pos = 0;
        buffer[pos++] = fixedHeader;
        buffer.set(lenBytes, pos);
        pos += lenBytes.length;
  
        buffer[pos++] = (topicBytes.length >> 8) & 0xFF;
        buffer[pos++] = topicBytes.length & 0xFF;
        buffer.set(topicBytes, pos);
        pos += topicBytes.length;
  
        if (qos > 0) {
          buffer[pos++] = (packetId >> 8) & 0xFF;
          buffer[pos++] = packetId & 0xFF;
        }
  
        buffer.set(payloadBytes, pos);
        pos += payloadBytes.length;
  
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
  
    // ========== 下行数据处理 ==========
    _handleDownMessage(data) {
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
  
      const svc = services[0];
      let props = svc.properties;
      if (!props) {
        console.warn('[MQTT] 未找到有效 properties 字段', svc);
        return;
      }
  
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
  
      if (this._app) {
        this._app.globalData.latestData = normalized;
        this._app.globalData.latestTimestamp = Date.now();
      }
  
      if (this._onDataReceivedCallbacks.length) {
        for (const cb of this._onDataReceivedCallbacks) {
          try { cb(normalized); } catch (e) {}
        }
      }
    }
  
    // ========== 心跳 ==========
    _startKeepalive() {
      this._stopKeepalive();
      const interval = (this._config.keepalive || 30) * 1000;
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
      if (this._app) {
        this._app.globalData.mqttConnected = false;
      }
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
  
    // ========== 回调注册（多回调） ==========
    onDataReceived(callback) {
      if (callback && typeof callback === 'function') {
        this._onDataReceivedCallbacks.push(callback);
      }
    }
  
    onConnectionChange(callback) {
      if (callback && typeof callback === 'function') {
        this._onConnectionChangeCallbacks.push(callback);
      }
    }
  
    _notifyConnectionChange(connected) {
      for (const cb of this._onConnectionChangeCallbacks) {
        try { cb(connected); } catch (e) {}
      }
    }
  
    // ========== 属性 ==========
    get isConnected() {
      return this._connected;
    }
  
    get upTopic() {
      return this._config ? this._config.topics.up : null;
    }
  }
  
  module.exports = MQTTManager;