#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

class StompFrame {
public:
    // Constructor
    StompFrame(const std::string& command, 
               const std::map<std::string, std::string>& headers,
               const std::string& body);
    
    // Getters
    const std::string& command() const { return command_; }
    const std::map<std::string, std::string>& headers() const { return headers_; }
    const std::string& body() const { return body_; }
    
    // Static factory methods for parsing
    static std::unique_ptr<StompFrame> parse(const std::string& frameData);
    
    // Serialization
    std::string serialize() const;
    
    // Client-side frame builders
    static std::unique_ptr<StompFrame> connect(const std::string& host, 
                                              const std::string& login = "", 
                                              const std::string& passcode = "");
    static std::unique_ptr<StompFrame> subscribe(const std::string& destination, 
                                               const std::string& subscriptionId);
    static std::unique_ptr<StompFrame> unsubscribe(const std::string& subscriptionId);
    static std::unique_ptr<StompFrame> send(const std::string& destination, 
                                          const std::string& body);
    static std::unique_ptr<StompFrame> disconnect();
    
    // Server-side frame builders
    static std::unique_ptr<StompFrame> connected(const std::string& session);
    static std::unique_ptr<StompFrame> receipt(const std::string& receiptId);
    static std::unique_ptr<StompFrame> message(const std::string& destination, 
                                             const std::string& messageId,
                                             const std::string& subscription, 
                                             const std::string& body);
    static std::unique_ptr<StompFrame> error(const std::string& message, 
                                           const std::string& details);

private:
    std::string command_;
    std::map<std::string, std::string> headers_;
    std::string body_;
    
    // Helper methods
    static std::vector<std::string> split(const std::string& str, char delimiter, int limit = -1);
    static std::string trim(const std::string& str);
};
