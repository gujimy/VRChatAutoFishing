#include "VRChatLogHandler.h"
#include "FishingConfig.h"
#include <windows.h>
#include <shlobj.h>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

VRChatLogHandler::VRChatLogHandler(std::function<void(const std::string&)> cb)
    : callback(cb), filePosition(0), running(false) {
    updateLogFile();
}

VRChatLogHandler::~VRChatLogHandler() {
    stop();
}

std::string VRChatLogHandler::getVRChatLogDir() {
    WCHAR appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        // Convert wide string to narrow string properly
        int size = WideCharToMultiByte(CP_UTF8, 0, appDataPath, -1, nullptr, 0, nullptr, nullptr);
        std::string path(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, appDataPath, -1, &path[0], size, nullptr, nullptr);
        // Remove null terminator
        if (!path.empty() && path.back() == '\0') {
            path.pop_back();
        }
        return path + "\\..\\LocalLow\\VRChat\\VRChat";
    }
    return "";
}

std::string VRChatLogHandler::findLatestLog() {
    std::string logDir = getVRChatLogDir();
    if (logDir.empty() || !fs::exists(logDir)) {
        return "";
    }

    std::string latestLog;
    std::filesystem::file_time_type latestTime;
    bool found = false;

    try {
        for (const auto& entry : fs::directory_iterator(logDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // 使用 C++17 兼容的方式检查文件扩展名
                if (filename.find("output_log_") == 0 &&
                    filename.length() >= 4 &&
                    filename.substr(filename.length() - 4) == ".txt") {
                    auto fileTime = fs::last_write_time(entry.path());
                    if (!found || fileTime > latestTime) {
                        latestLog = entry.path().string();
                        latestTime = fileTime;
                        found = true;
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error finding log file: " << e.what() << std::endl;
    }

    return latestLog;
}

bool VRChatLogHandler::updateLogFile() {
    std::string newLog = findLatestLog();
    if (newLog != currentLog && !newLog.empty()) {
        std::lock_guard<std::mutex> lock(logMutex);
        currentLog = newLog;
        // Move file position to the end of the file to ignore old logs
        try {
            std::ifstream file(currentLog, std::ios::ate);
            if (file.is_open()) {
                filePosition = file.tellg();
            } else {
                filePosition = 0;
            }
        } catch (...) {
            filePosition = 0;
        }
        // std::cout << "Detected new log file, starting from end: " << currentLog << std::endl;
        return true;
    }
    return false;
}

std::string VRChatLogHandler::safeReadFile() {
    std::string logPath;
    std::streampos currentPos;

    {
        std::lock_guard<std::mutex> lock(logMutex);
        logPath = currentLog;
        currentPos = filePosition;
    }

    if (logPath.empty() || !fs::exists(logPath)) {
        return "";
    }

    try {
        std::ifstream file(logPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return "";
        }

        std::streampos fileSize = file.tellg();
        if (currentPos > fileSize) {
            // Log file has shrunk or changed, reset.
            currentPos = 0;
        }

        if (currentPos >= fileSize) {
            return ""; // No new content
        }

        file.seekg(currentPos);
        
        std::string content;
        content.resize(fileSize - currentPos);
        file.read(&content[0], content.size());
        
        {
            std::lock_guard<std::mutex> lock(logMutex);
            filePosition = fileSize;
        }

        return content;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to read log file: " << e.what() << std::endl;
    }
    return "";
}

void VRChatLogHandler::checkLogsThread() {
    // std::cout << "Log monitoring thread started" << std::endl;
    
    while (running) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(FishingConfig::LOG_CHECK_INTERVAL * 1000))
        );

        if (updateLogFile()) {
            continue;
        }

        std::string content = safeReadFile();
        if (!content.empty()) {
            // std::cout << "Read log content: " << content.substr(0, (std::min)(100, (int)content.length())) << "..." << std::endl;
        }
        
        if (content.find("SAVED DATA") != std::string::npos) {
            // std::cout << "!!! Found SAVED DATA in log, triggering callback !!!" << std::endl;
            if (callback) {
                callback(content);
            }
        }
    }
    
    // std::cout << "Log monitoring thread stopped" << std::endl;
}

void VRChatLogHandler::startMonitor() {
    if (!running) {
        running = true;
        monitorThread = std::thread(&VRChatLogHandler::checkLogsThread, this);
    }
}

void VRChatLogHandler::stop() {
    if (running) {
        running = false;
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }
}

std::string VRChatLogHandler::getCurrentLogPath() const {
    return currentLog;
}