#include "StompClient.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

// STOMP Client Implementation
StompClient::StompClient(const std::string& host, int port)
    : StompClient(host, port, "", "") {
}

StompClient::StompClient(const std::string& host, int port, const std::string& login, const std::string& passcode)
    : host_(host), port_(port), login_(login), passcode_(passcode), socket_(-1) {
}

StompClient::~StompClient() {
    shutdown();
}

std::future<void> StompClient::connect() {
    if (connected_ || connecting_) {
        std::promise<void> promise;
        promise.set_value();
        return promise.get_future();
    }
    
    connecting_ = true;
    
    return std::async(std::launch::async, [this]() {
        try {
            // Create socket
            socket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_ < 0) {
                throw std::runtime_error("Failed to create socket");
            }
            
            // Setup server address
            struct sockaddr_in serverAddr;
            std::memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port_);
            
            if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) <= 0) {
                throw std::runtime_error("Invalid address/Address not supported");
            }
            
            // Connect to server
            if (::connect(socket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                throw std::runtime_error("Connection failed");
            }
            
            // Send CONNECT frame
            auto connectFrame = StompFrame::connect(host_, login_, passcode_);
            sendFrame(*connectFrame);
            
            // Start frame reader
            shouldStop_ = false;
            frameProcessorThread_ = std::thread(&StompClient::readFrames, this);
            
            std::cout << "Connected to STOMP server at " << host_ << ":" << port_ << std::endl;
            
            // Wait for CONNECTED response
            auto startTime = std::chrono::steady_clock::now();
            while (!connected_ && connecting_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
                if (elapsed.count() > 5000) { // 5 second timeout
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            if (!connected_) {
                connecting_ = false;
                cleanup();
                throw std::runtime_error("Connection timeout - no CONNECTED frame received");
            }
            
        } catch (const std::exception& e) {
            connecting_ = false;
            notifyConnectionListener(false, "Connection failed: " + std::string(e.what()));
            throw;
        }
    });
}

void StompClient::disconnect() {
    if (!connected_) return;
    
    try {
        // Send DISCONNECT frame
        auto disconnectFrame = StompFrame::disconnect();
        sendFrame(*disconnectFrame);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server time to process
    } catch (const std::exception& e) {
        // Log error but continue cleanup
        std::cerr << "Error during disconnect: " << e.what() << std::endl;
    }
    
    cleanup();
}

void StompClient::cleanup() {
    connected_ = false;
    connecting_ = false;
    shouldStop_ = true;
    
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    
    if (frameProcessorThread_.joinable()) {
        frameProcessorThread_.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        subscriptions_.clear();
    }
    
    notifyConnectionListener(false, "Disconnected");
}

std::string StompClient::subscribe(const std::string& destination, StompMessageListener listener) {
    if (!connected_) {
        throw std::runtime_error("Not connected to STOMP server");
    }
    
    std::string subscriptionId = "sub-" + std::to_string(subscriptionCounter_.fetch_add(1) + 1);
    auto subscription = std::make_unique<StompSubscription>(subscriptionId, destination, std::move(listener));
    
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        subscriptions_[subscriptionId] = std::move(subscription);
    }
    
    auto subscribeFrame = StompFrame::subscribe(destination, subscriptionId);
    sendFrame(*subscribeFrame);
    
    std::cout << "Subscribed to " << destination << " with subscription ID: " << subscriptionId << std::endl;
    
    return subscriptionId;
}

void StompClient::unsubscribe(const std::string& subscriptionId) {
    if (!connected_) return;
    
    std::unique_ptr<StompSubscription> subscription;
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        auto it = subscriptions_.find(subscriptionId);
        if (it != subscriptions_.end()) {
            subscription = std::move(it->second);
            subscriptions_.erase(it);
        }
    }
    
    if (subscription) {
        auto unsubscribeFrame = StompFrame::unsubscribe(subscriptionId);
        sendFrame(*unsubscribeFrame);
        
        std::cout << "Unsubscribed from " << subscription->destination << std::endl;
    }
}

void StompClient::send(const std::string& destination, const std::string& message) {
    if (!connected_) {
        throw std::runtime_error("Not connected to STOMP server");
    }
    
    auto sendFramePtr = StompFrame::send(destination, message);
    sendFrame(*sendFramePtr);
    
    std::cout << "Sent message to " << destination << ": " << message << std::endl;
}

void StompClient::sendFrame(const StompFrame& frame) {
    if (socket_ >= 0) {
        std::string serialized = frame.serialize();
        ssize_t sent = ::send(socket_, serialized.c_str(), serialized.length(), 0);
        if (sent < 0) {
            throw std::runtime_error("Failed to send frame");
        }
    }
}

void StompClient::readFrames() {
    try {
        while (!shouldStop_ && connected_) {
            std::string frameData = readFrame();
            if (!frameData.empty()) {
                auto frame = StompFrame::parse(frameData);
                if (frame) {
                    handleIncomingFrame(std::move(frame));
                }
            } else {
                break; // End of stream
            }
        }
    } catch (const std::exception& e) {
        if (connected_) {
            std::cerr << "Frame reading error: " << e.what() << std::endl;
            notifyConnectionListener(false, "Connection lost: " + std::string(e.what()));
        }
    }
    cleanup();
}

std::string StompClient::readFrame() {
    std::string frameData;
    char ch;
    
    while (!shouldStop_ && socket_ >= 0) {
        ssize_t received = recv(socket_, &ch, 1, 0);
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

void StompClient::handleIncomingFrame(std::unique_ptr<StompFrame> frame) {
    const std::string& command = frame->command();
    
    if (command == "CONNECTED") {
        handleConnected(*frame);
    } else if (command == "MESSAGE") {
        handleMessage(*frame);
    } else if (command == "RECEIPT") {
        handleReceipt(*frame);
    } else if (command == "ERROR") {
        handleError(*frame);
    } else {
        std::cout << "Received unknown frame: Command '" << command << "' not supported" << std::endl;
    }
}

void StompClient::handleConnected(const StompFrame& frame) {
    connected_ = true;
    connecting_ = false;
    
    auto it = frame.headers().find("session");
    if (it != frame.headers().end()) {
        sessionId_ = it->second;
    }
    
    std::cout << "Successfully connected to STOMP server. Session: " << sessionId_ << std::endl;
    notifyConnectionListener(true, "Connected");
}

void StompClient::handleMessage(const StompFrame& frame) {
    auto destIt = frame.headers().find("destination");
    auto subIt = frame.headers().find("subscription");
    
    if (destIt != frame.headers().end() && subIt != frame.headers().end()) {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        auto it = subscriptions_.find(subIt->second);
        if (it != subscriptions_.end() && it->second->listener) {
            try {
                it->second->listener(destIt->second, frame.body(), frame.headers());
            } catch (const std::exception& e) {
                std::cerr << "Error in message listener: " << e.what() << std::endl;
            }
        }
    }
}

void StompClient::handleReceipt(const StompFrame& frame) {
    auto it = frame.headers().find("receipt-id");
    if (it != frame.headers().end()) {
        std::cout << "Received receipt: " << it->second << std::endl;
    }
}

void StompClient::handleError(const StompFrame& frame) {
    auto it = frame.headers().find("message");
    std::string message = (it != frame.headers().end()) ? it->second : "Unknown error";
    
    std::cerr << "STOMP Error: " << message << std::endl;
    std::cerr << "Details: " << frame.body() << std::endl;
    
    if (errorListener_) {
        errorListener_(message, frame.body());
    }
}

void StompClient::setConnectionListener(StompConnectionListener listener) {
    connectionListener_ = std::move(listener);
}

void StompClient::setErrorListener(StompErrorListener listener) {
    errorListener_ = std::move(listener);
}

void StompClient::notifyConnectionListener(bool connected, const std::string& reason) {
    if (connectionListener_) {
        try {
            connectionListener_(connected, reason);
        } catch (const std::exception& e) {
            std::cerr << "Error in connection listener: " << e.what() << std::endl;
        }
    }
}

std::set<std::string> StompClient::getActiveSubscriptions() const {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    std::set<std::string> result;
    for (const auto& [id, subscription] : subscriptions_) {
        result.insert(id);
    }
    return result;
}

void StompClient::shutdown() {
    disconnect();
    if (frameProcessorThread_.joinable()) {
        frameProcessorThread_.join();
    }
}

void StompClient::runTestClient(const std::string& host, int port) {
    StompClient client(host, port);
    
    // Set up listeners
    client.setConnectionListener([](bool connected, const std::string& reason) {
        std::cout << "Connection status: " << connected << " - " << reason << std::endl;
    });
    
    client.setErrorListener([](const std::string& message, const std::string& details) {
        std::cerr << "STOMP Error: " << message << "\nDetails: " << details << std::endl;
    });
    
    try {
        // Connect to server
        client.connect().get();
        
        // Subscribe to a test topic
        std::string subscription = client.subscribe("/topic/test", 
            [](const std::string& destination, const std::string& body, const std::map<std::string, std::string>& /* headers */) {
                std::cout << "Received message on " << destination << ": " << body << std::endl;
            });
        
        // Interactive console
        std::cout << "STOMP Client started. Commands:" << std::endl;
        std::cout << "  send <destination> <message> - Send a message" << std::endl;
        std::cout << "  subscribe <destination> - Subscribe to destination" << std::endl;
        std::cout << "  unsubscribe <subscription-id> - Unsubscribe" << std::endl;
        std::cout << "  quit - Exit" << std::endl;
        
        std::string input;
        while (client.isConnected() && std::getline(std::cin, input)) {
            if (input.empty()) continue;
            
            if (input == "quit") {
                break;
            }
            
            std::istringstream iss(input);
            std::string command;
            iss >> command;
            
            try {
                if (command == "send") {
                    std::string destination, message;
                    iss >> destination;
                    std::getline(iss, message);
                    if (!message.empty() && message[0] == ' ') {
                        message = message.substr(1); // Remove leading space
                    }
                    
                    if (!destination.empty() && !message.empty()) {
                        client.send(destination, message);
                    } else {
                        std::cout << "Usage: send <destination> <message>" << std::endl;
                    }
                } else if (command == "subscribe") {
                    std::string destination;
                    iss >> destination;
                    
                    if (!destination.empty()) {
                        std::string subId = client.subscribe(destination, 
                            [destination](const std::string& dest, const std::string& body, const std::map<std::string, std::string>& /* headers */) {
                                std::cout << "Message on " << dest << ": " << body << std::endl;
                            });
                        std::cout << "Subscribed with ID: " << subId << std::endl;
                    } else {
                        std::cout << "Usage: subscribe <destination>" << std::endl;
                    }
                } else if (command == "unsubscribe") {
                    std::string subscriptionId;
                    iss >> subscriptionId;
                    
                    if (!subscriptionId.empty()) {
                        client.unsubscribe(subscriptionId);
                    } else {
                        std::cout << "Usage: unsubscribe <subscription-id>" << std::endl;
                    }
                } else if (command == "status") {
                    std::cout << "Connected: " << client.isConnected() << std::endl;
                } else {
                    std::cout << "Unknown command: " << command << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
    
    client.shutdown();
}
