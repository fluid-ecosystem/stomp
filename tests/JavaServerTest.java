import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CompletableFuture;

public class JavaServerTest {
    private static AtomicInteger receivedCount = new AtomicInteger(0);
    private static AtomicBoolean connectionSuccess = new AtomicBoolean(false);
    
    public static void main(String[] args) {
        try {
            System.out.println("🔌 [Java] Creating client with credentials...");
            StompClient client = new StompClient("localhost", 61613, "java_client", "test123");
            
            System.out.println("🔌 [Java] Connecting to Java server...");
            CompletableFuture<Void> connectFuture = client.connect();
            
            // Wait for connection with timeout
            connectFuture.get(10, TimeUnit.SECONDS);
            System.out.println("✅ [Java] Connected to Java server!");
            connectionSuccess.set(true);
            
            // Give connection time to fully establish
            Thread.sleep(500);
            
            // Subscribe to test queue
            System.out.println("📡 [Java] Subscribing to /queue/cross-test...");
            String subId = client.subscribe("/queue/cross-test", (dest, body, headers) -> {
                System.out.println("📨 [Java] Received: " + body + " on " + dest);
                receivedCount.incrementAndGet();
            });
            System.out.println("✅ [Java] Subscribed with ID: " + subId);
            
            // Send test messages
            System.out.println("📤 [Java] Sending messages to Java server...");
            client.send("/queue/cross-test", "Hello Java from Java!");
            client.send("/queue/cross-test", "Self-test message 1");
            client.send("/queue/cross-test", "Self-test message 2");
            
            // Wait for any responses
            System.out.println("⏳ [Java] Waiting for message processing...");
            Thread.sleep(3000);
            
            // Test different destinations
            client.send("/topic/broadcast", "Java broadcast message");
            
            Thread.sleep(2000);
            
            // Cleanup
            System.out.println("🧹 [Java] Cleaning up...");
            client.unsubscribe(subId);
            client.disconnect();
            
            System.out.println("📊 [Java] Test Results:");
            System.out.println("   Connection: " + (connectionSuccess.get() ? "SUCCESS" : "FAILED"));
            System.out.println("   Messages received: " + receivedCount.get());
            System.out.println("   Status: " + (connectionSuccess.get() ? "PASSED" : "FAILED"));
            
            if (!connectionSuccess.get()) {
                System.exit(1);
            }
            
            System.out.println("🎉 [Java] Self-test completed successfully!");
            
        } catch (Exception e) {
            System.err.println("❌ [Java] Test failed with error: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }
}
