import java.util.*;

// STOMP Frame representation
record StompFrame(String command, Map<String, String> headers, String body) {
    
    public static StompFrame parse(String frameData) {
        if (frameData == null || frameData.trim().isEmpty()) return null;
        
        // Use split with limit to preserve trailing empty strings
        String[] lines = frameData.split("\n", -1);
        if (lines.length == 0) return null;
        
        String command = lines[0].trim();
        Map<String, String> headers = new HashMap<>();
        int bodyStart = 1;
        
        // Parse headers
        for (int i = 1; i < lines.length; i++) {
            String line = lines[i];
            if (line.isEmpty()) {
                bodyStart = i + 1;
                break;
            }
            String[] headerParts = line.split(":", 2);
            if (headerParts.length == 2) {
                headers.put(headerParts[0].trim(), headerParts[1].trim());
            }
        }
        
        // Parse body
        StringBuilder bodyBuilder = new StringBuilder();
        for (int i = bodyStart; i < lines.length; i++) {
            if (i > bodyStart) bodyBuilder.append("\n");
            bodyBuilder.append(lines[i]);
        }
        String body = bodyBuilder.toString();
        
        // Remove null terminator if present
        if (body.endsWith("\0")) {
            body = body.substring(0, body.length() - 1);
        }
        
        return new StompFrame(command, headers, body);
    }
    
    public String serialize() {
        StringBuilder sb = new StringBuilder();
        sb.append(command).append("\n");
        
        // Add headers
        for (Map.Entry<String, String> entry : headers.entrySet()) {
            sb.append(entry.getKey()).append(":").append(entry.getValue()).append("\n");
        }
        
        // Empty line separator
        sb.append("\n");
        
        // Add body
        if (body != null && !body.isEmpty()) {
            sb.append(body);
        }
        
        // Null terminator
        sb.append("\0");
        
        return sb.toString();
    }
    
    // Client-side frame builders
    public static StompFrame connect(String host, String login, String passcode) {
        Map<String, String> headers = new HashMap<>();
        headers.put("accept-version", "1.2");
        headers.put("host", host);
        if (login != null) headers.put("login", login);
        if (passcode != null) headers.put("passcode", passcode);
        
        return new StompFrame("CONNECT", headers, "");
    }
    
    public static StompFrame subscribe(String destination, String subscriptionId) {
        return new StompFrame("SUBSCRIBE", 
            Map.of("destination", destination, "id", subscriptionId), "");
    }
    
    public static StompFrame unsubscribe(String subscriptionId) {
        return new StompFrame("UNSUBSCRIBE", 
            Map.of("id", subscriptionId), "");
    }
    
    public static StompFrame send(String destination, String body) {
        return new StompFrame("SEND", 
            Map.of("destination", destination), body);
    }
    
    public static StompFrame disconnect() {
        return new StompFrame("DISCONNECT", Map.of(), "");
    }
    
    // Server-side frame builders
    public static StompFrame connected(String session) {
        return new StompFrame("CONNECTED", 
            Map.of("version", "1.2", "session", session), "");
    }
    
    public static StompFrame receipt(String receiptId) {
        return new StompFrame("RECEIPT", 
            Map.of("receipt-id", receiptId), "");
    }
    
    public static StompFrame message(String destination, String messageId, 
                                   String subscription, String body) {
        return new StompFrame("MESSAGE", 
            Map.of("destination", destination, "message-id", messageId, 
                   "subscription", subscription), body);
    }
    
    public static StompFrame error(String message, String details) {
        return new StompFrame("ERROR", 
            Map.of("message", message), details);
    }
}
