#include "StompClient.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<int> received_count{0};
std::atomic<bool> connection_success{false};

void onMessage(const std::string& dest, const std::string& body, 
              const std::map<std::string, std::string>& headers) {
    std::cout << "📨 [C++] Received: " << body << " on " << dest << std::endl;
    received_count++;
}

int main() {
    try {
        std::cout << "🔌 [C++] Creating client with credentials..." << std::endl;
        StompClient client("localhost", 61613, "cpp_client", "test123");
        
        std::cout << "🔌 [C++] Connecting to C++ server..." << std::endl;
        auto future = client.connect();
        
        // Wait for connection with timeout
        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            std::cout << "✅ [C++] Connected to C++ server!" << std::endl;
            connection_success = true;
            
            // Give connection time to fully establish
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Subscribe to test queue
            std::cout << "📡 [C++] Subscribing to /queue/cross-test..." << std::endl;
            std::string subId = client.subscribe("/queue/cross-test", onMessage);
            std::cout << "✅ [C++] Subscribed with ID: " << subId << std::endl;
            
            // Send test messages
            std::cout << "📤 [C++] Sending messages to C++ server..." << std::endl;
            client.send("/queue/cross-test", "Hello C++ from C++!");
            client.send("/queue/cross-test", "Self-test message 1");
            client.send("/queue/cross-test", "Self-test message 2");
            
            // Wait for any responses
            std::cout << "⏳ [C++] Waiting for message processing..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            // Test different destinations
            client.send("/topic/broadcast", "C++ broadcast message");
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // Cleanup
            std::cout << "🧹 [C++] Cleaning up..." << std::endl;
            client.unsubscribe(subId);
            client.disconnect();
            
            std::cout << "📊 [C++] Test Results:" << std::endl;
            std::cout << "   Connection: " << (connection_success ? "SUCCESS" : "FAILED") << std::endl;
            std::cout << "   Messages received: " << received_count.load() << std::endl;
            std::cout << "   Status: " << (connection_success ? "PASSED" : "FAILED") << std::endl;
            
            if (!connection_success) {
                return 1;
            }
            
            std::cout << "🎉 [C++] Self-test completed successfully!" << std::endl;
        } else {
            std::cout << "❌ [C++] Connection timeout!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ [C++] Test failed with error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
