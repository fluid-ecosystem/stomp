#!/bin/bash

# Cross-Language STOMP Test Runner
echo "🚀 Cross-Language STOMP Test Runner"
echo "===================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_TIMEOUT=${1:-30}

echo "📋 Test Configuration:"
echo "   - Test timeout: ${TEST_TIMEOUT} seconds"
echo "   - Script directory: ${SCRIPT_DIR}"
echo ""

# Function to check if port is available
check_port() {
    local port=$1
    local max_attempts=15
    
    for i in $(seq 1 $max_attempts); do
        if nc -z localhost $port 2>/dev/null; then
            echo "✅ Port $port is ready"
            return 0
        fi
        if [ $i -eq $max_attempts ]; then
            echo "❌ Port $port not responding after $max_attempts attempts"
            return 1
        fi
        echo "🔄 Waiting for port $port (attempt $i/$max_attempts)..."
        sleep 2
    done
}

# Function to run Java server with C++ client
test_java_server_cpp_client() {
    echo ""
    echo "🧪 Test 1: Java Server ↔ C++ Client"
    echo "-----------------------------------"
    
    # Start Java server
    echo "🚀 Starting Java STOMP server..."
    cd "${SCRIPT_DIR}/../java"
    java StompServer &
    JAVA_PID=$!
    echo $JAVA_PID > "${SCRIPT_DIR}/java_server.pid"
    
    sleep 3
    
    # Check if server started
    if ! kill -0 $JAVA_PID 2>/dev/null; then
        echo "❌ Java server failed to start"
        return 1
    fi
    
    # Wait for port
    if ! check_port 61613; then
        kill $JAVA_PID 2>/dev/null
        return 1
    fi
    
    # Run C++ client test
    echo "🧪 Running C++ client test..."
    cd "${SCRIPT_DIR}"
    timeout $TEST_TIMEOUT ./JavaServerTest
    local exit_code=$?
    
    # Stop Java server
    echo "🛑 Stopping Java server..."
    kill $JAVA_PID 2>/dev/null || true
    sleep 2
    kill -9 $JAVA_PID 2>/dev/null || true
    rm -f "${SCRIPT_DIR}/java_server.pid"
    
    if [ $exit_code -eq 0 ]; then
        echo "✅ Java Server ↔ C++ Client test PASSED"
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo "⏰ Java Server ↔ C++ Client test TIMED OUT"
        return 1
    else
        echo "❌ Java Server ↔ C++ Client test FAILED (exit code: $exit_code)"
        return 1
    fi
}

# Function to run C++ server with Java client
test_cpp_server_java_client() {
    echo ""
    echo "🧪 Test 2: C++ Server ↔ Java Client"
    echo "-----------------------------------"
    
    # Start C++ server
    echo "🚀 Starting C++ STOMP server..."
    cd "${SCRIPT_DIR}/../cpp"
    ./stomp-server &
    CPP_PID=$!
    echo $CPP_PID > "${SCRIPT_DIR}/cpp_server.pid"
    
    sleep 3
    
    # Check if server started
    if ! kill -0 $CPP_PID 2>/dev/null; then
        echo "❌ C++ server failed to start"
        return 1
    fi
    
    # Wait for port
    if ! check_port 61613; then
        kill $CPP_PID 2>/dev/null
        return 1
    fi
    
    # Run Java client test
    echo "🧪 Running Java client test..."
    cd "${SCRIPT_DIR}"
    timeout $TEST_TIMEOUT java CppServerTest
    local exit_code=$?
    
    # Stop C++ server
    echo "🛑 Stopping C++ server..."
    kill $CPP_PID 2>/dev/null || true
    sleep 2
    kill -9 $CPP_PID 2>/dev/null || true
    rm -f "${SCRIPT_DIR}/cpp_server.pid"
    
    if [ $exit_code -eq 0 ]; then
        echo "✅ C++ Server ↔ Java Client test PASSED"
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo "⏰ C++ Server ↔ Java Client test TIMED OUT"
        return 1
    else
        echo "❌ C++ Server ↔ Java Client test FAILED (exit code: $exit_code)"
        return 1
    fi
}

# Main test execution
main() {
    echo "🔨 Building test components..."
    "${SCRIPT_DIR}/build-tests.sh"
    
    if [ $? -ne 0 ]; then
        echo "❌ Build failed, aborting tests"
        exit 1
    fi
    
    local test1_result=0
    local test2_result=0
    
    # Run tests
    test_java_server_cpp_client
    test1_result=$?
    
    test_cpp_server_java_client
    test2_result=$?
    
    # Run mapping verification
    echo ""
    echo "🔍 Running mapping verification..."
    "${SCRIPT_DIR}/mapping-verification.sh"
    
    # Generate final report
    echo ""
    echo "🏆 Final Test Results"
    echo "===================="
    echo "Date: $(date)"
    echo "Test timeout: ${TEST_TIMEOUT}s"
    echo ""
    
    if [ $test1_result -eq 0 ]; then
        echo "✅ Java Server ↔ C++ Client: PASSED"
    else
        echo "❌ Java Server ↔ C++ Client: FAILED"
    fi
    
    if [ $test2_result -eq 0 ]; then
        echo "✅ C++ Server ↔ Java Client: PASSED"
    else
        echo "❌ C++ Server ↔ Java Client: FAILED"
    fi
    
    if [ $test1_result -eq 0 ] && [ $test2_result -eq 0 ]; then
        echo ""
        echo "🎉 All cross-language connectivity tests PASSED!"
        echo "🎯 Multi-language STOMP implementation is fully interoperable!"
        exit 0
    else
        echo ""
        echo "❌ Some tests failed. Check the logs above for details."
        exit 1
    fi
}

# Cleanup function
cleanup() {
    echo ""
    echo "🧹 Cleaning up..."
    
    # Kill any remaining servers
    if [ -f "${SCRIPT_DIR}/java_server.pid" ]; then
        JAVA_PID=$(cat "${SCRIPT_DIR}/java_server.pid")
        kill $JAVA_PID 2>/dev/null || true
        rm -f "${SCRIPT_DIR}/java_server.pid"
    fi
    
    if [ -f "${SCRIPT_DIR}/cpp_server.pid" ]; then
        CPP_PID=$(cat "${SCRIPT_DIR}/cpp_server.pid")
        kill $CPP_PID 2>/dev/null || true
        rm -f "${SCRIPT_DIR}/cpp_server.pid"
    fi
    
    # Kill any remaining java or stomp-server processes
    pkill -f "java StompServer" 2>/dev/null || true
    pkill -f "stomp-server" 2>/dev/null || true
}

# Set up cleanup trap
trap cleanup EXIT

# Run main function
main "$@"
