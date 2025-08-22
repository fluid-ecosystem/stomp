import java.io.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;

// Client session management
class StompSession {
    private final String sessionId;
    private final Socket socket;
    private final PrintWriter writer;
    public final Map<String, String> subscriptions = new ConcurrentHashMap<>();
    private volatile boolean connected = true;
    
    public StompSession(String sessionId, Socket socket) throws IOException {
        this.sessionId = sessionId;
        this.socket = socket;
        this.writer = new PrintWriter(socket.getOutputStream(), true);
    }
    
    public String getSessionId() { return sessionId; }
    
    public void sendFrame(StompFrame frame) {
        if (connected && !socket.isClosed()) {
            writer.print(frame.serialize());
            writer.flush();
        }
    }
    
    public void subscribe(String subscriptionId, String destination) {
        subscriptions.put(subscriptionId, destination);
    }
    
    public void unsubscribe(String subscriptionId) {
        subscriptions.remove(subscriptionId);
    }
    
    public boolean isSubscribedTo(String destination) {
        return subscriptions.containsValue(destination);
    }
    
    public String getSubscriptionId(String destination) {
        return subscriptions.entrySet().stream()
            .filter(e -> e.getValue().equals(destination))
            .map(Map.Entry::getKey)
            .findFirst()
            .orElse(null);
    }
    
    public void close() {
        connected = false;
        try {
            if (!socket.isClosed()) {
                socket.close();
            }
        } catch (IOException e) {
            // Log error in production
        }
    }
    
    public boolean isConnected() {
        return connected && !socket.isClosed();
    }
}

// Message broker for handling destinations
class MessageBroker {
    private final Map<String, Set<StompSession>> destinationSubscribers = 
        new ConcurrentHashMap<>();
    
    public void subscribe(String destination, StompSession session) {
        destinationSubscribers
            .computeIfAbsent(destination, k -> ConcurrentHashMap.newKeySet())
            .add(session);
    }
    
    public void unsubscribe(String destination, StompSession session) {
        Set<StompSession> subscribers = destinationSubscribers.get(destination);
        if (subscribers != null) {
            subscribers.remove(session);
            if (subscribers.isEmpty()) {
                destinationSubscribers.remove(destination);
            }
        }
    }
    
    public void broadcast(String destination, String body) {
        Set<StompSession> subscribers = destinationSubscribers.get(destination);
        if (subscribers != null) {
            String messageId = UUID.randomUUID().toString();
            subscribers.removeIf(session -> !session.isConnected());
            
            subscribers.forEach(session -> {
                String subscriptionId = session.getSubscriptionId(destination);
                if (subscriptionId != null) {
                    StompFrame message = StompFrame.message(destination, 
                        messageId, subscriptionId, body);
                    session.sendFrame(message);
                }
            });
        }
    }
    
    public void removeSession(StompSession session) {
        destinationSubscribers.values().forEach(subscribers -> 
            subscribers.remove(session));
    }
}

// Client handler for processing STOMP commands
class StompClientHandler implements Runnable {
    private final Socket clientSocket;
    private final StompSession session;
    private final MessageBroker broker;
    private final Consumer<StompSession> sessionCloseCallback;
    
    public StompClientHandler(Socket socket, MessageBroker broker, 
                            Consumer<StompSession> sessionCloseCallback) 
            throws IOException {
        this.clientSocket = socket;
        this.session = new StompSession(UUID.randomUUID().toString(), socket);
        this.broker = broker;
        this.sessionCloseCallback = sessionCloseCallback;
    }
    
    @Override
    public void run() {
        try {
            InputStream inputStream = clientSocket.getInputStream();
            
            while (session.isConnected()) {
                String frameData = readFrame(inputStream);
                if (frameData != null) {
                    StompFrame frame = StompFrame.parse(frameData);
                    if (frame != null) {
                        handleFrame(frame);
                    }
                } else {
                    break; // End of stream
                }
            }
        } catch (IOException e) {
            System.err.println("Client handler error: " + e.getMessage());
        } finally {
            cleanup();
        }
    }
    
    private String readFrame(InputStream inputStream) throws IOException {
        StringBuilder frameBuilder = new StringBuilder();
        int ch;
        
        while ((ch = inputStream.read()) != -1) {
            if (ch == 0) { // Null terminator found
                String frame = frameBuilder.toString();
                return frame;
            }
            frameBuilder.append((char) ch);
        }
        
        return frameBuilder.length() > 0 ? frameBuilder.toString() : null;
    }
    
    private void handleFrame(StompFrame frame) {
        switch (frame.command()) {
            case "CONNECT", "STOMP" -> handleConnect(frame);
            case "SUBSCRIBE" -> handleSubscribe(frame);
            case "UNSUBSCRIBE" -> handleUnsubscribe(frame);
            case "SEND" -> handleSend(frame);
            case "DISCONNECT" -> handleDisconnect(frame);
            default -> {
                StompFrame error = StompFrame.error("Unknown command", 
                    "Command '" + frame.command() + "' not supported");
                session.sendFrame(error);
            }
        }
    }
    
    private void handleConnect(StompFrame frame) {
        StompFrame connected = StompFrame.connected(session.getSessionId());
        session.sendFrame(connected);
        
        // Send receipt if requested
        String receipt = frame.headers().get("receipt");
        if (receipt != null) {
            session.sendFrame(StompFrame.receipt(receipt));
        }
        
        System.out.println("Client connected: " + session.getSessionId());
    }
    
    private void handleSubscribe(StompFrame frame) {
        String destination = frame.headers().get("destination");
        String subscriptionId = frame.headers().get("id");
        
        if (destination != null && subscriptionId != null) {
            session.subscribe(subscriptionId, destination);
            broker.subscribe(destination, session);
            System.out.println("Client " + session.getSessionId() + 
                " subscribed to " + destination);
        }
        
        String receipt = frame.headers().get("receipt");
        if (receipt != null) {
            session.sendFrame(StompFrame.receipt(receipt));
        }
    }
    
    private void handleUnsubscribe(StompFrame frame) {
        String subscriptionId = frame.headers().get("id");
        
        if (subscriptionId != null) {
            String destination = session.subscriptions.get(subscriptionId);
            if (destination != null) {
                session.unsubscribe(subscriptionId);
                broker.unsubscribe(destination, session);
                System.out.println("Client " + session.getSessionId() + 
                    " unsubscribed from " + destination);
            }
        }
        
        String receipt = frame.headers().get("receipt");
        if (receipt != null) {
            session.sendFrame(StompFrame.receipt(receipt));
        }
    }
    
    private void handleSend(StompFrame frame) {
        String destination = frame.headers().get("destination");
        
        if (destination != null) {
            broker.broadcast(destination, frame.body());
            System.out.println("Message sent to " + destination + 
                ": " + frame.body());
        }
        
        String receipt = frame.headers().get("receipt");
        if (receipt != null) {
            session.sendFrame(StompFrame.receipt(receipt));
        }
    }
    
    private void handleDisconnect(StompFrame frame) {
        String receipt = frame.headers().get("receipt");
        if (receipt != null) {
            session.sendFrame(StompFrame.receipt(receipt));
        }
        
        cleanup();
    }
    
    private void cleanup() {
        broker.removeSession(session);
        session.close();
        sessionCloseCallback.accept(session);
        System.out.println("Client disconnected: " + session.getSessionId());
    }
}

// Main STOMP Server
public class StompServer {
    private final int port;
    private final MessageBroker broker = new MessageBroker();
    private final Set<StompSession> activeSessions = ConcurrentHashMap.newKeySet();
    private final ExecutorService clientHandlerPool = 
        Executors.newCachedThreadPool();
    private volatile boolean running = false;
    private ServerSocket serverSocket;
    
    public StompServer(int port) {
        this.port = port;
    }
    
    public void start() throws IOException {
        serverSocket = new ServerSocket(port);
        running = true;
        
        System.out.println("STOMP Server started on port " + port);
        
        // Accept client connections
        while (running && !serverSocket.isClosed()) {
            try {
                Socket clientSocket = serverSocket.accept();
                System.out.println("New client connection: " + 
                    clientSocket.getRemoteSocketAddress());
                
                StompClientHandler handler = new StompClientHandler(
                    clientSocket, broker, this::removeSession);
                    
                clientHandlerPool.submit(handler);
                
            } catch (IOException e) {
                if (running) {
                    System.err.println("Error accepting client: " + e.getMessage());
                }
            }
        }
    }
    
    private void removeSession(StompSession session) {
        activeSessions.remove(session);
    }
    
    public void stop() {
        running = false;
        try {
            if (serverSocket != null && !serverSocket.isClosed()) {
                serverSocket.close();
            }
        } catch (IOException e) {
            System.err.println("Error closing server socket: " + e.getMessage());
        }
        
        clientHandlerPool.shutdown();
        try {
            if (!clientHandlerPool.awaitTermination(5, TimeUnit.SECONDS)) {
                clientHandlerPool.shutdownNow();
            }
        } catch (InterruptedException e) {
            clientHandlerPool.shutdownNow();
            Thread.currentThread().interrupt();
        }
        
        System.out.println("STOMP Server stopped");
    }
    
    // Main method for running the server
    public static void main(String[] args) {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 61613;
        
        StompServer server = new StompServer(port);
        
        // Graceful shutdown hook
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            System.out.println("Shutting down STOMP Server...");
            server.stop();
        }));
        
        try {
            server.start();
        } catch (IOException e) {
            System.err.println("Failed to start STOMP Server: " + e.getMessage());
        }
    }
}