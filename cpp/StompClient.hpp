#pragma once

#include "StompFrame.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <future>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <condition_variable>

// Forward declarations
class StompSubscription;

// Message listener interface
using StompMessageListener = std::function<void(const std::string& destination, 
                                               const std::string& body, 
                                               const std::map<std::string, std::string>& headers)>;

// Connection state listener
using StompConnectionListener = std::function<void(bool connected, const std::string& reason)>;

// Error listener
using StompErrorListener = std::function<void(const std::string& message, const std::string& details)>;

// Subscription holder
class StompSubscription {
public:
    std::string subscriptionId;
    std::string destination;
    StompMessageListener listener;
    
    StompSubscription(const std::string& id, const std::string& dest, StompMessageListener l)
        : subscriptionId(id), destination(dest), listener(std::move(l)) {}
};

// STOMP Client Implementation
class StompClient {
private:
    std::string host_;
    int port_;
    std::string login_;
    std::string passcode_;
    
    int socket_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> connecting_{false};
    std::string sessionId_;
    
    // Listeners
    StompConnectionListener connectionListener_;
    StompErrorListener errorListener_;
    
    // Subscriptions management
    std::map<std::string, std::unique_ptr<StompSubscription>> subscriptions_;
    std::atomic<int> subscriptionCounter_{0};
    mutable std::mutex subscriptionsMutex_;
    
    // Frame processing
    std::thread frameProcessorThread_;
    std::atomic<bool> shouldStop_{false};
    
    // Internal methods
    void sendFrame(const StompFrame& frame);
    void readFrames();
    std::string readFrame();
    void handleIncomingFrame(std::unique_ptr<StompFrame> frame);
    void handleConnected(const StompFrame& frame);
    void handleMessage(const StompFrame& frame);
    void handleReceipt(const StompFrame& frame);
    void handleError(const StompFrame& frame);
    void cleanup();
    void notifyConnectionListener(bool connected, const std::string& reason);
    
public:
    StompClient(const std::string& host, int port);
    StompClient(const std::string& host, int port, const std::string& login, const std::string& passcode);
    ~StompClient();
    
    // Connection management
    std::future<void> connect();
    void disconnect();
    
    // Subscription management
    std::string subscribe(const std::string& destination, StompMessageListener listener);
    void unsubscribe(const std::string& subscriptionId);
    
    // Message sending
    void send(const std::string& destination, const std::string& message);
    
    // Listeners
    void setConnectionListener(StompConnectionListener listener);
    void setErrorListener(StompErrorListener listener);
    
    // Status methods
    bool isConnected() const { return connected_; }
    const std::string& getSessionId() const { return sessionId_; }
    std::set<std::string> getActiveSubscriptions() const;
    
    // Shutdown
    void shutdown();
    
    // Demo/Test client
    static void runTestClient(const std::string& host = "localhost", int port = 61613);
};
