import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;
import java.util.concurrent.atomic.AtomicInteger;


// Message listener interface
@FunctionalInterface
interface StompMessageListener {
    void onMessage(String destination, String body, Map<String, String> headers);
}

// Connection state listener
@FunctionalInterface
interface StompConnectionListener {
    void onConnectionChanged(boolean connected, String reason);
}

// Error listener
@FunctionalInterface
interface StompErrorListener {
    void onError(String message, String details);
}

// Subscription holder
record StompSubscription(String subscriptionId, String destination, 
                        StompMessageListener listener) {}

// STOMP Client Implementation
public class StompClient {
    private final String host;
    private final int port;
    private final String login;
    private final String passcode;
    
    private Socket socket;
    private PrintWriter writer;
    private volatile boolean connected = false;
    private volatile boolean connecting = false;
    private String sessionId;
    
    // Listeners
    private StompConnectionListener connectionListener;
    private StompErrorListener errorListener;
    
    // Subscriptions management
    private final Map<String, StompSubscription> subscriptions = 
        new ConcurrentHashMap<>();
    public final AtomicInteger subscriptionCounter = new AtomicInteger(0);
    
    // Frame processing
    private final ExecutorService frameProcessor = 
        Executors.newSingleThreadExecutor(r -> {
            Thread t = new Thread(r, "STOMP-Frame-Processor");
            t.setDaemon(true);
            return t;
        });
    
    private final CompletableFuture<Void> readerTask = new CompletableFuture<>();
    
    public StompClient(String host, int port) {
        this(host, port, null, null);
    }
    
    public StompClient(String host, int port, String login, String passcode) {
        this.host = host;
        this.port = port;
        this.login = login;
        this.passcode = passcode;
    }
    
    // Connection management
    public CompletableFuture<Void> connect() {
        if (connected || connecting) {
            return CompletableFuture.completedFuture(null);
        }
        
        connecting = true;
        
        return CompletableFuture.supplyAsync(() -> {
            try {
                socket = new Socket(host, port);
                writer = new PrintWriter(
                    new OutputStreamWriter(socket.getOutputStream(), 
                    StandardCharsets.UTF_8), true);
                
                // Send CONNECT frame
                StompFrame connectFrame = StompFrame.connect(host, login, passcode);
                sendFrame(connectFrame);
                
                // Start frame reader
                frameProcessor.submit(this::readFrames);
                
                System.out.println("Connected to STOMP server at " + host + ":" + port);
                
                // Wait for CONNECTED response
                long startTime = System.currentTimeMillis();
                while (!connected && connecting && (System.currentTimeMillis() - startTime) < 5000) {
                    try {
                        Thread.sleep(10);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new RuntimeException("Connection interrupted", e);
                    }
                }
                
                if (!connected) {
                    connecting = false;
                    cleanup();
                    throw new RuntimeException("Connection timeout - no CONNECTED frame received");
                }
                
                return null;
                
            } catch (IOException e) {
                connecting = false;
                notifyConnectionListener(false, "Connection failed: " + e.getMessage());
                throw new RuntimeException("Failed to connect to STOMP server", e);
            }
        });
    }
    
    public void disconnect() {
        if (!connected) return;
        
        try {
            // Send DISCONNECT frame
            sendFrame(StompFrame.disconnect());
            Thread.sleep(100); // Give server time to process
        } catch (Exception e) {
            // Log error
        }
        
        cleanup();
    }
    
    private void cleanup() {
        connected = false;
        connecting = false;
        
        try {
            if (socket != null && !socket.isClosed()) {
                socket.close();
            }
        } catch (IOException e) {
            // Log error
        }
        
        subscriptions.clear();
        readerTask.complete(null);
        notifyConnectionListener(false, "Disconnected");
    }
    
    // Subscription management
    public String subscribe(String destination, StompMessageListener listener) {
        if (!connected) {
            throw new IllegalStateException("Not connected to STOMP server");
        }
        
        String subscriptionId = "sub-" + subscriptionCounter.incrementAndGet();
        StompSubscription subscription = new StompSubscription(
            subscriptionId, destination, listener);
        
        subscriptions.put(subscriptionId, subscription);
        
        StompFrame subscribeFrame = StompFrame.subscribe(destination, subscriptionId);
        sendFrame(subscribeFrame);
        
        System.out.println("Subscribed to " + destination + 
            " with subscription ID: " + subscriptionId);
        
        return subscriptionId;
    }
    
    public void unsubscribe(String subscriptionId) {
        if (!connected) return;
        
        StompSubscription subscription = subscriptions.remove(subscriptionId);
        if (subscription != null) {
            StompFrame unsubscribeFrame = StompFrame.unsubscribe(subscriptionId);
            sendFrame(unsubscribeFrame);
            
            System.out.println("Unsubscribed from " + subscription.destination());
        }
    }
    
    // Message sending
    public void send(String destination, String message) {
        if (!connected) {
            throw new IllegalStateException("Not connected to STOMP server");
        }
        
        StompFrame sendFrame = StompFrame.send(destination, message);
        sendFrame(sendFrame);
        
        System.out.println("Sent message to " + destination + ": " + message);
    }
    
    // Frame processing
    private void sendFrame(StompFrame frame) {
        if (writer != null) {
            String serialized = frame.serialize();
            writer.print(serialized);
            writer.flush();
        }
    }
    
    private void readFrames() {
        try {
            InputStream inputStream = socket.getInputStream();
            
            while (!Thread.currentThread().isInterrupted()) {
                String frameData = readFrame(inputStream);
                if (frameData != null) {
                    StompFrame frame = StompFrame.parse(frameData);
                    if (frame != null) {
                        handleIncomingFrame(frame);
                    }
                } else {
                    break; // End of stream
                }
            }
        } catch (IOException e) {
            if (connected) {
                System.err.println("Frame reading error: " + e.getMessage());
                notifyConnectionListener(false, "Connection lost: " + e.getMessage());
            }
        } finally {
            cleanup();
        }
    }
    
    private String readFrame(InputStream inputStream) throws IOException {
        StringBuilder frameBuilder = new StringBuilder();
        int ch;
        
        while ((ch = inputStream.read()) != -1) {
            if (ch == 0) { // Null terminator found
                return frameBuilder.toString();
            }
            frameBuilder.append((char) ch);
        }
        
        return frameBuilder.length() > 0 ? frameBuilder.toString() : null;
    }
    
    private void handleIncomingFrame(StompFrame frame) {
        switch (frame.command()) {
            case "CONNECTED" -> handleConnected(frame);
            case "MESSAGE" -> handleMessage(frame);
            case "RECEIPT" -> handleReceipt(frame);
            case "ERROR" -> handleError(frame);
            default -> System.out.println("Received unknown frame: Command '" + frame.command() + "' not supported");
        }
    }
    
    private void handleConnected(StompFrame frame) {
        connected = true;
        connecting = false;
        sessionId = frame.headers().get("session");
        
        System.out.println("Successfully connected to STOMP server. Session: " + sessionId);
        notifyConnectionListener(true, "Connected");
    }
    
    private void handleMessage(StompFrame frame) {
        String destination = frame.headers().get("destination");
        String subscriptionId = frame.headers().get("subscription");
        
        if (destination != null && subscriptionId != null) {
            StompSubscription subscription = subscriptions.get(subscriptionId);
            if (subscription != null && subscription.listener() != null) {
                try {
                    subscription.listener().onMessage(destination, frame.body(), frame.headers());
                } catch (Exception e) {
                    System.err.println("Error in message listener: " + e.getMessage());
                }
            }
        }
    }
    
    private void handleReceipt(StompFrame frame) {
        String receiptId = frame.headers().get("receipt-id");
        System.out.println("Received receipt: " + receiptId);
    }
    
    private void handleError(StompFrame frame) {
        String message = frame.headers().get("message");
        System.err.println("STOMP Error: " + message);
        System.err.println("Details: " + frame.body());
        
        if (errorListener != null) {
            errorListener.onError(message, frame.body());
        }
    }
    
    // Listeners
    public void setConnectionListener(StompConnectionListener listener) {
        this.connectionListener = listener;
    }
    
    public void setErrorListener(StompErrorListener listener) {
        this.errorListener = listener;
    }
    
    private void notifyConnectionListener(boolean connected, String reason) {
        if (connectionListener != null) {
            try {
                connectionListener.onConnectionChanged(connected, reason);
            } catch (Exception e) {
                System.err.println("Error in connection listener: " + e.getMessage());
            }
        }
    }
    
    // Status methods
    public boolean isConnected() {
        return connected;
    }
    
    public String getSessionId() {
        return sessionId;
    }
    
    public Set<String> getActiveSubscriptions() {
        return new HashSet<>(subscriptions.keySet());
    }
    
    // Shutdown
    public void shutdown() {
        disconnect();
        frameProcessor.shutdown();
        try {
            if (!frameProcessor.awaitTermination(2, TimeUnit.SECONDS)) {
                frameProcessor.shutdownNow();
            }
        } catch (InterruptedException e) {
            frameProcessor.shutdownNow();
            Thread.currentThread().interrupt();
        }
    }
    
    // Demo/Test client
    public static void main(String[] args) {
        String host = args.length > 0 ? args[0] : "localhost";
        int port = args.length > 1 ? Integer.parseInt(args[1]) : 61613;
        
        StompClient client = new StompClient(host, port);
        
        // Set up listeners
        client.setConnectionListener((connected, reason) -> 
            System.out.println("Connection status: " + connected + " - " + reason));
        
        client.setErrorListener((message, details) -> 
            System.err.println("STOMP Error: " + message + "\nDetails: " + details));
        
        // Graceful shutdown
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            System.out.println("Shutting down STOMP client...");
            client.shutdown();
        }));
        
        try {
            // Connect to server
            client.connect().get();
            
            // Subscribe to a test topic
            String subscription = client.subscribe("/topic/test", 
                (destination, body, headers) -> 
                    System.out.println("Received message on " + destination + ": " + body));
            
            // Interactive console
            try (Scanner scanner = new Scanner(System.in)) {
                System.out.println("STOMP Client started. Commands:");
                System.out.println("  send <destination> <message> - Send a message");
                System.out.println("  subscribe <destination> - Subscribe to destination");
                System.out.println("  unsubscribe <subscription-id> - Unsubscribe");
                System.out.println("  quit - Exit");
                
                while (client.isConnected()) {
                    System.out.print("> ");
                    String input = scanner.nextLine().trim();
                    
                    if (input.equals("quit")) {
                        break;
                    }
                    
                    String[] parts = input.split(" ", 3);
                    
                    try {
                        switch (parts[0]) {
                            case "send" -> {
                                if (parts.length >= 3) {
                                    client.send(parts[1], parts[2]);
                                } else {
                                    System.out.println("Usage: send <destination> <message>");
                                }
                            }
                            case "subscribe" -> {
                                if (parts.length >= 2) {
                                    String subId = client.subscribe(parts[1], 
                                        (dest, body, headers) -> 
                                            System.out.println("[" + dest + "] " + body));
                                    System.out.println("Subscribed with ID: " + subId);
                                } else {
                                    System.out.println("Usage: subscribe <destination>");
                                }
                            }
                            case "unsubscribe" -> {
                                if (parts.length >= 2) {
                                    client.unsubscribe(parts[1]);
                                } else {
                                    System.out.println("Usage: unsubscribe <subscription-id>");
                                }
                            }
                            case "status" -> {
                                System.out.println("Connected: " + client.isConnected());
                                System.out.println("Session: " + client.getSessionId());
                                System.out.println("Active subscriptions: " + 
                                    client.getActiveSubscriptions());
                            }
                            default -> System.out.println("Unknown command: " + parts[0]);
                        }
                    } catch (Exception e) {
                        System.err.println("Error: " + e.getMessage());
                    }
                }
            }
            
        } catch (Exception e) {
            System.err.println("Client error: " + e.getMessage());
        } finally {
            client.shutdown();
        }
    }
}