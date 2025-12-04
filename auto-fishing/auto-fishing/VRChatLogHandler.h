#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>

// VRChat Log Handler Class
class VRChatLogHandler {
private:
    std::function<void(const std::string&)> callback;
    std::string currentLog;
    std::streampos filePosition;
    std::atomic<bool> running;
    std::mutex logMutex;
    std::thread monitorThread;

    // Get VRChat log directory
    std::string getVRChatLogDir();

    // Find the latest log file
    std::string findLatestLog();

    // Update the log file
    bool updateLogFile();

    // Thread function for checking logs
    void checkLogsThread();

public:
    VRChatLogHandler(std::function<void(const std::string&)> cb);
    ~VRChatLogHandler();

    // Start monitoring
    void startMonitor();

    // Stop monitoring
    void stop();

    // Safely read file (public method for external calls)
    std::string safeReadFile();

    // Get the current log file path
    std::string getCurrentLogPath() const;
};