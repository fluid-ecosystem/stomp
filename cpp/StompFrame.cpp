#include "StompFrame.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

StompFrame::StompFrame(const std::string& command, 
                      const std::map<std::string, std::string>& headers,
                      const std::string& body)
    : command_(command), headers_(headers), body_(body) {
}

std::unique_ptr<StompFrame> StompFrame::parse(const std::string& frameData) {
    if (frameData.empty()) return nullptr;
    
    auto lines = split(frameData, '\n', -1);
    if (lines.empty()) return nullptr;
    
    std::string command = trim(lines[0]);
    std::map<std::string, std::string> headers;
    size_t bodyStart = 1;
    
    // Parse headers
    for (size_t i = 1; i < lines.size(); i++) {
        const std::string& line = lines[i];
        if (line.empty()) {
            bodyStart = i + 1;
            break;
        }
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos && colonPos < line.length() - 1) {
            std::string key = trim(line.substr(0, colonPos));
            std::string value = trim(line.substr(colonPos + 1));
            headers[key] = value;
        }
    }
    
    // Parse body
    std::ostringstream bodyBuilder;
    for (size_t i = bodyStart; i < lines.size(); i++) {
        if (i > bodyStart) bodyBuilder << "\n";
        bodyBuilder << lines[i];
    }
    std::string body = bodyBuilder.str();
    
    // Remove null terminator if present
    if (!body.empty() && body.back() == '\0') {
        body.pop_back();
    }
    
    return std::make_unique<StompFrame>(command, headers, body);
}

std::string StompFrame::serialize() const {
    std::ostringstream sb;
    sb << command_ << "\n";
    
    // Add headers
    for (const auto& [key, value] : headers_) {
        sb << key << ":" << value << "\n";
    }
    
    // Empty line separator
    sb << "\n";
    
    // Add body
    if (!body_.empty()) {
        sb << body_;
    }
    
    // Null terminator
    sb << '\0';
    
    return sb.str();
}

// Client-side frame builders
std::unique_ptr<StompFrame> StompFrame::connect(const std::string& host, 
                                               const std::string& login, 
                                               const std::string& passcode) {
    std::map<std::string, std::string> headers;
    headers["accept-version"] = "1.2";
    headers["host"] = host;
    if (!login.empty()) headers["login"] = login;
    if (!passcode.empty()) headers["passcode"] = passcode;
    
    return std::make_unique<StompFrame>("CONNECT", headers, "");
}

std::unique_ptr<StompFrame> StompFrame::subscribe(const std::string& destination, 
                                                 const std::string& subscriptionId) {
    std::map<std::string, std::string> headers;
    headers["destination"] = destination;
    headers["id"] = subscriptionId;
    
    return std::make_unique<StompFrame>("SUBSCRIBE", headers, "");
}

std::unique_ptr<StompFrame> StompFrame::unsubscribe(const std::string& subscriptionId) {
    std::map<std::string, std::string> headers;
    headers["id"] = subscriptionId;
    
    return std::make_unique<StompFrame>("UNSUBSCRIBE", headers, "");
}

std::unique_ptr<StompFrame> StompFrame::send(const std::string& destination, 
                                            const std::string& body) {
    std::map<std::string, std::string> headers;
    headers["destination"] = destination;
    
    return std::make_unique<StompFrame>("SEND", headers, body);
}

std::unique_ptr<StompFrame> StompFrame::disconnect() {
    return std::make_unique<StompFrame>("DISCONNECT", std::map<std::string, std::string>{}, "");
}

// Server-side frame builders
std::unique_ptr<StompFrame> StompFrame::connected(const std::string& session) {
    std::map<std::string, std::string> headers;
    headers["version"] = "1.2";
    headers["session"] = session;
    
    return std::make_unique<StompFrame>("CONNECTED", headers, "");
}

std::unique_ptr<StompFrame> StompFrame::receipt(const std::string& receiptId) {
    std::map<std::string, std::string> headers;
    headers["receipt-id"] = receiptId;
    
    return std::make_unique<StompFrame>("RECEIPT", headers, "");
}

std::unique_ptr<StompFrame> StompFrame::message(const std::string& destination, 
                                               const std::string& messageId,
                                               const std::string& subscription, 
                                               const std::string& body) {
    std::map<std::string, std::string> headers;
    headers["destination"] = destination;
    headers["message-id"] = messageId;
    headers["subscription"] = subscription;
    
    return std::make_unique<StompFrame>("MESSAGE", headers, body);
}

std::unique_ptr<StompFrame> StompFrame::error(const std::string& message, 
                                             const std::string& details) {
    std::map<std::string, std::string> headers;
    headers["message"] = message;
    
    return std::make_unique<StompFrame>("ERROR", headers, details);
}

// Helper methods
std::vector<std::string> StompFrame::split(const std::string& str, char delimiter, int limit) {
    std::vector<std::string> result;
    std::istringstream stream(str);
    std::string token;
    
    if (limit == -1) {
        while (std::getline(stream, token, delimiter)) {
            result.push_back(token);
        }
        // Handle trailing empty strings for compatibility with Java split behavior
        if (!str.empty() && str.back() == delimiter) {
            result.push_back("");
        }
    } else {
        int count = 0;
        while (count < limit - 1 && std::getline(stream, token, delimiter)) {
            result.push_back(token);
            count++;
        }
        // Add remaining as last element
        if (std::getline(stream, token, '\0')) {
            result.push_back(token);
        }
    }
    
    return result;
}

std::string StompFrame::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
