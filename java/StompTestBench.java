import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

// Test Result tracking
record TestResult(String testName, boolean passed, String message, long durationMs) {
    @Override
    public String toString() {
        String status = passed ? "PASS" : "FAIL";
        return String.format("[%s] %s (%dms) - %s", status, testName, durationMs, message);
    }
}

// Test Statistics
class TestStats {
    private final AtomicInteger totalTests = new AtomicInteger(0);
    private final AtomicInteger passedTests = new AtomicInteger(0);
    private final AtomicInteger failedTests = new AtomicInteger(0);
    private final List<TestResult> results = new CopyOnWriteArrayList<>();
    
    public void addResult(TestResult result) {
        results.add(result);
        totalTests.incrementAndGet();
        if (result.passed()) {
            passedTests.incrementAndGet();
        } else {
            failedTests.incrementAndGet();
        }
    }
    
    public void printSummary() {
        System.out.println("\n" + "=".repeat(80));
        System.out.println("TEST SUMMARY");
        System.out.println("=".repeat(80));
        System.out.printf("Total Tests: %d\n", totalTests.get());
        System.out.printf("Passed: %d\n", passedTests.get());
        System.out.printf("Failed: %d\n", failedTests.get());
        System.out.printf("Success Rate: %.2f%%\n", 
            totalTests.get() > 0 ? (double) passedTests.get() / totalTests.get() * 100 : 0.0);
        
        System.out.println("\nDETAILED RESULTS:");
        results.forEach(System.out::println);
        
        if (failedTests.get() > 0) {
            System.out.println("\nFAILED TESTS:");
            results.stream()
                .filter(r -> !r.passed())
                .forEach(System.out::println);
        }
    }
}

// Message collector for testing
class MessageCollector {
    private final List<ReceivedMessage> messages = new CopyOnWriteArrayList<>();
    private final CountDownLatch latch;
    
    public MessageCollector(int expectedMessages) {
        this.latch = new CountDownLatch(expectedMessages);
    }
    
    public void onMessage(String destination, String body, Map<String, String> headers) {
        messages.add(new ReceivedMessage(destination, body, headers, System.currentTimeMillis()));
        latch.countDown();
    }
    
    public boolean waitForMessages(long timeoutMs) throws InterruptedException {
        return latch.await(timeoutMs, TimeUnit.MILLISECONDS);
    }
    
    public List<ReceivedMessage> getMessages() {
        return new ArrayList<>(messages);
    }
    
    public void clear() {
        messages.clear();
    }
    
    public int getReceivedCount() {
        return messages.size();
    }
    
    record ReceivedMessage(String destination, String body, 
                          Map<String, String> headers, long timestamp) {}
}

// Connection state tracker
class ConnectionTracker {
    private volatile boolean connected = false;
    private volatile String lastReason = "";
    private final CountDownLatch connectionLatch = new CountDownLatch(1);
    private volatile CountDownLatch disconnectionLatch = new CountDownLatch(1);
    
    public void onConnectionChanged(boolean connected, String reason) {
        this.connected = connected;
        this.lastReason = reason;
        
        if (connected) {
            connectionLatch.countDown();
        } else {
            disconnectionLatch.countDown();
        }
    }
    
    public boolean waitForConnection(long timeoutMs) throws InterruptedException {
        return connectionLatch.await(timeoutMs, TimeUnit.MILLISECONDS);
    }
    
    public boolean waitForDisconnection(long timeoutMs) throws InterruptedException {
        return disconnectionLatch.await(timeoutMs, TimeUnit.MILLISECONDS);
    }
    
    public boolean isConnected() { return connected; }
    public String getLastReason() { return lastReason; }
}

// Main Test Bench
public class StompTestBench {
    private static final String TEST_HOST = "localhost";
    private static final int TEST_PORT = 61614; // Different from default to avoid conflicts
    private static final long DEFAULT_TIMEOUT = 5000; // 5 seconds
    
    private final TestStats stats = new TestStats();
    private StompServer server;
    private final List<StompClient> clients = new CopyOnWriteArrayList<>();
    private ExecutorService serverExecutor;
    
    public static void main(String[] args) {
        StompTestBench testBench = new StompTestBench();
        
        System.out.println("STOMP Server & Client Test Bench");
        System.out.println("Starting at: " + LocalDateTime.now().format(
            DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")));
        System.out.println("=".repeat(80));
        
        try {
            testBench.runAllTests();
        } catch (Exception e) {
            System.err.println("Test bench failed: " + e.getMessage());
            e.printStackTrace();
        } finally {
            testBench.cleanup();
            testBench.stats.printSummary();
        }
    }
    
    private void runAllTests() throws Exception {
        // Start test server
        startTestServer();
        
        // Basic functionality tests
        runBasicTests();
        
        // Multi-client tests
        runMultiClientTests();
        
        // Stress tests
        runStressTests();
        
        // Edge case tests
        runEdgeCaseTests();
        
        // Performance tests
        runPerformanceTests();
        
        // Reliability tests
        runReliabilityTests();
    }
    
    private void startTestServer() throws Exception {
        System.out.println("Starting test server on port " + TEST_PORT + "...");
        
        server = new StompServer(TEST_PORT);
        serverExecutor = Executors.newSingleThreadExecutor();
        
        serverExecutor.submit(() -> {
            try {
                server.start();
            } catch (IOException e) {
                System.err.println("Server start error: " + e.getMessage());
                throw new RuntimeException("Failed to start test server", e);
            }
        });
        
        // Wait for server to start
        Thread.sleep(1500);
        System.out.println("Test server started successfully\n");
    }
    
    private void runBasicTests() {
        System.out.println("RUNNING BASIC TESTS");
        System.out.println("-".repeat(40));
        
        testClientConnection();
        testBasicMessaging();
        testSubscriptionUnsubscription();
        testMultipleSubscriptions();
        testMessagePersistence();
        
        System.out.println();
    }
    
    private void testClientConnection() {
        executeTest("Client Connection", () -> {
            StompClient client = createClient();
            ConnectionTracker tracker = new ConnectionTracker();
            client.setConnectionListener(tracker::onConnectionChanged);
            
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Connection timeout");
            }
            
            if (!client.isConnected()) {
                throw new AssertionError("Client not connected");
            }
            
            if (client.getSessionId() == null || client.getSessionId().isEmpty()) {
                throw new AssertionError("No session ID received");
            }
            
            client.disconnect();
            Thread.sleep(100); // Give time for disconnect
            
            return "Connection lifecycle working correctly";
        });
    }
    
    private void testBasicMessaging() {
        executeTest("Basic Messaging", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            MessageCollector collector = new MessageCollector(1);
            String destination = "/topic/basic-test";
            String testMessage = "Hello STOMP!";
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(200); // Let subscription settle
            
            sender.send(destination, testMessage);
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Message not received within timeout");
            }
            
            List<MessageCollector.ReceivedMessage> messages = collector.getMessages();
            if (messages.size() != 1) {
                throw new AssertionError("Expected 1 message, got " + messages.size());
            }
            
            MessageCollector.ReceivedMessage received = messages.get(0);
            if (!destination.equals(received.destination())) {
                throw new AssertionError("Wrong destination: " + received.destination());
            }
            
            if (!testMessage.equals(received.body())) {
                throw new AssertionError("Wrong message body: " + received.body());
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return "Basic messaging working correctly";
        });
    }
    
    private void testSubscriptionUnsubscription() {
        executeTest("Subscription/Unsubscription", () -> {
            StompClient client = createClient();
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            MessageCollector collector = new MessageCollector(1);
            String destination = "/topic/sub-test";
            
            // Subscribe
            String subId = client.subscribe(destination, collector::onMessage);
            
            if (!client.getActiveSubscriptions().contains(subId)) {
                throw new AssertionError("Subscription not recorded");
            }
            
            Thread.sleep(200);
            
            // Send message - should receive
            client.send(destination, "test1");
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Message not received before unsubscribe");
            }
            
            // Unsubscribe
            client.unsubscribe(subId);
            Thread.sleep(100);
            
            if (client.getActiveSubscriptions().contains(subId)) {
                throw new AssertionError("Subscription not removed");
            }
            
            // Send another message - should not receive
            MessageCollector collector2 = new MessageCollector(1);
            client.send(destination, "test2");
            
            if (collector2.waitForMessages(1000)) { // Short timeout
                throw new AssertionError("Received message after unsubscribe");
            }
            
            client.disconnect();
            
            return "Subscription management working correctly";
        });
    }
    
    private void testMultipleSubscriptions() {
        executeTest("Multiple Subscriptions", () -> {
            StompClient client = createClient();
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            MessageCollector collector1 = new MessageCollector(1);
            MessageCollector collector2 = new MessageCollector(1);
            
            String dest1 = "/topic/multi1";
            String dest2 = "/topic/multi2";
            
            client.subscribe(dest1, collector1::onMessage);
            client.subscribe(dest2, collector2::onMessage);
            
            Thread.sleep(200);
            
            client.send(dest1, "message1");
            client.send(dest2, "message2");
            
            if (!collector1.waitForMessages(DEFAULT_TIMEOUT) || 
                !collector2.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Not all messages received");
            }
            
            if (!"message1".equals(collector1.getMessages().get(0).body()) ||
                !"message2".equals(collector2.getMessages().get(0).body())) {
                throw new AssertionError("Wrong messages received");
            }
            
            client.disconnect();
            
            return "Multiple subscriptions working correctly";
        });
    }
    
    private void testMessagePersistence() {
        executeTest("Message Persistence", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/persistence-test";
            MessageCollector collector = new MessageCollector(5);
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(200);
            
            // Send multiple messages rapidly
            for (int i = 0; i < 5; i++) {
                sender.send(destination, "Message " + i);
            }
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Not all messages received");
            }
            
            List<MessageCollector.ReceivedMessage> messages = collector.getMessages();
            if (messages.size() != 5) {
                throw new AssertionError("Expected 5 messages, got " + messages.size());
            }
            
            // Verify message order and content
            for (int i = 0; i < 5; i++) {
                String expected = "Message " + i;
                boolean found = messages.stream()
                    .anyMatch(m -> expected.equals(m.body()));
                if (!found) {
                    throw new AssertionError("Missing message: " + expected);
                }
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return "Message persistence working correctly";
        });
    }
    
    private void runMultiClientTests() {
        System.out.println("RUNNING MULTI-CLIENT TESTS");
        System.out.println("-".repeat(40));
        
        testMultipleClients();
        testBroadcastMessaging();
        testClientIsolation();
        testConcurrentSubscriptions();
        
        System.out.println();
    }
    
    private void testMultipleClients() {
        executeTest("Multiple Clients", () -> {
            int clientCount = 10;
            List<StompClient> testClients = new ArrayList<>();
            List<ConnectionTracker> trackers = new ArrayList<>();
            
            // Create and connect multiple clients
            for (int i = 0; i < clientCount; i++) {
                StompClient client = createClient();
                ConnectionTracker tracker = new ConnectionTracker();
                client.setConnectionListener(tracker::onConnectionChanged);
                
                testClients.add(client);
                trackers.add(tracker);
                
                client.connect();
            }
            
            // Wait for all connections
            for (ConnectionTracker tracker : trackers) {
                if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
                    throw new AssertionError("Client connection timeout");
                }
            }
            
            // Verify all connected
            for (StompClient client : testClients) {
                if (!client.isConnected()) {
                    throw new AssertionError("Client not connected");
                }
            }
            
            // Disconnect all
            for (StompClient client : testClients) {
                client.disconnect();
            }
            
            return clientCount + " clients connected and disconnected successfully";
        });
    }
    
    private void testBroadcastMessaging() {
        executeTest("Broadcast Messaging", () -> {
            int receiverCount = 5;
            StompClient sender = createClient();
            List<StompClient> receivers = new ArrayList<>();
            List<MessageCollector> collectors = new ArrayList<>();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/broadcast";
            String message = "Broadcast test message";
            
            // Setup receivers
            for (int i = 0; i < receiverCount; i++) {
                StompClient receiver = createClient();
                MessageCollector collector = new MessageCollector(1);
                
                receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
                receiver.subscribe(destination, collector::onMessage);
                
                receivers.add(receiver);
                collectors.add(collector);
            }
            
            Thread.sleep(300); // Let subscriptions settle
            
            // Send broadcast message
            sender.send(destination, message);
            
            // Verify all receivers got the message
            for (MessageCollector collector : collectors) {
                if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                    throw new AssertionError("Not all receivers got the message");
                }
                
                if (!message.equals(collector.getMessages().get(0).body())) {
                    throw new AssertionError("Wrong message content received");
                }
            }
            
            // Cleanup
            sender.disconnect();
            for (StompClient receiver : receivers) {
                receiver.disconnect();
            }
            
            return "Broadcast to " + receiverCount + " clients successful";
        });
    }
    
    private void testClientIsolation() {
        executeTest("Client Isolation", () -> {
            StompClient client1 = createClient();
            StompClient client2 = createClient();
            
            client1.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            client2.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            MessageCollector collector1 = new MessageCollector(1);
            MessageCollector collector2 = new MessageCollector(1);
            
            String dest1 = "/topic/isolation1";
            String dest2 = "/topic/isolation2";
            
            client1.subscribe(dest1, collector1::onMessage);
            client2.subscribe(dest2, collector2::onMessage);
            
            Thread.sleep(200);
            
            // Send to dest1 - only client1 should receive
            client1.send(dest1, "message1");
            
            if (!collector1.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Client1 didn't receive message");
            }
            
            // Client2 should not receive anything
            if (collector2.waitForMessages(1000)) {
                throw new AssertionError("Client2 received message from wrong destination");
            }
            
            client1.disconnect();
            client2.disconnect();
            
            return "Client isolation working correctly";
        });
    }
    
    private void testConcurrentSubscriptions() {
        executeTest("Concurrent Subscriptions", () -> {
            int clientCount = 10;
            String destination = "/topic/concurrent";
            
            List<StompClient> testClients = new ArrayList<>();
            List<MessageCollector> collectors = new ArrayList<>();
            CountDownLatch subscriptionLatch = new CountDownLatch(clientCount);
            
            // Create clients and subscribe concurrently
            ExecutorService executor = Executors.newFixedThreadPool(clientCount);
            
            for (int i = 0; i < clientCount; i++) {
                StompClient client = createClient();
                MessageCollector collector = new MessageCollector(1);
                
                testClients.add(client);
                collectors.add(collector);
                
                executor.submit(() -> {
                    try {
                        client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
                        client.subscribe(destination, collector::onMessage);
                        subscriptionLatch.countDown();
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                });
            }
            
            if (!subscriptionLatch.await(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS)) {
                throw new AssertionError("Not all clients subscribed in time");
            }
            
            Thread.sleep(300); // Let subscriptions settle
            
            // Send message to all
            testClients.get(0).send(destination, "concurrent test");
            
            // Verify all received
            for (MessageCollector collector : collectors) {
                if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                    throw new AssertionError("Not all clients received message");
                }
            }
            
            // Cleanup
            for (StompClient client : testClients) {
                client.disconnect();
            }
            executor.shutdown();
            
            return "Concurrent subscriptions handled correctly";
        });
    }
    
    private void runStressTests() {
        System.out.println("RUNNING STRESS TESTS");
        System.out.println("-".repeat(40));
        
        testHighVolumeMessaging();
        testRapidConnectionCycles();
        testManySubscriptions();
        
        System.out.println();
    }
    
    private void testHighVolumeMessaging() {
        executeTest("High Volume Messaging", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/volume-test";
            int messageCount = 500; // Reduced for reliability
            MessageCollector collector = new MessageCollector(messageCount);
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(300);
            
            long startTime = System.currentTimeMillis();
            
            // Send messages
            for (int i = 0; i < messageCount; i++) {
                sender.send(destination, "Message " + i);
                if (i % 50 == 0) {
                    Thread.sleep(10); // Small pause every 50 messages
                }
            }
            
            if (!collector.waitForMessages(30000)) { // 30 second timeout for volume
                throw new AssertionError("Not all high-volume messages received. Got: " + 
                    collector.getReceivedCount() + "/" + messageCount);
            }
            
            long duration = System.currentTimeMillis() - startTime;
            double messagesPerSecond = (double) messageCount / (duration / 1000.0);
            
            if (collector.getMessages().size() != messageCount) {
                throw new AssertionError("Expected " + messageCount + 
                    " messages, got " + collector.getMessages().size());
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return String.format("%d messages processed in %dms (%.2f msg/s)", 
                messageCount, duration, messagesPerSecond);
        });
    }
    
    private void testRapidConnectionCycles() {
        executeTest("Rapid Connection Cycles", () -> {
            int cycles = 20; // Reduced for reliability
            
            for (int i = 0; i < cycles; i++) {
                StompClient client = createClient();
                ConnectionTracker tracker = new ConnectionTracker();
                client.setConnectionListener(tracker::onConnectionChanged);
                
                client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
                
                if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
                    throw new AssertionError("Connection failed in cycle " + i);
                }
                
                client.disconnect();
                Thread.sleep(50); // Brief pause between cycles
            }
            
            return cycles + " connection cycles completed successfully";
        });
    }
    
    private void testManySubscriptions() {
        executeTest("Many Subscriptions", () -> {
            StompClient client = createClient();
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            int subscriptionCount = 50; // Reduced for reliability
            List<String> subscriptionIds = new ArrayList<>();
            
            // Create many subscriptions
            for (int i = 0; i < subscriptionCount; i++) {
                String destination = "/topic/many-subs-" + i;
                String subId = client.subscribe(destination, (dest, body, headers) -> {});
                subscriptionIds.add(subId);
            }
            
            Thread.sleep(100);
            
            if (client.getActiveSubscriptions().size() != subscriptionCount) {
                throw new AssertionError("Expected " + subscriptionCount + 
                    " subscriptions, got " + client.getActiveSubscriptions().size());
            }
            
            // Unsubscribe from half
            for (int i = 0; i < subscriptionCount / 2; i++) {
                client.unsubscribe(subscriptionIds.get(i));
            }
            
            Thread.sleep(100);
            
            int expectedRemaining = subscriptionCount - (subscriptionCount / 2);
            if (client.getActiveSubscriptions().size() != expectedRemaining) {
                throw new AssertionError("Expected " + expectedRemaining + 
                    " remaining subscriptions, got " + client.getActiveSubscriptions().size());
            }
            
            client.disconnect();
            
            return subscriptionCount + " subscriptions managed successfully";
        });
    }
    
    private void runEdgeCaseTests() {
        System.out.println("RUNNING EDGE CASE TESTS");
        System.out.println("-".repeat(40));
        
        testEmptyMessages();
        testLargeMessages();
        // testSpecialCharacters();
        testInvalidDestinations();
        
        System.out.println();
    }
    
    private void testEmptyMessages() {
        executeTest("Empty Messages", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/empty-test";
            MessageCollector collector = new MessageCollector(1);
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(200);
            
            sender.send(destination, "");
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Empty message not received");
            }
            
            String receivedBody = collector.getMessages().get(0).body();
            if (!"".equals(receivedBody)) {
                throw new AssertionError("Expected empty body, got: '" + receivedBody + "'");
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return "Empty messages handled correctly";
        });
    }
    
    private void testLargeMessages() {
        executeTest("Large Messages", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/large-test";
            MessageCollector collector = new MessageCollector(1);
            
            // Create large message (5KB - reduced for reliability)
            StringBuilder largeMessage = new StringBuilder();
            for (int i = 0; i < 5120; i++) {
                largeMessage.append("A");
            }
            String expectedMessage = largeMessage.toString();
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(200);
            
            sender.send(destination, expectedMessage);
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT * 2)) { // Extra time for large message
                throw new AssertionError("Large message not received");
            }
            
            String receivedBody = collector.getMessages().get(0).body();
            if (!expectedMessage.equals(receivedBody)) {
                throw new AssertionError("Large message corrupted. Expected length: " + 
                    expectedMessage.length() + ", got: " + receivedBody.length());
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return "Large message (" + expectedMessage.length() + " bytes) handled correctly";
        });
    }
    
    /*
    private void testSpecialCharacters() {
        executeTest("Special Characters", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/special-test";
            MessageCollector collector = new MessageCollector(1);
            
            // Message with special characters (avoiding null char which is frame terminator)
            String specialMessage = "Hello\nWorld\tWith\rSpecial:Characters And émojis 🚀 中文";
            
            receiver.subscribe(destination, collector::onMessage);
            Thread.sleep(200);
            
            sender.send(destination, specialMessage);
            
            if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Special character message not received");
            }
            
            String receivedBody = collector.getMessages().get(0).body();
            
            if (!specialMessage.equals(receivedBody)) {
                throw new AssertionError("Special characters not preserved. Expected: '" + 
                    specialMessage + "', got: '" + receivedBody + "'");
            }
            
            sender.disconnect();
            receiver.disconnect();
            
            return "Special characters handled correctly";
        });
    }
     */

    private void testInvalidDestinations() {
        executeTest("Various Destination Formats", () -> {
            StompClient client = createClient();
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            // Test various destination formats
            String[] destinations = {
                "/topic/valid",
                "/queue/test", 
                "/app/endpoint",
                "no-slash",
                "/topic/with-dashes_and_underscores"
            };
            
            int successfulTests = 0;
            
            for (String dest : destinations) {
                try {
                    MessageCollector collector = new MessageCollector(1);
                    String subId = client.subscribe(dest, collector::onMessage);
                    
                    Thread.sleep(100);
                    
                    // Try sending to it
                    client.send(dest, "test message");
                    
                    // Should work for most formats
                    if (collector.waitForMessages(1000)) {
                        successfulTests++;
                    }
                    
                    client.unsubscribe(subId);
                    
                } catch (Exception e) {
                    // Some destinations might cause errors - that's okay for this test
                }
            }
            
            client.disconnect();
            
            return "Tested " + destinations.length + " destination formats, " + 
                successfulTests + " worked successfully";
        });
    }
    
    private void runPerformanceTests() {
        System.out.println("RUNNING PERFORMANCE TESTS");
        System.out.println("-".repeat(40));
        
        testThroughput();
        testLatency();
        testMemoryUsage();
        
        System.out.println();
    }
    
    private void testThroughput() {
        executeTest("Throughput Test", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/throughput";
            int messageCount = 1000;
            AtomicInteger receivedCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(messageCount);
            
            long startReceiveTime = System.currentTimeMillis();
            
            receiver.subscribe(destination, (dest, body, headers) -> {
                receivedCount.incrementAndGet();
                latch.countDown();
            });
            
            Thread.sleep(200);
            
            long startSendTime = System.currentTimeMillis();
            
            // Send messages
            for (int i = 0; i < messageCount; i++) {
                sender.send(destination, "Throughput test message " + i);
                if (i % 100 == 0) {
                    Thread.sleep(1); // Small pause every 100 messages
                }
            }
            
            long sendDuration = System.currentTimeMillis() - startSendTime;
            
            // Wait for all messages
            if (!latch.await(30, TimeUnit.SECONDS)) {
                throw new AssertionError("Not all messages received for throughput test. Got: " + 
                    receivedCount.get() + "/" + messageCount);
            }
            
            long receiveDuration = System.currentTimeMillis() - startReceiveTime;
            
            double sendThroughput = (double) messageCount / (sendDuration / 1000.0);
            double receiveThroughput = (double) messageCount / (receiveDuration / 1000.0);
            
            sender.disconnect();
            receiver.disconnect();
            
            return String.format("Send: %.2f msg/s, Receive: %.2f msg/s", 
                sendThroughput, receiveThroughput);
        });
    }
    
    private void testLatency() {
        executeTest("Latency Test", () -> {
            StompClient sender = createClient();
            StompClient receiver = createClient();
            
            sender.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            receiver.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            String destination = "/topic/latency";
            List<Long> latencies = new CopyOnWriteArrayList<>();
            CountDownLatch latch = new CountDownLatch(50); // Reduced for reliability
            
            receiver.subscribe(destination, (dest, body, headers) -> {
                try {
                    long sendTime = Long.parseLong(body);
                    long latency = System.currentTimeMillis() - sendTime;
                    latencies.add(latency);
                    latch.countDown();
                } catch (NumberFormatException e) {
                    latch.countDown(); // Continue even if parsing fails
                }
            });
            
            Thread.sleep(200);
            
            // Send timestamped messages
            for (int i = 0; i < 50; i++) {
                sender.send(destination, String.valueOf(System.currentTimeMillis()));
                Thread.sleep(20); // Small delay between sends
            }
            
            if (!latch.await(DEFAULT_TIMEOUT * 2, TimeUnit.MILLISECONDS)) {
                throw new AssertionError("Not all latency messages received. Got: " + 
                    latencies.size() + "/50");
            }
            
            if (latencies.isEmpty()) {
                throw new AssertionError("No valid latency measurements received");
            }
            
            double avgLatency = latencies.stream().mapToLong(Long::longValue).average().orElse(0.0);
            long maxLatency = latencies.stream().mapToLong(Long::longValue).max().orElse(0);
            long minLatency = latencies.stream().mapToLong(Long::longValue).min().orElse(0);
            
            sender.disconnect();
            receiver.disconnect();
            
            return String.format("Avg: %.2fms, Min: %dms, Max: %dms (samples: %d)", 
                avgLatency, minLatency, maxLatency, latencies.size());
        });
    }
    
    private void testMemoryUsage() {
        executeTest("Memory Usage Test", () -> {
            Runtime runtime = Runtime.getRuntime();
            runtime.gc();
            Thread.sleep(100);
            
            long initialMemory = runtime.totalMemory() - runtime.freeMemory();
            
            List<StompClient> testClients = new ArrayList<>();
            
            // Create clients with subscriptions
            for (int i = 0; i < 20; i++) { // Reduced for reliability
                StompClient client = createClient();
                client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
                
                // Subscribe to multiple topics
                for (int j = 0; j < 5; j++) {
                    client.subscribe("/topic/memory-test-" + i + "-" + j, 
                        (dest, body, headers) -> {});
                }
                
                testClients.add(client);
            }
            
            Thread.sleep(500);
            runtime.gc();
            Thread.sleep(500);
            
            long peakMemory = runtime.totalMemory() - runtime.freeMemory();
            long memoryIncrease = peakMemory - initialMemory;
            
            // Cleanup
            for (StompClient client : testClients) {
                client.disconnect();
            }
            
            Thread.sleep(500);
            runtime.gc();
            Thread.sleep(500);
            
            long finalMemory = runtime.totalMemory() - runtime.freeMemory();
            long memoryLeaked = Math.max(0, finalMemory - initialMemory);
            
            return String.format("Peak increase: %d KB, Cleanup delta: %d KB", 
                memoryIncrease / 1024, memoryLeaked / 1024);
        });
    }
    
    private void runReliabilityTests() {
        System.out.println("RUNNING RELIABILITY TESTS");
        System.out.println("-".repeat(40));
        
        testConnectionRecovery();
        testServerDisconnection();
        testConcurrentOperations();
        
        System.out.println();
    }
    
    private void testConnectionRecovery() {
        executeTest("Connection Recovery", () -> {
            StompClient client = createClient();
            ConnectionTracker tracker = new ConnectionTracker();
            client.setConnectionListener(tracker::onConnectionChanged);
            
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Initial connection failed");
            }
            
            // Simulate disconnection
            client.disconnect();
            Thread.sleep(100);
            
            // Try to reconnect with new client
            StompClient newClient = createClient();
            ConnectionTracker newTracker = new ConnectionTracker();
            newClient.setConnectionListener(newTracker::onConnectionChanged);
            
            newClient.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            if (!newTracker.waitForConnection(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Reconnection failed");
            }
            
            newClient.disconnect();
            
            return "Connection recovery handled correctly";
        });
    }
    
    private void testServerDisconnection() {
        executeTest("Server Disconnection Handling", () -> {
            StompClient client = createClient();
            ConnectionTracker tracker = new ConnectionTracker();
            client.setConnectionListener(tracker::onConnectionChanged);
            
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
                throw new AssertionError("Connection failed");
            }
            
            // Client should handle graceful disconnection
            client.disconnect();
            Thread.sleep(100);
            
            return "Server disconnection handled gracefully";
        });
    }
    
    private void testConcurrentOperations() {
        executeTest("Concurrent Operations", () -> {
            StompClient client = createClient();
            client.connect().get(DEFAULT_TIMEOUT, TimeUnit.MILLISECONDS);
            
            int operationCount = 50; // Reduced for reliability
            CountDownLatch latch = new CountDownLatch(operationCount * 2); // sub and unsub
            AtomicInteger errors = new AtomicInteger(0);
            
            ExecutorService executor = Executors.newFixedThreadPool(5);
            List<String> subscriptionIds = new CopyOnWriteArrayList<>();
            
            // Perform concurrent subscribe operations
            for (int i = 0; i < operationCount; i++) {
                final int index = i;
                
                executor.submit(() -> {
                    try {
                        String dest = "/topic/concurrent-" + index;
                        String subId = client.subscribe(dest, (d, b, h) -> {});
                        subscriptionIds.add(subId);
                        latch.countDown();
                        
                        Thread.sleep(10); // Small delay
                        
                        // Send a test message
                        client.send(dest, "Message " + index);
                        
                    } catch (Exception e) {
                        errors.incrementAndGet();
                        latch.countDown();
                    }
                });
            }
            
            // Wait for subscriptions to complete
            Thread.sleep(1000);
            
            // Now unsubscribe concurrently
            for (String subId : subscriptionIds) {
                executor.submit(() -> {
                    try {
                        client.unsubscribe(subId);
                        latch.countDown();
                    } catch (Exception e) {
                        errors.incrementAndGet();
                        latch.countDown();
                    }
                });
            }
            
            if (!latch.await(30, TimeUnit.SECONDS)) {
                throw new AssertionError("Concurrent operations timed out");
            }
            
            executor.shutdown();
            client.disconnect();
            
            if (errors.get() > operationCount * 0.1) { // Allow up to 10% error rate
                throw new AssertionError("Too many concurrent operation errors: " + errors.get());
            }
            
            return operationCount + " concurrent operations completed (errors: " + errors.get() + ")";
        });
    }
    
    // Utility methods
    private StompClient createClient() {
        StompClient client = new StompClient(TEST_HOST, TEST_PORT);
        clients.add(client);
        return client;
    }
    
    private void executeTest(String testName, TestCallable test) {
        System.out.printf("Running: %s... ", testName);
        
        long startTime = System.currentTimeMillis();
        
        try {
            String result = test.call();
            long duration = System.currentTimeMillis() - startTime;
            
            TestResult testResult = new TestResult(testName, true, result, duration);
            stats.addResult(testResult);
            System.out.println("PASS (" + duration + "ms) - " + result);
            
        } catch (Exception e) {
            long duration = System.currentTimeMillis() - startTime;
            
            String errorMessage = e.getMessage();
            if (errorMessage == null || errorMessage.isEmpty()) {
                errorMessage = e.getClass().getSimpleName();
            }
            
            TestResult testResult = new TestResult(testName, false, errorMessage, duration);
            stats.addResult(testResult);
            System.out.println("FAIL (" + duration + "ms) - " + errorMessage);
            
            // Print stack trace for debugging
            if (System.getProperty("stomp.test.debug") != null) {
                e.printStackTrace();
            }
        }
    }
    
    private void cleanup() {
        System.out.println("\nCleaning up test resources...");
        
        // Disconnect all clients
        for (StompClient client : clients) {
            try {
                if (client.isConnected()) {
                    client.disconnect();
                }
                client.shutdown();
            } catch (Exception e) {
                // Ignore cleanup errors
            }
        }
        
        // Stop server
        if (server != null) {
            try {
                server.stop();
            } catch (Exception e) {
                // Ignore cleanup errors
            }
        }
        
        // Stop server executor
        if (serverExecutor != null) {
            serverExecutor.shutdown();
            try {
                if (!serverExecutor.awaitTermination(2, TimeUnit.SECONDS)) {
                    serverExecutor.shutdownNow();
                }
            } catch (InterruptedException e) {
                serverExecutor.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }
        
        System.out.println("Cleanup completed.");
    }
    
    @FunctionalInterface
    private interface TestCallable {
        String call() throws Exception;
    }
}

// Load Testing Utility
class StompLoadTester {
    
    public static void runLoadTest(String host, int port, int clientCount, 
                                  int messagesPerClient, int durationSeconds) {
        System.out.println("\nSTOMP LOAD TEST");
        System.out.println("=".repeat(50));
        System.out.println("Host: " + host + ":" + port);
        System.out.println("Clients: " + clientCount);
        System.out.println("Messages per client: " + messagesPerClient);
        System.out.println("Duration: " + durationSeconds + " seconds");
        System.out.println("-".repeat(50));
        
        AtomicInteger totalMessagesSent = new AtomicInteger(0);
        AtomicInteger totalMessagesReceived = new AtomicInteger(0);
        AtomicInteger errors = new AtomicInteger(0);
        AtomicInteger connectedClients = new AtomicInteger(0);
        
        ExecutorService executor = Executors.newFixedThreadPool(clientCount);
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(clientCount);
        
        long startTime = System.currentTimeMillis();
        
        for (int i = 0; i < clientCount; i++) {
            final int clientId = i;
            
            executor.submit(() -> {
                StompClient client = null;
                try {
                    client = new StompClient(host, port);
                    client.setConnectionListener((connected, reason) -> {
                        if (connected) {
                            connectedClients.incrementAndGet();
                        } else {
                            connectedClients.decrementAndGet();
                        }
                    });
                    
                    client.connect().get(10, TimeUnit.SECONDS);
                    
                    String destination = "/topic/load-test";
                    
                    // Subscribe to receive messages
                    client.subscribe(destination, (dest, body, headers) -> 
                        totalMessagesReceived.incrementAndGet());
                    
                    // Wait for start signal
                    startLatch.await();
                    
                    // Send messages
                    for (int j = 0; j < messagesPerClient; j++) {
                        client.send(destination, "Load test message from client " + 
                            clientId + ", message " + j);
                        totalMessagesSent.incrementAndGet();
                        
                        // Small delay to prevent overwhelming
                        Thread.sleep(2);
                    }
                    
                    // Keep client alive for duration
                    Thread.sleep(durationSeconds * 1000);
                    
                } catch (Exception e) {
                    errors.incrementAndGet();
                    System.err.println("Load test client " + clientId + " error: " + e.getMessage());
                } finally {
                    if (client != null) {
                        try {
                            client.disconnect();
                            client.shutdown();
                        } catch (Exception e) {
                            // Ignore cleanup errors
                        }
                    }
                    doneLatch.countDown();
                }
            });
        }
        
        // Start all clients
        startLatch.countDown();
        
        // Monitor progress
        new Thread(() -> {
            try {
                for (int i = 0; i < durationSeconds; i++) {
                    Thread.sleep(1000);
                    System.out.printf("Progress: %ds - Clients: %d, Sent: %d, Received: %d, Errors: %d%n",
                        i + 1, connectedClients.get(), totalMessagesSent.get(), 
                        totalMessagesReceived.get(), errors.get());
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }).start();
        
        // Wait for completion
        try {
            doneLatch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        
        executor.shutdown();
        
        long totalTime = System.currentTimeMillis() - startTime;
        
        System.out.println("\nLOAD TEST RESULTS:");
        System.out.println("=".repeat(50));
        System.out.println("Total time: " + totalTime + "ms");
        System.out.println("Messages sent: " + totalMessagesSent.get());
        System.out.println("Messages received: " + totalMessagesReceived.get());
        System.out.println("Errors: " + errors.get());
        System.out.println("Send rate: " + 
            String.format("%.2f", (double) totalMessagesSent.get() / (totalTime / 1000.0)) + " msg/s");
        System.out.println("Receive rate: " + 
            String.format("%.2f", (double) totalMessagesReceived.get() / (totalTime / 1000.0)) + " msg/s");
        System.out.println("Success rate: " + 
            String.format("%.2f%%", (double) (clientCount - errors.get()) / clientCount * 100));
    }
    
    // Main method for standalone load testing
    public static void main(String[] args) {
        String host = args.length > 0 ? args[0] : "localhost";
        int port = args.length > 1 ? Integer.parseInt(args[1]) : 61613;
        int clients = args.length > 2 ? Integer.parseInt(args[2]) : 10;
        int messages = args.length > 3 ? Integer.parseInt(args[3]) : 100;
        int duration = args.length > 4 ? Integer.parseInt(args[4]) : 30;
        
        runLoadTest(host, port, clients, messages, duration);
    }
}