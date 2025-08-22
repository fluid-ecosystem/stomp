#pragma once

#include "StompFrame.hpp"
#include <string>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <future>
#include <condition_variable>
#include <vector>

// Forward declarations
class StompSession;
class MessageBroker;
class StompClientHandler;

// Client session management
class StompSession {
private:
    std::string sessionId_;
    int socket_;
    std::atomic<bool> connected_{true};
    mutable std::mutex subscriptionsMutex_;
    
public:
    std::map<std::string, std::string> subscriptions;
    
    StompSession(const std::string& sessionId, int socket);
    ~StompSession();
    
    const std::string& getSessionId() const { return sessionId_; }
    void sendFrame(const StompFrame& frame);
    void subscribe(const std::string& subscriptionId, const std::string& destination);
    void unsubscribe(const std::string& subscriptionId);
    bool isSubscribedTo(const std::string& destination) const;
    std::string getSubscriptionId(const std::string& destination) const;
    void close();
    bool isConnected() const;
    int getSocket() const { return socket_; }
};

// Message broker for handling destinations
class MessageBroker {
private:
    std::map<std::string, std::set<std::shared_ptr<StompSession>>> destinationSubscribers_;
    mutable std::mutex brokerMutex_;
    
public:
    void subscribe(const std::string& destination, std::shared_ptr<StompSession> session);
    void unsubscribe(const std::string& destination, std::shared_ptr<StompSession> session);
    void broadcast(const std::string& destination, const std::string& body);
    void removeSession(std::shared_ptr<StompSession> session);
};

// Client handler for processing STOMP commands
class StompClientHandler {
private:
    int clientSocket_;
    std::shared_ptr<StompSession> session_;
    std::shared_ptr<MessageBroker> broker_;
    std::function<void(std::shared_ptr<StompSession>)> sessionCloseCallback_;
    
    std::string readFrame();
    void handleFrame(std::unique_ptr<StompFrame> frame);
    void handleConnect(const StompFrame& frame);
    void handleSubscribe(const StompFrame& frame);
    void handleUnsubscribe(const StompFrame& frame);
    void handleSend(const StompFrame& frame);
    void handleDisconnect(const StompFrame& frame);
    void cleanup();
    
public:
    StompClientHandler(int socket, std::shared_ptr<MessageBroker> broker,
                      std::function<void(std::shared_ptr<StompSession>)> sessionCloseCallback);
    ~StompClientHandler();
    
    void run();
};

// Main STOMP Server
class StompServer {
private:
    int port_;
    std::shared_ptr<MessageBroker> broker_;
    std::set<std::shared_ptr<StompSession>> activeSessions_;
    std::atomic<bool> running_{false};
    int serverSocket_;
    std::vector<std::thread> clientThreads_;
    mutable std::mutex sessionsMutex_;
    mutable std::mutex threadsMutex_;
    
    void removeSession(std::shared_ptr<StompSession> session);
    void cleanupThreads();
    
public:
    StompServer(int port);
    ~StompServer();
    
    void start();
    void stop();
    
    // Main method for running the server
    static void runServer(int port = 61613);
};
