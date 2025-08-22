#!/bin/bash

# Cross-Language Test Build Script
echo "🔨 Building Cross-Language STOMP Tests"
echo "======================================"

# Build C++ components first
echo "🔧 Building C++ STOMP components..."
cd "$(dirname "$0")/../cpp"
make clean
make StompFrame.o StompClient.o StompServer.o

if [ $? -eq 0 ]; then
    echo "✅ C++ components built successfully"
else
    echo "❌ C++ build failed"
    exit 1
fi

# Build Java components
echo "☕ Building Java STOMP components..."
cd ../java
javac *.java

if [ $? -eq 0 ]; then
    echo "✅ Java components built successfully"
else
    echo "❌ Java build failed"
    exit 1
fi

# Build C++ test
echo "🧪 Building C++ test client..."
cd ../tests
g++ -std=c++17 -pthread -I../cpp JavaServerTest.cpp ../cpp/StompClient.o ../cpp/StompFrame.o -o JavaServerTest

if [ $? -eq 0 ]; then
    echo "✅ C++ test client built successfully"
else
    echo "❌ C++ test build failed"
    exit 1
fi

# Prepare Java test (copy dependencies)
echo "🧪 Preparing Java test client..."
cp ../java/StompClient.java ../java/StompFrame.java .

javac CppServerTest.java StompClient.java StompFrame.java

if [ $? -eq 0 ]; then
    echo "✅ Java test client built successfully"
else
    echo "❌ Java test build failed"
    exit 1
fi

echo ""
echo "🎉 All cross-language tests built successfully!"
echo "📁 Test executables:"
echo "   - C++ → Java test: ./tests/JavaServerTest"
echo "   - Java → C++ test: cd tests && java CppServerTest"
