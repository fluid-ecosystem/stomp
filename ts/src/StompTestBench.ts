import { StompClient } from './StompClient.js';
import { StompServer } from './StompServer.js';

// Test Result tracking
interface TestResult {
  testName: string;
  passed: boolean;
  message: string;
  durationMs: number;
}

// Test Statistics
class TestStats {
  private totalTests = 0;
  private passedTests = 0;
  private failedTests = 0;
  private readonly results: TestResult[] = [];
  
  addResult(result: TestResult): void {
    this.results.push(result);
    this.totalTests++;
    if (result.passed) {
      this.passedTests++;
    } else {
      this.failedTests++;
    }
  }
  
  printSummary(): void {
    console.log('\n' + '='.repeat(80));
    console.log('TEST SUMMARY');
    console.log('='.repeat(80));
    console.log(`Total Tests: ${this.totalTests}`);
    console.log(`Passed: ${this.passedTests}`);
    console.log(`Failed: ${this.failedTests}`);
    console.log(`Success Rate: ${this.totalTests > 0 ? (this.passedTests / this.totalTests * 100).toFixed(2) : 0.00}%`);
    
    console.log('\nDETAILED RESULTS:');
    this.results.forEach(result => {
      const status = result.passed ? 'PASS' : 'FAIL';
      console.log(`[${status}] ${result.testName} (${result.durationMs}ms) - ${result.message}`);
    });
    
    if (this.failedTests > 0) {
      console.log('\nFAILED TESTS:');
      this.results
        .filter(r => !r.passed)
        .forEach(result => {
          const status = result.passed ? 'PASS' : 'FAIL';
          console.log(`[${status}] ${result.testName} (${result.durationMs}ms) - ${result.message}`);
        });
    }
  }
}

// Message collector for testing
class MessageCollector {
  private readonly messages: ReceivedMessage[] = [];
  private readonly expectedCount: number;
  private resolveWait?: () => void;
  private rejectWait?: (error: Error) => void;
  
  constructor(expectedMessages: number) {
    this.expectedCount = expectedMessages;
  }
  
  onMessage = (destination: string, body: string, headers: Record<string, string>): void => {
    this.messages.push({
      destination,
      body,
      headers,
      timestamp: Date.now()
    });
    
    if (this.messages.length >= this.expectedCount && this.resolveWait) {
      this.resolveWait();
    }
  };
  
  async waitForMessages(timeoutMs: number): Promise<boolean> {
    if (this.messages.length >= this.expectedCount) {
      return true;
    }
    
    return new Promise((resolve, reject) => {
      this.resolveWait = () => resolve(true);
      this.rejectWait = reject;
      
      setTimeout(() => {
        resolve(false);
      }, timeoutMs);
    });
  }
  
  getMessages(): ReceivedMessage[] {
    return [...this.messages];
  }
  
  clear(): void {
    this.messages.length = 0;
  }
  
  getReceivedCount(): number {
    return this.messages.length;
  }
}

interface ReceivedMessage {
  destination: string;
  body: string;
  headers: Record<string, string>;
  timestamp: number;
}

// Connection state tracker
class ConnectionTracker {
  private connected = false;
  private lastReason = '';
  private resolveConnection?: () => void;
  private resolveDisconnection?: () => void;
  
  onConnectionChanged = (connected: boolean, reason: string): void => {
    this.connected = connected;
    this.lastReason = reason;
    
    if (connected && this.resolveConnection) {
      this.resolveConnection();
    } else if (!connected && this.resolveDisconnection) {
      this.resolveDisconnection();
    }
  };
  
  async waitForConnection(timeoutMs: number): Promise<boolean> {
    if (this.connected) return true;
    
    return new Promise((resolve) => {
      this.resolveConnection = () => resolve(true);
      setTimeout(() => resolve(false), timeoutMs);
    });
  }
  
  async waitForDisconnection(timeoutMs: number): Promise<boolean> {
    if (!this.connected) return true;
    
    return new Promise((resolve) => {
      this.resolveDisconnection = () => resolve(true);
      setTimeout(() => resolve(false), timeoutMs);
    });
  }
  
  isConnected(): boolean {
    return this.connected;
  }
  
  getLastReason(): string {
    return this.lastReason;
  }
}

// Test Suite
export class StompTestBench {
  private readonly stats = new TestStats();
  private server?: StompServer;
  private readonly testPort = 61614; // Use different port for testing
  
  async runAllTests(): Promise<void> {
    console.log('Starting STOMP Test Bench...\n');
    
    try {
      // Start test server
      await this.startTestServer();
      
      // Run all tests
      await this.testFrameParsing();
      await this.testBasicConnection();
      await this.testSubscriptionAndMessaging();
      await this.testMultipleClients();
      await this.testErrorHandling();
      await this.testConnectionResilience();
      
    } finally {
      // Stop test server
      await this.stopTestServer();
      
      // Print summary
      this.stats.printSummary();
    }
  }
  
  private async startTestServer(): Promise<void> {
    this.server = new StompServer(this.testPort);
    await this.server.start();
    // Give server time to start
    await this.sleep(100);
  }
  
  private async stopTestServer(): Promise<void> {
    if (this.server) {
      this.server.stop();
      await this.sleep(100); // Give server time to stop
    }
  }
  
  private async testFrameParsing(): Promise<void> {
    await this.runTest('Frame Parsing and Serialization', async () => {
      const { StompFrameUtils } = await import('./StompFrame.js');
      
      // Test CONNECT frame
      const connectFrame = StompFrameUtils.connect('localhost', 'user', 'pass');
      const serialized = StompFrameUtils.serialize(connectFrame);
      const parsed = StompFrameUtils.parse(serialized.replace('\0', ''));
      
      if (!parsed) throw new Error('Failed to parse frame');
      if (parsed.command !== 'CONNECT') throw new Error('Wrong command');
      if (parsed.headers['host'] !== 'localhost') throw new Error('Wrong host header');
      
      // Test MESSAGE frame
      const messageFrame = StompFrameUtils.message('/topic/test', 'msg-1', 'sub-1', 'Hello World');
      const msgSerialized = StompFrameUtils.serialize(messageFrame);
      const msgParsed = StompFrameUtils.parse(msgSerialized.replace('\0', ''));
      
      if (!msgParsed) throw new Error('Failed to parse message frame');
      if (msgParsed.body !== 'Hello World') throw new Error('Wrong message body');
    });
  }
  
  private async testBasicConnection(): Promise<void> {
    await this.runTest('Basic Connection', async () => {
      const client = new StompClient('localhost', this.testPort);
      const tracker = new ConnectionTracker();
      client.setConnectionListener(tracker.onConnectionChanged);
      
      try {
        await client.connect();
        
        if (!await tracker.waitForConnection(2000)) {
          throw new Error('Connection timeout');
        }
        
        if (!client.isConnected()) {
          throw new Error('Client not connected');
        }
        
        if (!client.getSessionId()) {
          throw new Error('No session ID received');
        }
        
      } finally {
        client.shutdown();
      }
    });
  }
  
  private async testSubscriptionAndMessaging(): Promise<void> {
    await this.runTest('Subscription and Messaging', async () => {
      const client1 = new StompClient('localhost', this.testPort);
      const client2 = new StompClient('localhost', this.testPort);
      
      const collector = new MessageCollector(1);
      
      try {
        await client1.connect();
        await client2.connect();
        
        // Client1 subscribes to topic
        client1.subscribe('/topic/test', collector.onMessage);
        
        await this.sleep(100); // Give subscription time to register
        
        // Client2 sends message
        client2.send('/topic/test', 'Hello from client2');
        
        // Wait for message
        if (!await collector.waitForMessages(2000)) {
          throw new Error('Message not received within timeout');
        }
        
        const messages = collector.getMessages();
        if (messages.length !== 1) {
          throw new Error(`Expected 1 message, got ${messages.length}`);
        }
        
        if (messages[0]!.body !== 'Hello from client2') {
          throw new Error('Wrong message content');
        }
        
      } finally {
        client1.shutdown();
        client2.shutdown();
      }
    });
  }
  
  private async testMultipleClients(): Promise<void> {
    await this.runTest('Multiple Clients', async () => {
      const clients: StompClient[] = [];
      const collectors: MessageCollector[] = [];
      
      try {
        // Create 3 clients
        for (let i = 0; i < 3; i++) {
          const client = new StompClient('localhost', this.testPort);
          const collector = new MessageCollector(1);
          
          clients.push(client);
          collectors.push(collector);
          
          await client.connect();
          client.subscribe('/topic/multi', collector.onMessage);
        }
        
        await this.sleep(100); // Give subscriptions time to register
        
        // Send message from first client
        clients[0]!.send('/topic/multi', 'Broadcast message');
        
        // All clients should receive the message
        for (let i = 0; i < 3; i++) {
          if (!await collectors[i]!.waitForMessages(2000)) {
            throw new Error(`Client ${i} did not receive message`);
          }
        }
        
        // Verify message content
        for (let i = 0; i < 3; i++) {
          const messages = collectors[i]!.getMessages();
          if (messages.length !== 1) {
            throw new Error(`Client ${i} received ${messages.length} messages`);
          }
          if (messages[0]!.body !== 'Broadcast message') {
            throw new Error(`Client ${i} received wrong message`);
          }
        }
        
      } finally {
        clients.forEach(client => client.shutdown());
      }
    });
  }
  
  private async testErrorHandling(): Promise<void> {
    await this.runTest('Error Handling', async () => {
      const client = new StompClient('localhost', this.testPort);
      
      try {
        // Test sending without connection
        try {
          client.send('/topic/test', 'Should fail');
          throw new Error('Expected error for send without connection');
        } catch (error) {
          if (!(error as Error).message.includes('Not connected')) {
            throw new Error('Wrong error message');
          }
        }
        
        // Test subscription without connection
        try {
          client.subscribe('/topic/test', () => {});
          throw new Error('Expected error for subscribe without connection');
        } catch (error) {
          if (!(error as Error).message.includes('Not connected')) {
            throw new Error('Wrong error message');
          }
        }
        
      } finally {
        client.shutdown();
      }
    });
  }
  
  private async testConnectionResilience(): Promise<void> {
    await this.runTest('Connection Resilience', async () => {
      const client = new StompClient('localhost', this.testPort);
      const tracker = new ConnectionTracker();
      client.setConnectionListener(tracker.onConnectionChanged);
      
      try {
        await client.connect();
        
        if (!await tracker.waitForConnection(2000)) {
          throw new Error('Initial connection failed');
        }
        
        // Force disconnect
        client.disconnect();
        
        if (!await tracker.waitForDisconnection(2000)) {
          throw new Error('Disconnect event not received');
        }
        
        if (client.isConnected()) {
          throw new Error('Client still reports connected after disconnect');
        }
        
      } finally {
        client.shutdown();
      }
    });
  }
  
  private async runTest(name: string, testFn: () => Promise<void>): Promise<void> {
    const startTime = Date.now();
    
    try {
      await testFn();
      const duration = Date.now() - startTime;
      this.stats.addResult({
        testName: name,
        passed: true,
        message: 'Test passed',
        durationMs: duration
      });
      console.log(`✓ ${name} (${duration}ms)`);
      
    } catch (error) {
      const duration = Date.now() - startTime;
      this.stats.addResult({
        testName: name,
        passed: false,
        message: (error as Error).message,
        durationMs: duration
      });
      console.log(`✗ ${name} (${duration}ms) - ${(error as Error).message}`);
    }
  }
  
  private sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

// Main function for running tests
async function main(): Promise<void> {
  const testBench = new StompTestBench();
  await testBench.runAllTests();
  
  // Exit with appropriate code
  process.exit(0);
}

// Run the test bench if this file is executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch((error) => {
    console.error('Test bench failed:', error);
    process.exit(1);
  });
}
