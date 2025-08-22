#include "StompServer.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>

// Utility function to generate UUID-like string
std::string generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

// StompSession implementation
StompSession::StompSession(const std::string& sessionId, int socket)
    : sessionId_(sessionId), socket_(socket) {
}

StompSession::~StompSession() {
    close();
}

void StompSession::sendFrame(const StompFrame& frame) {
    if (connected_ && socket_ >= 0) {
        std::string serialized = frame.serialize();
        ssize_t sent = send(socket_, serialized.c_str(), serialized.length(), 0);
        if (sent < 0) {
            connected_ = false;
        }
    }
}

void StompSession::subscribe(const std::string& subscriptionId, const std::string& destination) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions[subscriptionId] = destination;
}

void StompSession::unsubscribe(const std::string& subscriptionId) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions.erase(subscriptionId);
}

bool StompSession::isSubscribedTo(const std::string& destination) const {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    for (const auto& [id, dest] : subscriptions) {
        if (dest == destination) return true;
    }
    return false;
}

std::string StompSession::getSubscriptionId(const std::string& destination) const {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    for (const auto& [id, dest] : subscriptions) {
        if (dest == destination) return id;
    }
    return "";
}

void StompSession::close() {
    connected_ = false;
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
}

bool StompSession::isConnected() const {
    return connected_ && socket_ >= 0;
}

// MessageBroker implementation
void MessageBroker::subscribe(const std::string& destination, std::shared_ptr<StompSession> session) {
    std::lock_guard<std::mutex> lock(brokerMutex_);
    destinationSubscribers_[destination].insert(session);
}

void MessageBroker::unsubscribe(const std::string& destination, std::shared_ptr<StompSession> session) {
    std::lock_guard<std::mutex> lock(brokerMutex_);
    auto it = destinationSubscribers_.find(destination);
    if (it != destinationSubscribers_.end()) {
        it->second.erase(session);
        if (it->second.empty()) {
            destinationSubscribers_.erase(it);
        }
    }
}

void MessageBroker::broadcast(const std::string& destination, const std::string& body) {
    std::lock_guard<std::mutex> lock(brokerMutex_);
    auto it = destinationSubscribers_.find(destination);
    if (it != destinationSubscribers_.end()) {
        std::string messageId = generateUUID();
        
        // Remove disconnected sessions
        auto& subscribers = it->second;
        auto sessionIt = subscribers.begin();
        while (sessionIt != subscribers.end()) {
            if (!(*sessionIt)->isConnected()) {
                sessionIt = subscribers.erase(sessionIt);
            } else {
                ++sessionIt;
            }
        }
        
        // Send message to remaining subscribers
        for (auto& session : subscribers) {
            std::string subscriptionId = session->getSubscriptionId(destination);
            if (!subscriptionId.empty()) {
                auto message = StompFrame::message(destination, messageId, subscriptionId, body);
                session->sendFrame(*message);
            }
        }
    }
}

void MessageBroker::removeSession(std::shared_ptr<StompSession> session) {
    std::lock_guard<std::mutex> lock(brokerMutex_);
    for (auto& [dest, subscribers] : destinationSubscribers_) {
        subscribers.erase(session);
    }
}

// StompClientHandler implementation
StompClientHandler::StompClientHandler(int socket, std::shared_ptr<MessageBroker> broker,
                                     std::function<void(std::shared_ptr<StompSession>)> sessionCloseCallback)
    : clientSocket_(socket), broker_(broker), sessionCloseCallback_(sessionCloseCallback) {
    session_ = std::make_shared<StompSession>(generateUUID(), socket);
}

StompClientHandler::~StompClientHandler() {
    cleanup();
}

void StompClientHandler::run() {
    try {
        while (session_->isConnected()) {
            std::string frameData = readFrame();
            if (!frameData.empty()) {
                auto frame = StompFrame::parse(frameData);
                if (frame) {
                    handleFrame(std::move(frame));
                }
            } else {
                break; // End of stream
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Client handler error: " << e.what() << std::endl;
    }
    cleanup();
}

std::string StompClientHandler::readFrame() {
    std::string frameData;
    char ch;
    
    while (session_->isConnected()) {
        ssize_t received = recv(clientSocket_, &ch, 1, 0);
        if (received <= 0) {
            break; // Connection closed or error
        }
        
        if (ch == '\0') { // Null terminator found
            return frameData;
        }
        frameData += ch;
    }
    
    return frameData.empty() ? "" : frameData;
}

void StompClientHandler::handleFrame(std::unique_ptr<StompFrame> frame) {
    const std::string& command = frame->command();
    
    if (command == "CONNECT" || command == "STOMP") {
        handleConnect(*frame);
    } else if (command == "SUBSCRIBE") {
        handleSubscribe(*frame);
    } else if (command == "UNSUBSCRIBE") {
        handleUnsubscribe(*frame);
    } else if (command == "SEND") {
        handleSend(*frame);
    } else if (command == "DISCONNECT") {
        handleDisconnect(*frame);
    } else {
        auto error = StompFrame::error("Unknown command", 
                                     "Command '" + command + "' not supported");
        session_->sendFrame(*error);
    }
}

void StompClientHandler::handleConnect(const StompFrame& frame) {
    auto connected = StompFrame::connected(session_->getSessionId());
    session_->sendFrame(*connected);
    
    // Send receipt if requested
    auto it = frame.headers().find("receipt");
    if (it != frame.headers().end()) {
        auto receipt = StompFrame::receipt(it->second);
        session_->sendFrame(*receipt);
    }
    
    std::cout << "Client connected: " << session_->getSessionId() << std::endl;
}

void StompClientHandler::handleSubscribe(const StompFrame& frame) {
    auto destIt = frame.headers().find("destination");
    auto idIt = frame.headers().find("id");
    
    if (destIt != frame.headers().end() && idIt != frame.headers().end()) {
        session_->subscribe(idIt->second, destIt->second);
        broker_->subscribe(destIt->second, session_);
        std::cout << "Client " << session_->getSessionId() << 
                    " subscribed to " << destIt->second << std::endl;
    }
    
    auto receiptIt = frame.headers().find("receipt");
    if (receiptIt != frame.headers().end()) {
        auto receipt = StompFrame::receipt(receiptIt->second);
        session_->sendFrame(*receipt);
    }
}

void StompClientHandler::handleUnsubscribe(const StompFrame& frame) {
    auto idIt = frame.headers().find("id");
    
    if (idIt != frame.headers().end()) {
        std::string destination = session_->getSubscriptionId(idIt->second);
        if (!destination.empty()) {
            session_->unsubscribe(idIt->second);
            broker_->unsubscribe(destination, session_);
            std::cout << "Client " << session_->getSessionId() << 
                        " unsubscribed from " << destination << std::endl;
        }
    }
    
    auto receiptIt = frame.headers().find("receipt");
    if (receiptIt != frame.headers().end()) {
        auto receipt = StompFrame::receipt(receiptIt->second);
        session_->sendFrame(*receipt);
    }
}

void StompClientHandler::handleSend(const StompFrame& frame) {
    auto destIt = frame.headers().find("destination");
    
    if (destIt != frame.headers().end()) {
        broker_->broadcast(destIt->second, frame.body());
        std::cout << "Message sent to " << destIt->second << 
                    ": " << frame.body() << std::endl;
    }
    
    auto receiptIt = frame.headers().find("receipt");
    if (receiptIt != frame.headers().end()) {
        auto receipt = StompFrame::receipt(receiptIt->second);
        session_->sendFrame(*receipt);
    }
}

void StompClientHandler::handleDisconnect(const StompFrame& frame) {
    auto receiptIt = frame.headers().find("receipt");
    if (receiptIt != frame.headers().end()) {
        auto receipt = StompFrame::receipt(receiptIt->second);
        session_->sendFrame(*receipt);
    }
    
    cleanup();
}

void StompClientHandler::cleanup() {
    broker_->removeSession(session_);
    session_->close();
    sessionCloseCallback_(session_);
    std::cout << "Client disconnected: " << session_->getSessionId() << std::endl;
}

// StompServer implementation
StompServer::StompServer(int port) : port_(port), serverSocket_(-1) {
    broker_ = std::make_shared<MessageBroker>();
}

StompServer::~StompServer() {
    stop();
}

void StompServer::start() {
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        throw std::runtime_error("Failed to create server socket");
    }
    
    int opt = 1;
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(serverSocket_);
        throw std::runtime_error("Failed to set socket options");
    }
    
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port_);
    
    if (bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        ::close(serverSocket_);
        throw std::runtime_error("Failed to bind server socket");
    }
    
    if (listen(serverSocket_, 10) < 0) {
        ::close(serverSocket_);
        throw std::runtime_error("Failed to listen on server socket");
    }
    
    running_ = true;
    std::cout << "STOMP Server started on port " << port_ << std::endl;
    
    // Accept client connections
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            if (running_) {
                std::cerr << "Error accepting client connection" << std::endl;
            }
            continue;
        }
        
        std::cout << "New client connection from socket " << clientSocket << std::endl;
        
        try {
            auto handler = std::make_unique<StompClientHandler>(
                clientSocket, broker_, 
                [this](std::shared_ptr<StompSession> session) { removeSession(session); });
            
            std::lock_guard<std::mutex> lock(threadsMutex_);
            clientThreads_.emplace_back([handler = std::move(handler)]() {
                handler->run();
            });
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating client handler: " << e.what() << std::endl;
            ::close(clientSocket);
        }
    }
}

void StompServer::removeSession(std::shared_ptr<StompSession> session) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.erase(session);
}

void StompServer::stop() {
    running_ = false;
    
    if (serverSocket_ >= 0) {
        ::close(serverSocket_);
        serverSocket_ = -1;
    }
    
    cleanupThreads();
    std::cout << "STOMP Server stopped" << std::endl;
}

void StompServer::cleanupThreads() {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto& thread : clientThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    clientThreads_.clear();
}

void StompServer::runServer(int port) {
    StompServer server(port);
    
    // Setup signal handler for graceful shutdown
    signal(SIGINT, [](int) {
        std::cout << "\nShutting down STOMP Server..." << std::endl;
        exit(0);
    });
    
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Failed to start STOMP Server: " << e.what() << std::endl;
    }
}
