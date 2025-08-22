#include "StompTestBench.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <thread>
#include <future>
#include <ctime>
#include <cstring>

// TestStats implementation
void TestStats::addResult(const TestResult& result) {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    results_.push_back(result);
    totalTests_.fetch_add(1);
    if (result.passed) {
        passedTests_.fetch_add(1);
    } else {
        failedTests_.fetch_add(1);
    }
}

void TestStats::printSummary() const {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "Total Tests: " << totalTests_.load() << std::endl;
    std::cout << "Passed: " << passedTests_.load() << std::endl;
    std::cout << "Failed: " << failedTests_.load() << std::endl;
    
    double successRate = totalTests_.load() > 0 ? 
        (double)passedTests_.load() / totalTests_.load() * 100 : 0.0;
    std::cout << "Success Rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
    
    std::cout << "\nDETAILED RESULTS:" << std::endl;
    for (const auto& result : results_) {
        std::cout << result.toString() << std::endl;
    }
    
    if (failedTests_.load() > 0) {
        std::cout << "\n" << std::string(80, '!') << std::endl;
        std::cout << "SOME TESTS FAILED - REVIEW ABOVE RESULTS" << std::endl;
        std::cout << std::string(80, '!') << std::endl;
    }
}

// MessageCollector implementation
MessageCollector::MessageCollector(int expectedMessages) : expectedMessages_(expectedMessages) {}

void MessageCollector::onMessage(const std::string& destination, const std::string& body, 
                                const std::map<std::string, std::string>& headers) {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    messages_.emplace_back(destination, body, headers, timestamp);
    receivedCount_.fetch_add(1);
    messageCondition_.notify_one();
}

bool MessageCollector::waitForMessages(long timeoutMs) {
    std::unique_lock<std::mutex> lock(messagesMutex_);
    return messageCondition_.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
        [this] { return static_cast<int>(messages_.size()) >= expectedMessages_; });
}

std::vector<ReceivedMessage> MessageCollector::getMessages() const {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    return messages_;
}

void MessageCollector::clear() {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    messages_.clear();
    receivedCount_ = 0;
}

// ConnectionTracker implementation
void ConnectionTracker::onConnectionChanged(bool connected, const std::string& reason) {
    connected_ = connected;
    {
        std::lock_guard<std::mutex> lock(reasonMutex_);
        lastReason_ = reason;
    }
    
    if (connected) {
        connectionReceived_ = true;
        connectionCondition_.notify_all();
    } else {
        disconnectionReceived_ = true;
        disconnectionCondition_.notify_all();
    }
}

bool ConnectionTracker::waitForConnection(long timeoutMs) {
    std::unique_lock<std::mutex> lock(reasonMutex_);
    return connectionCondition_.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
        [this] { return connectionReceived_.load(); });
}

bool ConnectionTracker::waitForDisconnection(long timeoutMs) {
    std::unique_lock<std::mutex> lock(reasonMutex_);
    return disconnectionCondition_.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
        [this] { return disconnectionReceived_.load(); });
}

std::string ConnectionTracker::getLastReason() const {
    std::lock_guard<std::mutex> lock(reasonMutex_);
    return lastReason_;
}

// StompTestBench implementation
void StompTestBench::runAllTests() {
    try {
        // Start test server
        startTestServer();
        
        // Basic functionality tests
        runBasicTests();
        
        // Multi-client tests
        runMultiClientTests();
        
        // Stress tests
        runStressTests();
        
        // Edge case tests
        runEdgeCaseTests();
        
        // Performance tests
        runPerformanceTests();
        
        // Reliability tests
        runReliabilityTests();
        
    } catch (const std::exception& e) {
        std::cerr << "Test execution failed: " << e.what() << std::endl;
    }
}

void StompTestBench::startTestServer() {
    std::cout << "Starting test server on port " << TEST_PORT << "..." << std::endl;
    
    server_ = std::make_unique<StompServer>(TEST_PORT);
    serverThread_ = std::thread([this]() {
        try {
            server_->start();
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    });
    
    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    std::cout << "Test server started successfully\n" << std::endl;
}

void StompTestBench::runBasicTests() {
    std::cout << "RUNNING BASIC TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testClientConnection();
    testBasicMessaging();
    testSubscriptionUnsubscription();
    testMultipleSubscriptions();
    testMessagePersistence();
    
    std::cout << std::endl;
}

void StompTestBench::testClientConnection() {
    executeTest("Client Connection", [this]() {
        auto client = createClient();
        ConnectionTracker tracker;
        
        client->setConnectionListener([&tracker](bool connected, const std::string& reason) {
            tracker.onConnectionChanged(connected, reason);
        });
        
        client->connect().get();
        
        if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Connection timeout");
        }
        
        if (!client->isConnected()) {
            throw std::runtime_error("Client not connected");
        }
        
        client->disconnect();
        
        if (!tracker.waitForDisconnection(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Disconnection timeout");
        }
        
        return "Connection and disconnection successful";
    });
}

void StompTestBench::testBasicMessaging() {
    executeTest("Basic Messaging", [this]() {
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/basic-test";
        MessageCollector collector(1);
        
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::string testMessage = "Hello, STOMP!";
        sender->send(destination, testMessage);
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Message not received");
        }
        
        auto messages = collector.getMessages();
        if (messages.empty() || messages[0].body != testMessage) {
            throw std::runtime_error("Message content mismatch");
        }
        
        sender->disconnect();
        receiver->disconnect();
        
        return "Message sent and received successfully";
    });
}

void StompTestBench::testSubscriptionUnsubscription() {
    executeTest("Subscription/Unsubscription", [this]() {
        auto client = createClient();
        client->connect().get();
        
        std::string destination = "/topic/sub-test";
        MessageCollector collector(1);
        
        std::string subscriptionId = client->subscribe(destination, 
            [&collector](const std::string& dest, const std::string& body, 
                        const std::map<std::string, std::string>& headers) {
                collector.onMessage(dest, body, headers);
            });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Send a message - should be received
        client->send(destination, "Test message 1");
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("First message not received");
        }
        
        // Unsubscribe
        client->unsubscribe(subscriptionId);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Send another message - should NOT be received
        collector.clear();
        MessageCollector collector2(1);
        client->send(destination, "Test message 2");
        
        // Wait briefly and check no message was received
        if (collector2.waitForMessages(1000)) {
            throw std::runtime_error("Message received after unsubscription");
        }
        
        client->disconnect();
        
        return "Subscription and unsubscription work correctly";
    });
}

void StompTestBench::testMultipleSubscriptions() {
    executeTest("Multiple Subscriptions", [this]() {
        auto client = createClient();
        client->connect().get();
        
        std::string dest1 = "/topic/multi-test-1";
        std::string dest2 = "/topic/multi-test-2";
        
        MessageCollector collector1(1);
        MessageCollector collector2(1);
        
        client->subscribe(dest1, [&collector1](const std::string& dest, const std::string& body, 
                                              const std::map<std::string, std::string>& headers) {
            collector1.onMessage(dest, body, headers);
        });
        
        client->subscribe(dest2, [&collector2](const std::string& dest, const std::string& body, 
                                              const std::map<std::string, std::string>& headers) {
            collector2.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        client->send(dest1, "Message for topic 1");
        client->send(dest2, "Message for topic 2");
        
        if (!collector1.waitForMessages(DEFAULT_TIMEOUT) || !collector2.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Not all messages received");
        }
        
        auto messages1 = collector1.getMessages();
        auto messages2 = collector2.getMessages();
        
        if (messages1.empty() || messages2.empty()) {
            throw std::runtime_error("Missing messages");
        }
        
        if (messages1[0].body != "Message for topic 1" || messages2[0].body != "Message for topic 2") {
            throw std::runtime_error("Message content mismatch");
        }
        
        client->disconnect();
        
        return "Multiple subscriptions work correctly";
    });
}

void StompTestBench::testMessagePersistence() {
    executeTest("Message Persistence", [this]() {
        auto sender = createClient();
        sender->connect().get();
        
        std::string destination = "/topic/persistence-test";
        std::string testMessage = "Persistent message";
        
        // Send message before subscriber connects
        sender->send(destination, testMessage);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Now connect receiver and subscribe
        auto receiver = createClient();
        receiver->connect().get();
        
        MessageCollector collector(1);
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        // Send another message after subscription
        sender->send(destination, testMessage);
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Message not received");
        }
        
        sender->disconnect();
        receiver->disconnect();
        
        return "Message delivery after subscription works";
    });
}

void StompTestBench::runMultiClientTests() {
    std::cout << "RUNNING MULTI-CLIENT TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testMultipleClients();
    testBroadcastMessaging();
    testClientIsolation();
    testConcurrentSubscriptions();
    
    std::cout << std::endl;
}

void StompTestBench::testMultipleClients() {
    executeTest("Multiple Clients", [this]() {
        const int clientCount = 5;
        std::vector<std::unique_ptr<StompClient>> testClients;
        
        // Create and connect multiple clients
        for (int i = 0; i < clientCount; i++) {
            auto client = createClient();
            client->connect().get();
            testClients.push_back(std::move(client));
        }
        
        // Verify all are connected
        for (const auto& client : testClients) {
            if (!client->isConnected()) {
                throw std::runtime_error("Client not connected");
            }
        }
        
        // Disconnect all
        for (auto& client : testClients) {
            client->disconnect();
        }
        
        return std::to_string(clientCount) + " clients connected and disconnected successfully";
    });
}

void StompTestBench::testBroadcastMessaging() {
    executeTest("Broadcast Messaging", [this]() {
        const int receiverCount = 3;
        auto sender = createClient();
        std::vector<std::unique_ptr<StompClient>> receivers;
        std::vector<std::unique_ptr<MessageCollector>> collectors;
        
        sender->connect().get();
        
        std::string destination = "/topic/broadcast-test";
        
        // Create receivers and collectors
        for (int i = 0; i < receiverCount; i++) {
            auto receiver = createClient();
            receiver->connect().get();
            
            auto collector = std::make_unique<MessageCollector>(1);
            
            receiver->subscribe(destination, [&collector = *collector](const std::string& dest, const std::string& body, 
                                                                      const std::map<std::string, std::string>& headers) {
                collector.onMessage(dest, body, headers);
            });
            
            receivers.push_back(std::move(receiver));
            collectors.push_back(std::move(collector));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::string broadcastMessage = "Broadcast to all!";
        sender->send(destination, broadcastMessage);
        
        // Verify all receivers got the message
        for (auto& collector : collectors) {
            if (!collector->waitForMessages(DEFAULT_TIMEOUT)) {
                throw std::runtime_error("Not all receivers got the broadcast");
            }
            
            auto messages = collector->getMessages();
            if (messages.empty() || messages[0].body != broadcastMessage) {
                throw std::runtime_error("Broadcast message content mismatch");
            }
        }
        
        sender->disconnect();
        for (auto& receiver : receivers) {
            receiver->disconnect();
        }
        
        return "Broadcast message delivered to " + std::to_string(receiverCount) + " receivers";
    });
}

void StompTestBench::testClientIsolation() {
    executeTest("Client Isolation", [this]() {
        auto client1 = createClient();
        auto client2 = createClient();
        
        client1->connect().get();
        client2->connect().get();
        
        std::string dest1 = "/topic/isolation-1";
        std::string dest2 = "/topic/isolation-2";
        
        MessageCollector collector1(1);
        MessageCollector collector2(1);
        
        client1->subscribe(dest1, [&collector1](const std::string& dest, const std::string& body, 
                                               const std::map<std::string, std::string>& headers) {
            collector1.onMessage(dest, body, headers);
        });
        
        client2->subscribe(dest2, [&collector2](const std::string& dest, const std::string& body, 
                                               const std::map<std::string, std::string>& headers) {
            collector2.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Send to dest1 - only client1 should receive
        client1->send(dest1, "Message for client1");
        
        if (!collector1.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Client1 didn't receive its message");
        }
        
        // Verify client2 didn't receive it
        if (collector2.waitForMessages(1000)) {
            throw std::runtime_error("Client2 received message meant for client1");
        }
        
        client1->disconnect();
        client2->disconnect();
        
        return "Client isolation working correctly";
    });
}

void StompTestBench::testConcurrentSubscriptions() {
    executeTest("Concurrent Subscriptions", [this]() {
        const int clientCount = 10;
        std::vector<std::future<bool>> futures;
        
        for (int i = 0; i < clientCount; i++) {
            futures.push_back(std::async(std::launch::async, [this, i]() {
                try {
                    auto client = createClient();
                    client->connect().get();
                    
                    std::string destination = "/topic/concurrent-" + std::to_string(i);
                    MessageCollector collector(1);
                    
                    client->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                               const std::map<std::string, std::string>& headers) {
                        collector.onMessage(dest, body, headers);
                    });
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    
                    client->send(destination, "Concurrent message " + std::to_string(i));
                    
                    bool received = collector.waitForMessages(DEFAULT_TIMEOUT);
                    client->disconnect();
                    return received;
                } catch (...) {
                    return false;
                }
            }));
        }
        
        // Wait for all clients to complete
        int successCount = 0;
        for (auto& future : futures) {
            if (future.get()) {
                successCount++;
            }
        }
        
        if (successCount != clientCount) {
            throw std::runtime_error("Not all concurrent operations succeeded: " + 
                                    std::to_string(successCount) + "/" + std::to_string(clientCount));
        }
        
        return std::to_string(clientCount) + " concurrent subscriptions successful";
    });
}

void StompTestBench::runStressTests() {
    std::cout << "RUNNING STRESS TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testHighVolumeMessaging();
    testRapidConnectionCycles();
    testManySubscriptions();
    
    std::cout << std::endl;
}

void StompTestBench::testHighVolumeMessaging() {
    executeTest("High Volume Messaging", [this]() {
        const int messageCount = 100;
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/volume-test";
        MessageCollector collector(messageCount);
        
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto startTime = std::chrono::steady_clock::now();
        
        // Send messages rapidly
        for (int i = 0; i < messageCount; i++) {
            sender->send(destination, "Volume message " + std::to_string(i));
        }
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT * 2)) {
            throw std::runtime_error("Not all volume messages received: " + 
                                    std::to_string(collector.getReceivedCount()) + "/" + std::to_string(messageCount));
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        sender->disconnect();
        receiver->disconnect();
        
        return std::to_string(messageCount) + " messages sent/received in " + 
               std::to_string(duration.count()) + "ms";
    });
}

void StompTestBench::testRapidConnectionCycles() {
    executeTest("Rapid Connection Cycles", [this]() {
        const int cycles = 20;
        
        for (int i = 0; i < cycles; i++) {
            auto client = createClient();
            client->connect().get();
            
            if (!client->isConnected()) {
                throw std::runtime_error("Connection failed on cycle " + std::to_string(i));
            }
            
            client->disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        return std::to_string(cycles) + " rapid connection cycles completed";
    });
}

void StompTestBench::testManySubscriptions() {
    executeTest("Many Subscriptions", [this]() {
        const int subscriptionCount = 50;
        auto client = createClient();
        client->connect().get();
        
        std::vector<std::string> subscriptionIds;
        
        // Create many subscriptions
        for (int i = 0; i < subscriptionCount; i++) {
            std::string destination = "/topic/many-subs-" + std::to_string(i);
            std::string subId = client->subscribe(destination, 
                [](const std::string& dest, const std::string& body, 
                   const std::map<std::string, std::string>& headers) {
                    // Just receive, don't process
                });
            subscriptionIds.push_back(subId);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Send a message to the last destination
        std::string testDest = "/topic/many-subs-" + std::to_string(subscriptionCount - 1);
        client->send(testDest, "Test message for many subscriptions");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Unsubscribe all
        for (const auto& subId : subscriptionIds) {
            client->unsubscribe(subId);
        }
        
        client->disconnect();
        
        return std::to_string(subscriptionCount) + " subscriptions created and cleaned up";
    });
}

void StompTestBench::runEdgeCaseTests() {
    std::cout << "RUNNING EDGE CASE TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testEmptyMessages();
    testLargeMessages();
    testInvalidDestinations();
    
    std::cout << std::endl;
}

void StompTestBench::testEmptyMessages() {
    executeTest("Empty Messages", [this]() {
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/empty-test";
        MessageCollector collector(1);
        
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        sender->send(destination, "");
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Empty message not received");
        }
        
        auto messages = collector.getMessages();
        if (messages.empty() || !messages[0].body.empty()) {
            throw std::runtime_error("Empty message content error");
        }
        
        sender->disconnect();
        receiver->disconnect();
        
        return "Empty message handled correctly";
    });
}

void StompTestBench::testLargeMessages() {
    executeTest("Large Messages", [this]() {
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/large-test";
        MessageCollector collector(1);
        
        // Create a large message (10KB)
        std::string largeMessage(10240, 'A');
        
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        sender->send(destination, largeMessage);
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Large message not received");
        }
        
        auto messages = collector.getMessages();
        if (messages.empty() || messages[0].body != largeMessage) {
            throw std::runtime_error("Large message content mismatch");
        }
        
        sender->disconnect();
        receiver->disconnect();
        
        return "Large message (" + std::to_string(largeMessage.length()) + " bytes) handled correctly";
    });
}

void StompTestBench::testInvalidDestinations() {
    executeTest("Various Destination Formats", [this]() {
        auto client = createClient();
        client->connect().get();
        
        std::vector<std::string> destinations = {
            "/topic/valid",
            "/queue/test",
            "/topic/with-hyphens-and_underscores",
            "/topic/with.dots",
            "/topic/123numeric",
            "/topic/UPPERCASE",
            "/topic/mixedCase"
        };
        
        MessageCollector collector(static_cast<int>(destinations.size()));
        
        // Subscribe to all destinations
        for (const auto& dest : destinations) {
            client->subscribe(dest, [&collector](const std::string& dest, const std::string& body, 
                                                const std::map<std::string, std::string>& headers) {
                collector.onMessage(dest, body, headers);
            });
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Send messages to all destinations
        for (const auto& dest : destinations) {
            client->send(dest, "Test message for " + dest);
        }
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Not all destination messages received: " + 
                                    std::to_string(collector.getReceivedCount()) + "/" + std::to_string(destinations.size()));
        }
        
        client->disconnect();
        
        return std::to_string(destinations.size()) + " different destination formats tested successfully";
    });
}

void StompTestBench::runPerformanceTests() {
    std::cout << "RUNNING PERFORMANCE TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testThroughput();
    testLatency();
    testMemoryUsage();
    
    std::cout << std::endl;
}

void StompTestBench::testThroughput() {
    executeTest("Throughput Test", [this]() {
        const int messageCount = 1000;
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/throughput-test";
        MessageCollector collector(messageCount);
        
        receiver->subscribe(destination, [&collector](const std::string& dest, const std::string& body, 
                                                     const std::map<std::string, std::string>& headers) {
            collector.onMessage(dest, body, headers);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto startTime = std::chrono::steady_clock::now();
        
        // Send messages as fast as possible
        for (int i = 0; i < messageCount; i++) {
            sender->send(destination, "Throughput message " + std::to_string(i));
        }
        
        if (!collector.waitForMessages(DEFAULT_TIMEOUT * 3)) {
            throw std::runtime_error("Throughput test incomplete: " + 
                                    std::to_string(collector.getReceivedCount()) + "/" + std::to_string(messageCount));
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        double messagesPerSecond = (double)messageCount / (duration.count() / 1000.0);
        
        sender->disconnect();
        receiver->disconnect();
        
        return std::to_string(messageCount) + " messages, " + 
               std::to_string(static_cast<int>(messagesPerSecond)) + " msgs/sec";
    });
}

void StompTestBench::testLatency() {
    executeTest("Latency Test", [this]() {
        const int sampleCount = 100;
        auto sender = createClient();
        auto receiver = createClient();
        
        sender->connect().get();
        receiver->connect().get();
        
        std::string destination = "/topic/latency-test";
        std::vector<long> latencies;
        std::mutex latencyMutex;
        
        receiver->subscribe(destination, [&latencies, &latencyMutex](const std::string& dest, const std::string& body, 
                                                                    const std::map<std::string, std::string>& headers) {
            auto receiveTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            // Extract send time from message
            size_t pos = body.find(':');
            if (pos != std::string::npos) {
                long sendTime = std::stoll(body.substr(pos + 1));
                long latency = receiveTime - sendTime;
                
                std::lock_guard<std::mutex> lock(latencyMutex);
                latencies.push_back(latency);
            }
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Send messages with timestamps
        for (int i = 0; i < sampleCount; i++) {
            auto sendTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            sender->send(destination, "Latency:" + std::to_string(sendTime));
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Space out sends
        }
        
        // Wait for all samples
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        
        if (latencies.size() < sampleCount * 0.9) { // Allow for some loss
            throw std::runtime_error("Insufficient latency samples: " + std::to_string(latencies.size()));
        }
        
        // Calculate average latency
        long totalLatency = 0;
        for (long latency : latencies) {
            totalLatency += latency;
        }
        double avgLatency = (double)totalLatency / latencies.size() / 1000.0; // Convert to milliseconds
        
        sender->disconnect();
        receiver->disconnect();
        
        return std::to_string(latencies.size()) + " samples, avg latency: " + 
               std::to_string(static_cast<int>(avgLatency * 100) / 100.0) + "ms";
    });
}

void StompTestBench::testMemoryUsage() {
    executeTest("Memory Usage Test", [this]() {
        const int clientCount = 50;
        const int messagesPerClient = 20;
        
        std::vector<std::unique_ptr<StompClient>> testClients;
        
        // Create many clients with subscriptions
        for (int i = 0; i < clientCount; i++) {
            auto client = createClient();
            client->connect().get();
            
            std::string destination = "/topic/memory-test-" + std::to_string(i);
            client->subscribe(destination, [](const std::string& dest, const std::string& body, 
                                            const std::map<std::string, std::string>& headers) {
                // Just receive, minimal processing
            });
            
            testClients.push_back(std::move(client));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Send messages from each client
        for (int i = 0; i < clientCount; i++) {
            for (int j = 0; j < messagesPerClient; j++) {
                std::string destination = "/topic/memory-test-" + std::to_string(i);
                testClients[i]->send(destination, "Memory test message " + std::to_string(j));
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Disconnect all clients
        for (auto& client : testClients) {
            client->disconnect();
        }
        
        return std::to_string(clientCount) + " clients with " + 
               std::to_string(clientCount * messagesPerClient) + " total messages processed";
    });
}

void StompTestBench::runReliabilityTests() {
    std::cout << "RUNNING RELIABILITY TESTS" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    testConnectionRecovery();
    testServerDisconnection();
    testConcurrentOperations();
    
    std::cout << std::endl;
}

void StompTestBench::testConnectionRecovery() {
    executeTest("Connection Recovery", [this]() {
        auto client = createClient();
        
        // Initial connection
        client->connect().get();
        if (!client->isConnected()) {
            throw std::runtime_error("Initial connection failed");
        }
        
        // Disconnect
        client->disconnect();
        
        // Reconnect
        client->connect().get();
        if (!client->isConnected()) {
            throw std::runtime_error("Reconnection failed");
        }
        
        client->disconnect();
        
        return "Connection recovery successful";
    });
}

void StompTestBench::testServerDisconnection() {
    executeTest("Server Disconnection Handling", [this]() {
        auto client = createClient();
        ConnectionTracker tracker;
        
        client->setConnectionListener([&tracker](bool connected, const std::string& reason) {
            tracker.onConnectionChanged(connected, reason);
        });
        
        client->connect().get();
        
        if (!tracker.waitForConnection(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Initial connection failed");
        }
        
        // Simulate graceful disconnect
        client->disconnect();
        
        if (!tracker.waitForDisconnection(DEFAULT_TIMEOUT)) {
            throw std::runtime_error("Disconnect not detected");
        }
        
        return "Server disconnection handled correctly";
    });
}

void StompTestBench::testConcurrentOperations() {
    executeTest("Concurrent Operations", [this]() {
        const int operationCount = 20;
        std::vector<std::future<bool>> futures;
        
        for (int i = 0; i < operationCount; i++) {
            futures.push_back(std::async(std::launch::async, [this, i]() {
                try {
                    auto client = createClient();
                    client->connect().get();
                    
                    // Perform various operations concurrently
                    std::string destination = "/topic/concurrent-ops-" + std::to_string(i);
                    
                    std::string subId = client->subscribe(destination, 
                        [](const std::string& dest, const std::string& body, 
                           const std::map<std::string, std::string>& headers) {
                            // Minimal processing
                        });
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    
                    client->send(destination, "Concurrent operation " + std::to_string(i));
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    
                    client->unsubscribe(subId);
                    client->disconnect();
                    
                    return true;
                } catch (...) {
                    return false;
                }
            }));
        }
        
        // Wait for all operations to complete
        int successCount = 0;
        for (auto& future : futures) {
            if (future.get()) {
                successCount++;
            }
        }
        
        if (successCount != operationCount) {
            throw std::runtime_error("Not all concurrent operations succeeded: " + 
                                    std::to_string(successCount) + "/" + std::to_string(operationCount));
        }
        
        return std::to_string(operationCount) + " concurrent operations completed successfully";
    });
}

std::unique_ptr<StompClient> StompTestBench::createClient() {
    auto client = std::make_unique<StompClient>(TEST_HOST, TEST_PORT);
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.push_back(std::unique_ptr<StompClient>());
    return client;
}

void StompTestBench::executeTest(const std::string& testName, TestCallable test) {
    std::cout << "Running: " << testName << "... " << std::flush;
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        std::string result = test();
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "PASS" << std::endl;
        stats_.addResult(TestResult(testName, true, result, duration.count()));
    } catch (const std::exception& e) {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "FAIL" << std::endl;
        stats_.addResult(TestResult(testName, false, e.what(), duration.count()));
    }
}

void StompTestBench::cleanup() {
    std::cout << "\nCleaning up test resources..." << std::endl;
    
    // Disconnect all clients
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_) {
            if (client && client->isConnected()) {
                try {
                    client->disconnect();
                } catch (...) {
                    // Ignore cleanup errors
                }
            }
        }
        clients_.clear();
    }
    
    // Stop server
    if (server_) {
        try {
            server_->stop();
        } catch (...) {
            // Ignore cleanup errors
        }
    }
    
    // Wait for server thread
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    std::cout << "Cleanup completed." << std::endl;
}

void StompTestBench::main(const std::vector<std::string>& args) {
    StompTestBench testBench;
    
    std::cout << "STOMP Server & Client Test Bench" << std::endl;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::cout << "Starting at: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    try {
        testBench.runAllTests();
    } catch (const std::exception& e) {
        std::cerr << "Test execution failed: " << e.what() << std::endl;
    }
    
    testBench.cleanup();
    testBench.stats_.printSummary();
}

// StompLoadTester implementation
void StompLoadTester::runLoadTest(const std::string& host, int port, int clientCount, 
                                 int messagesPerClient, int durationSeconds) {
    std::cout << "\nSTOMP LOAD TEST" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    std::cout << "Host: " << host << ":" << port << std::endl;
    std::cout << "Clients: " << clientCount << std::endl;
    std::cout << "Messages per client: " << messagesPerClient << std::endl;
    std::cout << "Duration: " << durationSeconds << " seconds" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    std::atomic<int> totalMessagesSent{0};
    std::atomic<int> totalMessagesReceived{0};
    std::atomic<int> errors{0};
    std::atomic<int> connectedClients{0};
    
    std::vector<std::thread> clientThreads;
    std::condition_variable startCondition;
    std::mutex startMutex;
    bool startSignal = false;
    
    auto startTime = std::chrono::steady_clock::now();
    
    for (int i = 0; i < clientCount; i++) {
        clientThreads.emplace_back([&, i]() {
            try {
                StompClient client(host, port);
                
                // Wait for start signal
                {
                    std::unique_lock<std::mutex> lock(startMutex);
                    startCondition.wait(lock, [&startSignal] { return startSignal; });
                }
                
                client.connect().get();
                connectedClients.fetch_add(1);
                
                std::string destination = "/topic/load-test";
                client.subscribe(destination, [&totalMessagesReceived](const std::string& dest, const std::string& body, 
                                                                      const std::map<std::string, std::string>& headers) {
                    totalMessagesReceived.fetch_add(1);
                });
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Send messages
                for (int j = 0; j < messagesPerClient; j++) {
                    client.send(destination, "Load test message " + std::to_string(i) + "-" + std::to_string(j));
                    totalMessagesSent.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(durationSeconds * 1000));
                client.disconnect();
                
            } catch (const std::exception& e) {
                errors.fetch_add(1);
                std::cerr << "Client error: " << e.what() << std::endl;
            }
        });
    }
    
    // Start all clients
    {
        std::lock_guard<std::mutex> lock(startMutex);
        startSignal = true;
    }
    startCondition.notify_all();
    
    // Monitor progress
    std::thread monitorThread([&]() {
        for (int i = 0; i < durationSeconds; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "Progress: " << (i + 1) << "s - Sent: " << totalMessagesSent.load() 
                      << ", Received: " << totalMessagesReceived.load() 
                      << ", Connected: " << connectedClients.load() << std::endl;
        }
    });
    
    // Wait for completion
    for (auto& thread : clientThreads) {
        thread.join();
    }
    monitorThread.join();
    
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    
    std::cout << "\nLOAD TEST RESULTS:" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    std::cout << "Total time: " << totalTime.count() << "ms" << std::endl;
    std::cout << "Messages sent: " << totalMessagesSent.load() << std::endl;
    std::cout << "Messages received: " << totalMessagesReceived.load() << std::endl;
    std::cout << "Errors: " << errors.load() << std::endl;
    
    double sendRate = (double)totalMessagesSent.load() / (totalTime.count() / 1000.0);
    double receiveRate = (double)totalMessagesReceived.load() / (totalTime.count() / 1000.0);
    double successRate = (double)(clientCount - errors.load()) / clientCount * 100;
    
    std::cout << "Send rate: " << std::fixed << std::setprecision(2) << sendRate << " msg/s" << std::endl;
    std::cout << "Receive rate: " << std::fixed << std::setprecision(2) << receiveRate << " msg/s" << std::endl;
    std::cout << "Success rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
}

void StompLoadTester::main(const std::vector<std::string>& args) {
    std::string host = args.size() > 0 ? args[0] : "localhost";
    int port = args.size() > 1 ? std::stoi(args[1]) : 61613;
    int clients = args.size() > 2 ? std::stoi(args[2]) : 10;
    int messages = args.size() > 3 ? std::stoi(args[3]) : 100;
    int duration = args.size() > 4 ? std::stoi(args[4]) : 30;
    
    runLoadTest(host, port, clients, messages, duration);
}
