# STOMP C++ Implementation Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
INCLUDES = -I.
LDFLAGS = -pthread

# Source files
SOURCES = StompFrame.cpp StompServer.cpp StompClient.cpp StompTestBench.cpp
HEADERS = StompFrame.hpp StompServer.hpp StompClient.hpp StompTestBench.hpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Executable targets
TARGETS = stomp-server stomp-client stomp-test-bench stomp-load-tester

# Default target
all: $(TARGETS)

# Object file compilation
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Server executable
stomp-server: $(OBJECTS) StompServerMain.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Client executable
stomp-client: $(OBJECTS) StompClientMain.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Test bench executable
stomp-test-bench: $(OBJECTS) StompTestBenchMain.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Load tester executable
stomp-load-tester: $(OBJECTS) StompLoadTesterMain.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Main object files
StompServerMain.o: StompServerMain.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

StompClientMain.o: StompClientMain.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

StompTestBenchMain.o: StompTestBenchMain.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

StompLoadTesterMain.o: StompLoadTesterMain.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean target
clean:
	rm -f $(OBJECTS) StompServerMain.o StompClientMain.o StompTestBenchMain.o StompLoadTesterMain.o $(TARGETS)

# Install target (optional)
install: $(TARGETS)
	mkdir -p /usr/local/bin
	cp $(TARGETS) /usr/local/bin/

# Uninstall target (optional)
uninstall:
	rm -f /usr/local/bin/stomp-server /usr/local/bin/stomp-client /usr/local/bin/stomp-test-bench /usr/local/bin/stomp-load-tester

# Test target
test: stomp-test-bench
	./stomp-test-bench

# Load test target  
load-test: stomp-load-tester
	./stomp-load-tester

# Run server
run-server: stomp-server
	./stomp-server

# Run client
run-client: stomp-client
	./stomp-client

# Help target
help:
	@echo "Available targets:"
	@echo "  all           - Build all executables"
	@echo "  stomp-server  - Build STOMP server"
	@echo "  stomp-client  - Build STOMP client"
	@echo "  stomp-test-bench - Build test bench"
	@echo "  stomp-load-tester - Build load tester"
	@echo "  clean         - Remove all build artifacts"
	@echo "  test          - Build and run test bench"
	@echo "  load-test     - Build and run load tester"
	@echo "  run-server    - Build and run server"
	@echo "  run-client    - Build and run client"
	@echo "  install       - Install executables to /usr/local/bin"
	@echo "  uninstall     - Remove installed executables"
	@echo "  help          - Show this help message"

.PHONY: all clean install uninstall test load-test run-server run-client help
