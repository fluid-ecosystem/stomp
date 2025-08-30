import { StompClient } from './StompClient.js';
import * as readline from 'readline';

// Demo/Test client
async function main(): Promise<void> {
  const host = process.argv[2] || 'localhost';
  const port = parseInt(process.argv[3] || '61613');
  
  const client = new StompClient(host, port);
  
  // Set up listeners
  client.setConnectionListener((connected, reason) => 
    console.log(`Connection status: ${connected} - ${reason}`));
  
  client.setErrorListener((message, details) => 
    console.error(`STOMP Error: ${message}\nDetails: ${details}`));
  
  // Graceful shutdown
  process.on('SIGINT', () => {
    console.log('Shutting down STOMP client...');
    client.shutdown();
    process.exit(0);
  });
  
  try {
    // Connect to server
    await client.connect();
    
    // Subscribe to a test topic
    const subscription = client.subscribe('/topic/test', 
      (destination, body, headers) => 
        console.log(`Received message on ${destination}: ${body}`));
    
    // Interactive console
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout
    });
    
    console.log('STOMP Client started. Commands:');
    console.log('  send <destination> <message> - Send a message');
    console.log('  subscribe <destination> - Subscribe to destination');
    console.log('  unsubscribe <subscription-id> - Unsubscribe');
    console.log('  status - Show connection status');
    console.log('  quit - Exit');
    
    const askQuestion = () => {
      rl.question('> ', (input) => {
        if (!client.isConnected() && input !== 'quit') {
          console.log('Not connected to server');
          askQuestion();
          return;
        }
        
        const parts = input.trim().split(' ');
        
        try {
          switch (parts[0]) {
            case 'quit':
              rl.close();
              return;
            
            case 'send':
              if (parts.length >= 3) {
                const destination = parts[1]!;
                const message = parts.slice(2).join(' ');
                client.send(destination, message);
              } else {
                console.log('Usage: send <destination> <message>');
              }
              break;
            
            case 'subscribe':
              if (parts.length >= 2) {
                const destination = parts[1]!;
                const subId = client.subscribe(destination, 
                  (dest, body, headers) => 
                    console.log(`[${dest}] ${body}`));
                console.log(`Subscribed with ID: ${subId}`);
              } else {
                console.log('Usage: subscribe <destination>');
              }
              break;
            
            case 'unsubscribe':
              if (parts.length >= 2) {
                const subId = parts[1]!;
                client.unsubscribe(subId);
              } else {
                console.log('Usage: unsubscribe <subscription-id>');
              }
              break;
            
            case 'status':
              console.log(`Connected: ${client.isConnected()}`);
              console.log(`Session: ${client.getSessionId()}`);
              console.log(`Active subscriptions: ${Array.from(client.getActiveSubscriptions()).join(', ')}`);
              break;
            
            default:
              if (parts[0]!.length > 0) {
                console.log(`Unknown command: ${parts[0]}`);
              }
          }
        } catch (error) {
          console.error(`Error: ${error}`);
        }
        
        askQuestion();
      });
    };
    
    askQuestion();
    
    rl.on('close', () => {
      client.shutdown();
      process.exit(0);
    });
    
  } catch (error) {
    console.error(`Client error: ${error}`);
    client.shutdown();
    process.exit(1);
  }
}

// Run the client if this file is executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch(console.error);
}
