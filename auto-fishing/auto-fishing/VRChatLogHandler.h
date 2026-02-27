#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

enum class LogEventType {
    FishOnHook,
    FishPickup,
    BucketSave
};

class VRChatLogHandler {
public:
    static constexpr const char* FISH_HOOK_KEYWORD = "SAVED DATA";
    static constexpr const char* FISH_PICKUP_KEYWORD = "Fish Pickup attached to rod Toggles(True)";
    static constexpr const char* BUCKET_SAVE_KEYWORD = "Attempt saving";
    static constexpr const char* LOG_FILE_PREFIX = "output_log_";
    static constexpr const char* LOG_FILE_EXTENSION = ".txt";

    using LogCallback = std::function<void(LogEventType, const std::string&)>;

    explicit VRChatLogHandler(LogCallback callback);
    ~VRChatLogHandler();

    VRChatLogHandler(const VRChatLogHandler&) = delete;
    VRChatLogHandler& operator=(const VRChatLogHandler&) = delete;

    void startMonitor();
    void stop();
    std::string safeReadFile();
    std::string readTail(size_t maxBytes = 131072);
    std::string getCurrentLogPath() const;
    bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    std::wstring getVRChatLogDir() const;
    std::wstring findLatestLog() const;
    bool updateLogFile();
    void directoryWatchThread();
    void fileReadThread();
    std::string readNewContent();
    void processLogContent(const std::string& content);
    void processLine(const std::string& line);

    LogCallback callback_;
    std::wstring logDirectory_;
    std::wstring currentLogPath_;
    LARGE_INTEGER filePosition_;
    std::string incompleteLineBuffer_;
    
    std::atomic<bool> running_;
    mutable std::mutex mutex_;
    
    HANDLE stopEvent_;
    HANDLE fileChangeEvent_;
    HANDLE logFileHandle_;
    
    std::thread watchThread_;
    std::thread readThread_;
};
