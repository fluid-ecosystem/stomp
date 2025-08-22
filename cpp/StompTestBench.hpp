#pragma once

#include "StompClient.hpp"
#include "StompServer.hpp"
#include <vector>
#include <chrono>
#include <atomic>
#include <memory>
#include <future>
#include <random>

// Test Result tracking
struct TestResult {
    std::string testName;
    bool passed;
    std::string message;
    long durationMs;
    
    TestResult(const std::string& name, bool p, const std::string& msg, long duration)
        : testName(name), passed(p), message(msg), durationMs(duration) {}
    
    std::string toString() const {
        std::string status = passed ? "PASS" : "FAIL";
        return "[" + status + "] " + testName + " (" + std::to_string(durationMs) + "ms) - " + message;
    }
};

// Test Statistics
class TestStats {
private:
    std::atomic<int> totalTests_{0};
    std::atomic<int> passedTests_{0};
    std::atomic<int> failedTests_{0};
    std::vector<TestResult> results_;
    mutable std::mutex resultsMutex_;
    
public:
    void addResult(const TestResult& result);
    void printSummary() const;
};

// Message collector for testing
struct ReceivedMessage {
    std::string destination;
    std::string body;
    std::map<std::string, std::string> headers;
    long timestamp;
    
    ReceivedMessage(const std::string& dest, const std::string& b, 
                   const std::map<std::string, std::string>& h, long ts)
        : destination(dest), body(b), headers(h), timestamp(ts) {}
};

class MessageCollector {
private:
    std::vector<ReceivedMessage> messages_;
    mutable std::mutex messagesMutex_;
    std::condition_variable messageCondition_;
    int expectedMessages_;
    std::atomic<int> receivedCount_{0};
    
public:
    MessageCollector(int expectedMessages);
    
    void onMessage(const std::string& destination, const std::string& body, 
                  const std::map<std::string, std::string>& headers);
    bool waitForMessages(long timeoutMs);
    std::vector<ReceivedMessage> getMessages() const;
    void clear();
    int getReceivedCount() const { return receivedCount_; }
};

// Connection state tracker
class ConnectionTracker {
private:
    std::atomic<bool> connected_{false};
    std::string lastReason_;
    mutable std::mutex reasonMutex_;
    std::condition_variable connectionCondition_;
    std::condition_variable disconnectionCondition_;
    std::atomic<bool> connectionReceived_{false};
    std::atomic<bool> disconnectionReceived_{false};
    
public:
    void onConnectionChanged(bool connected, const std::string& reason);
    bool waitForConnection(long timeoutMs);
    bool waitForDisconnection(long timeoutMs);
    bool isConnected() const { return connected_; }
    std::string getLastReason() const;
};

// Main Test Bench
class StompTestBench {
private:
    static constexpr const char* TEST_HOST = "localhost";
    static constexpr int TEST_PORT = 61614; // Different from default to avoid conflicts
    static constexpr long DEFAULT_TIMEOUT = 5000; // 5 seconds
    
    TestStats stats_;
    std::unique_ptr<StompServer> server_;
    std::vector<std::unique_ptr<StompClient>> clients_;
    std::thread serverThread_;
    mutable std::mutex clientsMutex_;
    
    // Test execution
    using TestCallable = std::function<std::string()>;
    void executeTest(const std::string& testName, TestCallable test);
    
    // Test setup/teardown
    void startTestServer();
    void cleanup();
    std::unique_ptr<StompClient> createClient();
    
    // Test categories
    void runBasicTests();
    void testClientConnection();
    void testBasicMessaging();
    void testSubscriptionUnsubscription();
    void testMultipleSubscriptions();
    void testMessagePersistence();
    
    void runMultiClientTests();
    void testMultipleClients();
    void testBroadcastMessaging();
    void testClientIsolation();
    void testConcurrentSubscriptions();
    
    void runStressTests();
    void testHighVolumeMessaging();
    void testRapidConnectionCycles();
    void testManySubscriptions();
    
    void runEdgeCaseTests();
    void testEmptyMessages();
    void testLargeMessages();
    void testInvalidDestinations();
    
    void runPerformanceTests();
    void testThroughput();
    void testLatency();
    void testMemoryUsage();
    
    void runReliabilityTests();
    void testConnectionRecovery();
    void testServerDisconnection();
    void testConcurrentOperations();
    
public:
    void runAllTests();
    static void main(const std::vector<std::string>& args = {});
};

// Load Testing Utility
class StompLoadTester {
public:
    static void runLoadTest(const std::string& host, int port, int clientCount, 
                           int messagesPerClient, int durationSeconds);
    static void main(const std::vector<std::string>& args = {});
};
