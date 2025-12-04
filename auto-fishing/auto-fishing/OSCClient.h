#pragma once
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// OSC Client Class - for sending OSC messages to VRChat
class OSCClient {
private:
    SOCKET sock;
    sockaddr_in serverAddr;
    bool initialized;

    // Build OSC message
    std::string buildOSCMessage(const std::string& address, int value);
    
    // Pad to 4-byte boundary
    size_t padTo4Bytes(size_t size);

public:
    OSCClient(const std::string& ip = "127.0.0.1", int port = 9000);
    ~OSCClient();

    // Initialize socket
    bool initialize();

    // Send message
    bool sendMessage(const std::string& address, int value);

    // Send click message
    bool sendClick(bool press);

    // Cleanup resources
    void cleanup();
};