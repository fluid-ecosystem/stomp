import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CompletableFuture;

public class CppServerTest {
    private static AtomicInteger receivedCount = new AtomicInteger(0);
    private static AtomicBoolean connectionSuccess = new AtomicBoolean(false);
    private static CountDownLatch testLatch = new CountDownLatch(1);
    
    public static void main(String[] args) {
        try {
            System.out.println("🔌 [Java] Creating client with credentials...");
            StompClient client = new StompClient("localhost", 61613, "java_client", "test123");
            
            System.out.println("🔌 [Java] Connecting to C++ server...");
            CompletableFuture<Void> connectFuture = client.connect();
            
            // Wait for connection with timeout
            connectFuture.get(10, TimeUnit.SECONDS);
            System.out.println("✅ [Java] Connected to C++ server!");
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
            System.out.println("📤 [Java] Sending messages to C++ server...");
            client.send("/queue/cross-test", "Hello C++ from Java!");
            client.send("/queue/cross-test", "Cross-language test message 1");
            client.send("/queue/cross-test", "Cross-language test message 2");
            
            // Wait for any responses
            System.out.println("⏳ [Java] Waiting for message processing...");
            Thread.sleep(3000);
            
            // Test different destinations
            client.send("/topic/broadcast", "Java broadcast message");
            
            Thread.sleep(2000);
            
            System.out.println("🔌 [Java] Disconnecting...");
            client.disconnect();
            
            System.out.println("📊 [Java] Messages received: " + receivedCount.get());
            System.out.println("🎉 [Java] Test completed successfully!");
            
        } catch (Exception e) {
            System.out.println("❌ [Java] Error: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }
}
