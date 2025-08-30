const net = require('net');

let receivedCount = 0;
let connectionSuccess = false;

// Simple StompFrame utilities (same as others)
class StompFrame {
  constructor(command, headers = {}, body = '') {
    this.command = command;
    this.headers = headers;
    this.body = body;
  }
}

class StompFrameUtils {
  static serialize(frame) {
    let result = frame.command + '\n';
    for (const [key, value] of Object.entries(frame.headers)) {
      result += `${key}:${value}\n`;
    }
    result += '\n' + frame.body + '\0';
    return result;
  }
  
  static parse(data) {
    const parts = data.split('\n\n');
    if (parts.length < 2) return null;
    
    const headerLines = parts[0].split('\n');
    const command = headerLines[0];
    const headers = {};
    
    for (let i = 1; i < headerLines.length; i++) {
      const [key, value] = headerLines[i].split(':');
      if (key && value) headers[key] = value;
    }
    
    const body = parts[1].replace(/\0$/, '');
    return new StompFrame(command, headers, body);
  }
}

// Simple StompClient for testing
class StompClient {
  constructor(host, port, login, passcode) {
    this.host = host;
    this.port = port;
    this.login = login;
    this.passcode = passcode;
    this.socket = null;
    this.connected = false;
    this.connectionListener = null;
    this.errorListener = null;
    this.subscriptions = new Map();
  }
  
  setConnectionListener(listener) {
    this.connectionListener = listener;
  }
  
  setErrorListener(listener) {
    this.errorListener = listener;
  }
  
  connect() {
    return new Promise((resolve, reject) => {
      this.socket = net.createConnection(this.port, this.host);
      
      this.socket.on('connect', () => {
        const connectFrame = new StompFrame('CONNECT', {
          'accept-version': '1.2',
          'host': this.host,
          'login': this.login,
          'passcode': this.passcode
        });
        this.socket.write(StompFrameUtils.serialize(connectFrame));
      });
      
      this.socket.on('data', (data) => {
        const frame = StompFrameUtils.parse(data.toString());
        if (frame) {
          if (frame.command === 'CONNECTED') {
            this.connected = true;
            if (this.connectionListener) {
              this.connectionListener(true, 'Connected');
            }
            resolve();
          } else if (frame.command === 'MESSAGE') {
            const destination = frame.headers.destination;
            const subscription = frame.headers.subscription;
            const listener = this.subscriptions.get(subscription);
            if (listener) {
              listener(destination, frame.body, frame.headers);
            }
          } else if (frame.command === 'ERROR') {
            if (this.errorListener) {
              this.errorListener(frame.headers.message || 'Unknown error', frame.body);
            }
          }
        }
      });
      
      this.socket.on('error', (error) => {
        if (this.connectionListener) {
          this.connectionListener(false, error.message);
        }
        reject(error);
      });
    });
  }
  
  subscribe(destination, listener) {
    const subscriptionId = 'sub-' + Date.now();
    this.subscriptions.set(subscriptionId, listener);
    
    const subscribeFrame = new StompFrame('SUBSCRIBE', {
      'id': subscriptionId,
      'destination': destination
    });
    this.socket.write(StompFrameUtils.serialize(subscribeFrame));
    return subscriptionId;
  }
  
  send(destination, body) {
    const sendFrame = new StompFrame('SEND', {
      'destination': destination
    }, body);
    this.socket.write(StompFrameUtils.serialize(sendFrame));
  }
  
  unsubscribe(subscriptionId) {
    this.subscriptions.delete(subscriptionId);
    const unsubscribeFrame = new StompFrame('UNSUBSCRIBE', {
      'id': subscriptionId
    });
    this.socket.write(StompFrameUtils.serialize(unsubscribeFrame));
  }
  
  disconnect() {
    if (this.socket) {
      const disconnectFrame = new StompFrame('DISCONNECT');
      this.socket.write(StompFrameUtils.serialize(disconnectFrame));
      this.socket.end();
    }
  }
}

async function main() {
  try {
    console.log('🔌 [TypeScript] Creating client with credentials...');
    const client = new StompClient('localhost', 61613, 'ts_client', 'test123');
    
    console.log('🔌 [TypeScript] Connecting to TypeScript server...');
    
    client.setConnectionListener((connected, reason) => {
      if (connected) {
        console.log('✅ [TypeScript] Connected to TypeScript server!');
        connectionSuccess = true;
      } else {
        console.log(`❌ [TypeScript] Connection failed: ${reason}`);
      }
    });
    
    client.setErrorListener((message, details) => {
      console.log(`❌ [TypeScript] Error: ${message} - ${details}`);
    });
    
    await client.connect();
    
    // Give connection time to fully establish
    await new Promise(resolve => setTimeout(resolve, 500));
    
    if (!connectionSuccess) {
      console.log('❌ [TypeScript] Failed to establish connection');
      return;
    }
    
    // Subscribe to test queue
    console.log('📡 [TypeScript] Subscribing to /queue/cross-test...');
    const subId = client.subscribe('/queue/cross-test', (dest, body, headers) => {
      console.log(`📨 [TypeScript] Received: ${body} on ${dest}`);
      receivedCount++;
    });
    console.log(`✅ [TypeScript] Subscribed with ID: ${subId}`);
    
    // Send test messages
    console.log('📤 [TypeScript] Sending messages to TypeScript server...');
    client.send('/queue/cross-test', 'Hello TypeScript from TypeScript!');
    client.send('/queue/cross-test', 'Self-test message 1');
    client.send('/queue/cross-test', 'Self-test message 2');
    
    // Wait for any responses
    console.log('⏳ [TypeScript] Waiting for message processing...');
    await new Promise(resolve => setTimeout(resolve, 3000));
    
    // Test different destinations
    client.send('/topic/broadcast', 'TypeScript broadcast message');
    
    await new Promise(resolve => setTimeout(resolve, 2000));
    
    // Cleanup
    console.log('🧹 [TypeScript] Cleaning up...');
    client.unsubscribe(subId);
    client.disconnect();
    
    console.log(`📊 [TypeScript] Test Results:`);
    console.log(`   Connection: ${connectionSuccess ? 'SUCCESS' : 'FAILED'}`);
    console.log(`   Messages received: ${receivedCount}`);
    console.log(`   Status: ${connectionSuccess ? 'PASSED' : 'FAILED'}`);
    
    console.log('🎉 [TypeScript] Self-test completed successfully!');
    
  } catch (error) {
    console.error('❌ [TypeScript] Test failed with error:', error);
  }
}

main().catch(error => {
  console.error('❌ [TypeScript] Unhandled error:', error);
});
