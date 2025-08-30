import * as net from 'net';
import { StompFrame, StompFrameUtils } from './StompFrame.js';

// Listener interfaces
export type StompMessageListener = (destination: string, body: string, headers: Record<string, string>) => void;
export type StompConnectionListener = (connected: boolean, reason: string) => void;
export type StompErrorListener = (message: string, details: string) => void;

// Subscription holder
export interface StompSubscription {
  subscriptionId: string;
  destination: string;
  listener: StompMessageListener;
}

// STOMP Client Implementation
export class StompClient {
  private readonly host: string;
  private readonly port: number;
  private readonly login?: string;
  private readonly passcode?: string;
  
  private socket?: net.Socket;
  private connected = false;
  private connecting = false;
  private sessionId?: string;
  
  // Listeners
  private connectionListener?: StompConnectionListener;
  private errorListener?: StompErrorListener;
  
  // Subscriptions management
  private readonly subscriptions = new Map<string, StompSubscription>();
  private subscriptionCounter = 0;
  
  constructor(host: string, port: number, login?: string, passcode?: string) {
    this.host = host;
    this.port = port;
    this.login = login;
    this.passcode = passcode;
  }
  
  // Connection management
  async connect(): Promise<void> {
    if (this.connected || this.connecting) {
      return;
    }
    
    this.connecting = true;
    
    return new Promise((resolve, reject) => {
      try {
        this.socket = new net.Socket();
        
        this.socket.connect(this.port, this.host, () => {
          console.log(`Connected to STOMP server at ${this.host}:${this.port}`);
          
          // Send CONNECT frame
          const connectFrame = StompFrameUtils.connect(this.host, this.login, this.passcode);
          this.sendFrame(connectFrame);
          
          // Start frame reader
          this.readFrames();
          
          // Wait for CONNECTED response
          const startTime = Date.now();
          const checkConnection = () => {
            if (this.connected) {
              resolve();
            } else if (this.connecting && (Date.now() - startTime) < 5000) {
              setTimeout(checkConnection, 10);
            } else {
              this.connecting = false;
              this.cleanup();
              reject(new Error('Connection timeout - no CONNECTED frame received'));
            }
          };
          checkConnection();
        });
        
        this.socket.on('error', (err) => {
          this.connecting = false;
          this.notifyConnectionListener(false, `Connection failed: ${err.message}`);
          reject(new Error(`Failed to connect to STOMP server: ${err.message}`));
        });
        
      } catch (error) {
        this.connecting = false;
        this.notifyConnectionListener(false, `Connection failed: ${error}`);
        reject(new Error(`Failed to connect to STOMP server: ${error}`));
      }
    });
  }
  
  disconnect(): void {
    if (!this.connected) return;
    
    try {
      // Send DISCONNECT frame
      this.sendFrame(StompFrameUtils.disconnect());
      setTimeout(() => this.cleanup(), 100); // Give server time to process
    } catch (error) {
      // Log error in production
      this.cleanup();
    }
  }
  
  private cleanup(): void {
    this.connected = false;
    this.connecting = false;
    
    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
    }
    
    this.subscriptions.clear();
    this.notifyConnectionListener(false, 'Disconnected');
  }
  
  // Subscription management
  subscribe(destination: string, listener: StompMessageListener): string {
    if (!this.connected) {
      throw new Error('Not connected to STOMP server');
    }
    
    const subscriptionId = `sub-${++this.subscriptionCounter}`;
    const subscription: StompSubscription = {
      subscriptionId,
      destination,
      listener
    };
    
    this.subscriptions.set(subscriptionId, subscription);
    
    const subscribeFrame = StompFrameUtils.subscribe(destination, subscriptionId);
    this.sendFrame(subscribeFrame);
    
    console.log(`Subscribed to ${destination} with subscription ID: ${subscriptionId}`);
    
    return subscriptionId;
  }
  
  unsubscribe(subscriptionId: string): void {
    if (!this.connected) return;
    
    const subscription = this.subscriptions.get(subscriptionId);
    if (subscription) {
      this.subscriptions.delete(subscriptionId);
      const unsubscribeFrame = StompFrameUtils.unsubscribe(subscriptionId);
      this.sendFrame(unsubscribeFrame);
      
      console.log(`Unsubscribed from ${subscription.destination}`);
    }
  }
  
  // Message sending
  send(destination: string, message: string): void {
    if (!this.connected) {
      throw new Error('Not connected to STOMP server');
    }
    
    const sendFrame = StompFrameUtils.send(destination, message);
    this.sendFrame(sendFrame);
    
    console.log(`Sent message to ${destination}: ${message}`);
  }
  
  // Frame processing
  private sendFrame(frame: StompFrame): void {
    if (this.socket && !this.socket.destroyed) {
      const serialized = StompFrameUtils.serialize(frame);
      this.socket.write(serialized, 'utf8');
    }
  }
  
  private readFrames(): void {
    if (!this.socket) return;
    
    let buffer = '';
    
    this.socket.on('data', (data) => {
      buffer += data.toString('utf8');
      
      // Process complete frames (ending with null terminator)
      let nullIndex;
      while ((nullIndex = buffer.indexOf('\0')) !== -1) {
        const frameData = buffer.substring(0, nullIndex);
        buffer = buffer.substring(nullIndex + 1);
        
        if (frameData.length > 0) {
          const frame = StompFrameUtils.parse(frameData);
          if (frame) {
            this.handleIncomingFrame(frame);
          }
        }
      }
    });
    
    this.socket.on('close', () => {
      if (this.connected) {
        console.error('Connection lost');
        this.notifyConnectionListener(false, 'Connection lost');
      }
      this.cleanup();
    });
    
    this.socket.on('error', (err) => {
      if (this.connected) {
        console.error(`Frame reading error: ${err.message}`);
        this.notifyConnectionListener(false, `Connection lost: ${err.message}`);
      }
      this.cleanup();
    });
  }
  
  private handleIncomingFrame(frame: StompFrame): void {
    switch (frame.command) {
      case 'CONNECTED':
        this.handleConnected(frame);
        break;
      case 'MESSAGE':
        this.handleMessage(frame);
        break;
      case 'RECEIPT':
        this.handleReceipt(frame);
        break;
      case 'ERROR':
        this.handleError(frame);
        break;
      default:
        console.log(`Received unknown frame: Command '${frame.command}' not supported`);
    }
  }
  
  private handleConnected(frame: StompFrame): void {
    this.connected = true;
    this.connecting = false;
    this.sessionId = frame.headers['session'];
    
    console.log(`Successfully connected to STOMP server. Session: ${this.sessionId}`);
    this.notifyConnectionListener(true, 'Connected');
  }
  
  private handleMessage(frame: StompFrame): void {
    const destination = frame.headers['destination'];
    const subscriptionId = frame.headers['subscription'];
    
    if (destination && subscriptionId) {
      const subscription = this.subscriptions.get(subscriptionId);
      if (subscription && subscription.listener) {
        try {
          subscription.listener(destination, frame.body, frame.headers);
        } catch (error) {
          console.error(`Error in message listener: ${error}`);
        }
      }
    }
  }
  
  private handleReceipt(frame: StompFrame): void {
    const receiptId = frame.headers['receipt-id'];
    console.log(`Received receipt: ${receiptId}`);
  }
  
  private handleError(frame: StompFrame): void {
    const message = frame.headers['message'];
    console.error(`STOMP Error: ${message}`);
    console.error(`Details: ${frame.body}`);
    
    if (this.errorListener) {
      this.errorListener(message || 'Unknown error', frame.body);
    }
  }
  
  // Listeners
  setConnectionListener(listener: StompConnectionListener): void {
    this.connectionListener = listener;
  }
  
  setErrorListener(listener: StompErrorListener): void {
    this.errorListener = listener;
  }
  
  private notifyConnectionListener(connected: boolean, reason: string): void {
    if (this.connectionListener) {
      try {
        this.connectionListener(connected, reason);
      } catch (error) {
        console.error(`Error in connection listener: ${error}`);
      }
    }
  }
  
  // Status methods
  isConnected(): boolean {
    return this.connected;
  }
  
  getSessionId(): string | undefined {
    return this.sessionId;
  }
  
  getActiveSubscriptions(): Set<string> {
    return new Set(this.subscriptions.keys());
  }
  
  // Shutdown
  shutdown(): void {
    this.disconnect();
  }
}
