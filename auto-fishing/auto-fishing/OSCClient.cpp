#include "OSCClient.h"
#include <cstring>
#include <iostream>

OSCClient::OSCClient(const std::string& ip, int port) : sock(INVALID_SOCKET), initialized(false) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return;
    }

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return;
    }

    // Set server address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

    initialized = true;
}

OSCClient::~OSCClient() {
    cleanup();
}

bool OSCClient::initialize() {
    return initialized;
}

size_t OSCClient::padTo4Bytes(size_t size) {
    return (size + 3) & ~3;
}

std::string OSCClient::buildOSCMessage(const std::string& address, int value) {
    std::string message;

    // OSC Address
    message += address;
    // Pad to 4-byte boundary
    size_t addressPadded = padTo4Bytes(address.length() + 1);
    message.append(addressPadded - address.length(), '\0');

    // Type tag string ",i" for an integer argument
    message += ",i";
    // Pad to 4-byte boundary
    message.append(2, '\0');

    // Integer value (big-endian)
    unsigned char valueBytes[4];
    valueBytes[0] = (value >> 24) & 0xFF;
    valueBytes[1] = (value >> 16) & 0xFF;
    valueBytes[2] = (value >> 8) & 0xFF;
    valueBytes[3] = value & 0xFF;
    message.append(reinterpret_cast<char*>(valueBytes), 4);

    return message;
}

bool OSCClient::sendMessage(const std::string& address, int value) {
    if (!initialized) {
        return false;
    }

    std::string message = buildOSCMessage(address, value);
    
    int result = sendto(sock, message.c_str(), static_cast<int>(message.length()), 
                       0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    
    return result != SOCKET_ERROR;
}

bool OSCClient::sendClick(bool press) {
    return sendMessage("/input/UseRight", press ? 1 : 0);
}

void OSCClient::cleanup() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    WSACleanup();
    initialized = false;
}