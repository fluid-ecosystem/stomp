import * as net from 'net';
import { randomUUID } from 'crypto';
import { StompFrame, StompFrameUtils } from './StompFrame.js';

// Client session management
export class StompSession {
  private readonly sessionId: string;
  private readonly socket: net.Socket;
  readonly subscriptions = new Map<string, string>();
  private connected = true;
  
  constructor(sessionId: string, socket: net.Socket) {
    this.sessionId = sessionId;
    this.socket = socket;
  }
  
  getSessionId(): string {
    return this.sessionId;
  }
  
  sendFrame(frame: StompFrame): void {
    if (this.connected && !this.socket.destroyed) {
      const serialized = StompFrameUtils.serialize(frame);
      this.socket.write(serialized, 'utf8');
    }
  }
  
  subscribe(subscriptionId: string, destination: string): void {
    this.subscriptions.set(subscriptionId, destination);
  }
  
  unsubscribe(subscriptionId: string): void {
    this.subscriptions.delete(subscriptionId);
  }
  
  isSubscribedTo(destination: string): boolean {
    return Array.from(this.subscriptions.values()).includes(destination);
  }
  
  getSubscriptionId(destination: string): string | undefined {
    for (const [subId, dest] of this.subscriptions.entries()) {
      if (dest === destination) {
        return subId;
      }
    }
    return undefined;
  }
  
  close(): void {
    this.connected = false;
    if (!this.socket.destroyed) {
      this.socket.destroy();
    }
  }
  
  isConnected(): boolean {
    return this.connected && !this.socket.destroyed;
  }
}

// Message broker for handling destinations
export class MessageBroker {
  private readonly destinationSubscribers = new Map<string, Set<StompSession>>();
  
  subscribe(destination: string, session: StompSession): void {
    if (!this.destinationSubscribers.has(destination)) {
      this.destinationSubscribers.set(destination, new Set());
    }
    this.destinationSubscribers.get(destination)!.add(session);
  }
  
  unsubscribe(destination: string, session: StompSession): void {
    const subscribers = this.destinationSubscribers.get(destination);
    if (subscribers) {
      subscribers.delete(session);
      if (subscribers.size === 0) {
        this.destinationSubscribers.delete(destination);
      }
    }
  }
  
  broadcast(destination: string, body: string): void {
    const subscribers = this.destinationSubscribers.get(destination);
    if (subscribers) {
      const messageId = randomUUID();
      
      // Remove disconnected sessions
      const disconnectedSessions: StompSession[] = [];
      for (const session of subscribers) {
        if (!session.isConnected()) {
          disconnectedSessions.push(session);
        }
      }
      disconnectedSessions.forEach(session => subscribers.delete(session));
      
      // Send message to all connected subscribers
      for (const session of subscribers) {
        const subscriptionId = session.getSubscriptionId(destination);
        if (subscriptionId) {
          const message = StompFrameUtils.message(destination, messageId, subscriptionId, body);
          session.sendFrame(message);
        }
      }
    }
  }
  
  removeSession(session: StompSession): void {
    for (const subscribers of this.destinationSubscribers.values()) {
      subscribers.delete(session);
    }
  }
}

// Client handler for processing STOMP commands
export class StompClientHandler {
  private readonly clientSocket: net.Socket;
  private readonly session: StompSession;
  private readonly broker: MessageBroker;
  private readonly sessionCloseCallback: (session: StompSession) => void;
  
  constructor(
    socket: net.Socket, 
    broker: MessageBroker, 
    sessionCloseCallback: (session: StompSession) => void
  ) {
    this.clientSocket = socket;
    this.session = new StompSession(randomUUID(), socket);
    this.broker = broker;
    this.sessionCloseCallback = sessionCloseCallback;
  }
  
  start(): void {
    let buffer = '';
    
    this.clientSocket.on('data', (data: Buffer) => {
      buffer += data.toString('utf8');
      
      // Process complete frames (ending with null terminator)
      let nullIndex;
      while ((nullIndex = buffer.indexOf('\0')) !== -1) {
        const frameData = buffer.substring(0, nullIndex);
        buffer = buffer.substring(nullIndex + 1);
        
        if (frameData.length > 0) {
          const frame = StompFrameUtils.parse(frameData);
          if (frame) {
            this.handleFrame(frame);
          }
        }
      }
    });
    
    this.clientSocket.on('close', () => {
      this.cleanup();
    });
    
    this.clientSocket.on('error', (err: Error) => {
      console.error('Client handler error:', err.message);
      this.cleanup();
    });
  }
  
  private handleFrame(frame: StompFrame): void {
    switch (frame.command) {
      case 'CONNECT':
      case 'STOMP':
        this.handleConnect(frame);
        break;
      case 'SUBSCRIBE':
        this.handleSubscribe(frame);
        break;
      case 'UNSUBSCRIBE':
        this.handleUnsubscribe(frame);
        break;
      case 'SEND':
        this.handleSend(frame);
        break;
      case 'DISCONNECT':
        this.handleDisconnect(frame);
        break;
      default:
        {
          const error = StompFrameUtils.error('Unknown command', `Command '${frame.command}' not supported`);
          this.session.sendFrame(error);
        }
    }
  }
  
  private handleConnect(frame: StompFrame): void {
    const connected = StompFrameUtils.connected(this.session.getSessionId());
    this.session.sendFrame(connected);
    
    // Send receipt if requested
    const receipt = frame.headers['receipt'];
    if (receipt) {
      this.session.sendFrame(StompFrameUtils.receipt(receipt));
    }
    
    console.log('Client connected:', this.session.getSessionId());
  }
  
  private handleSubscribe(frame: StompFrame): void {
    const destination = frame.headers['destination'];
    const subscriptionId = frame.headers['id'];
    
    if (destination && subscriptionId) {
      this.session.subscribe(subscriptionId, destination);
      this.broker.subscribe(destination, this.session);
      console.log(`Client ${this.session.getSessionId()} subscribed to ${destination}`);
    }
    
    const receipt = frame.headers['receipt'];
    if (receipt) {
      this.session.sendFrame(StompFrameUtils.receipt(receipt));
    }
  }
  
  private handleUnsubscribe(frame: StompFrame): void {
    const subscriptionId = frame.headers['id'];
    
    if (subscriptionId) {
      const destination = this.session.subscriptions.get(subscriptionId);
      if (destination) {
        this.session.unsubscribe(subscriptionId);
        this.broker.unsubscribe(destination, this.session);
        console.log(`Client ${this.session.getSessionId()} unsubscribed from ${destination}`);
      }
    }
    
    const receipt = frame.headers['receipt'];
    if (receipt) {
      this.session.sendFrame(StompFrameUtils.receipt(receipt));
    }
  }
  
  private handleSend(frame: StompFrame): void {
    const destination = frame.headers['destination'];
    
    if (destination) {
      this.broker.broadcast(destination, frame.body);
      console.log(`Message sent to ${destination}: ${frame.body}`);
    }
    
    const receipt = frame.headers['receipt'];
    if (receipt) {
      this.session.sendFrame(StompFrameUtils.receipt(receipt));
    }
  }
  
  private handleDisconnect(frame: StompFrame): void {
    const receipt = frame.headers['receipt'];
    if (receipt) {
      this.session.sendFrame(StompFrameUtils.receipt(receipt));
    }
    
    this.cleanup();
  }
  
  private cleanup(): void {
    this.broker.removeSession(this.session);
    this.session.close();
    this.sessionCloseCallback(this.session);
    console.log('Client disconnected:', this.session.getSessionId());
  }
}

// Main STOMP Server
export class StompServer {
  private readonly port: number;
  private readonly broker = new MessageBroker();
  private readonly activeSessions = new Set<StompSession>();
  private running = false;
  private server?: net.Server;
  
  constructor(port: number) {
    this.port = port;
  }
  
  async start(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.server = net.createServer((socket) => {
        console.log('New client connection:', socket.remoteAddress);
        
        const handler = new StompClientHandler(socket, this.broker, (session) => {
          this.removeSession(session);
        });
        
        handler.start();
      });
      
      this.server.on('error', (err) => {
        if (this.running) {
          console.error('Error accepting client:', err.message);
        }
        reject(err);
      });
      
      this.server.listen(this.port, () => {
        this.running = true;
        console.log(`STOMP Server started on port ${this.port}`);
        resolve();
      });
    });
  }
  
  private removeSession(session: StompSession): void {
    this.activeSessions.delete(session);
  }
  
  stop(): void {
    this.running = false;
    
    if (this.server) {
      this.server.close();
    }
    
    // Close all active sessions
    for (const session of this.activeSessions) {
      session.close();
    }
    this.activeSessions.clear();
    
    console.log('STOMP Server stopped');
  }
  
  // Main method for running the server
  static async main(args: string[]): Promise<void> {
    const port = args.length > 0 ? parseInt(args[0]!) : 61613;
    
    const server = new StompServer(port);
    
    // Graceful shutdown hook
    process.on('SIGINT', () => {
      console.log('Shutting down STOMP Server...');
      server.stop();
      process.exit(0);
    });
    
    process.on('SIGTERM', () => {
      console.log('Shutting down STOMP Server...');
      server.stop();
      process.exit(0);
    });
    
    try {
      await server.start();
    } catch (error) {
      console.error('Failed to start STOMP Server:', error);
      process.exit(1);
    }
  }
}

// Run the server if this file is executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  StompServer.main(process.argv.slice(2));
}
