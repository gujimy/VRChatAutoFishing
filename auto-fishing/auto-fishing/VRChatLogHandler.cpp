#include "VRChatLogHandler.h"
#include "FishingConfig.h"
#include <shlobj.h>
#include <algorithm>

VRChatLogHandler::VRChatLogHandler(LogCallback callback)
    : callback_(std::move(callback))
    , running_(false)
    , stopEvent_(NULL)
    , fileChangeEvent_(NULL)
    , logFileHandle_(INVALID_HANDLE_VALUE)
{
    filePosition_.QuadPart = 0;
    logDirectory_ = getVRChatLogDir();
    updateLogFile();
}

VRChatLogHandler::~VRChatLogHandler() {
    stop();
}

void VRChatLogHandler::startMonitor() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    stopEvent_ = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stopEvent_) {
        running_ = false;
        return;
    }

    if (!logDirectory_.empty()) {
        fileChangeEvent_ = FindFirstChangeNotificationW(
            logDirectory_.c_str(),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
        );
    }

    watchThread_ = std::thread(&VRChatLogHandler::directoryWatchThread, this);
    readThread_ = std::thread(&VRChatLogHandler::fileReadThread, this);
}

void VRChatLogHandler::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    if (stopEvent_) {
        SetEvent(stopEvent_);
    }

    if (watchThread_.joinable()) {
        watchThread_.join();
    }
    if (readThread_.joinable()) {
        readThread_.join();
    }

    if (fileChangeEvent_ && fileChangeEvent_ != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(fileChangeEvent_);
        fileChangeEvent_ = NULL;
    }

    if (logFileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(logFileHandle_);
        logFileHandle_ = INVALID_HANDLE_VALUE;
    }

    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = NULL;
    }
}

std::string VRChatLogHandler::safeReadFile() {
    return readNewContent();
}

std::string VRChatLogHandler::getCurrentLogPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentLogPath_.empty()) {
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, currentLogPath_.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, currentLogPath_.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring VRChatLogHandler::getVRChatLogDir() const {
    PWSTR localLowPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &localLowPath);
    
    if (SUCCEEDED(hr) && localLowPath) {
        std::wstring path(localLowPath);
        CoTaskMemFree(localLowPath);
        return path + L"\\VRChat\\VRChat";
    }
    
    WCHAR appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        return std::wstring(appDataPath) + L"\\..\\LocalLow\\VRChat\\VRChat";
    }
    
    return L"";
}

std::wstring VRChatLogHandler::findLatestLog() const {
    if (logDirectory_.empty()) {
        return L"";
    }

    WIN32_FIND_DATAW findData;
    std::wstring searchPath = logDirectory_ + L"\\output_log_*.txt";
    
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return L"";
    }

    std::wstring latestFile;
    FILETIME latestTime = {0, 0};

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (CompareFileTime(&findData.ftLastWriteTime, &latestTime) > 0) {
                latestTime = findData.ftLastWriteTime;
                latestFile = findData.cFileName;
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    if (latestFile.empty()) {
        return L"";
    }

    return logDirectory_ + L"\\" + latestFile;
}

bool VRChatLogHandler::updateLogFile() {
    std::wstring newLog = findLatestLog();
    
    if (newLog.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    if (newLog == currentLogPath_) {
        return false;
    }

    if (logFileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(logFileHandle_);
        logFileHandle_ = INVALID_HANDLE_VALUE;
    }

    currentLogPath_ = newLog;

    logFileHandle_ = CreateFileW(
        currentLogPath_.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (logFileHandle_ != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(logFileHandle_, &fileSize)) {
            filePosition_ = fileSize;
            SetFilePointerEx(logFileHandle_, filePosition_, NULL, FILE_BEGIN);
        }
    } else {
        filePosition_.QuadPart = 0;
    }

    return true;
}

void VRChatLogHandler::directoryWatchThread() {
    HANDLE handles[2] = { stopEvent_, fileChangeEvent_ };
    int handleCount = fileChangeEvent_ ? 2 : 1;

    while (running_.load(std::memory_order_acquire)) {
        DWORD waitResult = WaitForMultipleObjects(
            handleCount,
            handles,
            FALSE,
            static_cast<DWORD>(FishingConfig::LOG_CHECK_INTERVAL * 1000)
        );

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        updateLogFile();

        if (waitResult == WAIT_OBJECT_0 + 1 && fileChangeEvent_) {
            FindNextChangeNotification(fileChangeEvent_);
        }
    }
}

void VRChatLogHandler::fileReadThread() {
    while (running_.load(std::memory_order_acquire)) {
        DWORD waitResult = WaitForSingleObject(
            stopEvent_,
            static_cast<DWORD>(FishingConfig::LOG_CHECK_INTERVAL * 1000)
        );

        if (!running_.load(std::memory_order_acquire) || waitResult == WAIT_OBJECT_0) {
            break;
        }

        std::string content = readNewContent();
        if (!content.empty()) {
            processLogContent(content);
        }
    }
}

std::string VRChatLogHandler::readNewContent() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (logFileHandle_ == INVALID_HANDLE_VALUE) {
        return "";
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(logFileHandle_, &fileSize)) {
        return "";
    }

    if (filePosition_.QuadPart > fileSize.QuadPart) {
        filePosition_.QuadPart = 0;
        SetFilePointerEx(logFileHandle_, filePosition_, NULL, FILE_BEGIN);
    }

    if (filePosition_.QuadPart >= fileSize.QuadPart) {
        return "";
    }

    LONGLONG bytesToRead = fileSize.QuadPart - filePosition_.QuadPart;
    if (bytesToRead <= 0 || bytesToRead > 10 * 1024 * 1024) {
        return "";
    }

    std::string buffer(static_cast<size_t>(bytesToRead), '\0');
    DWORD bytesRead = 0;

    SetFilePointerEx(logFileHandle_, filePosition_, NULL, FILE_BEGIN);
    
    if (!ReadFile(logFileHandle_, buffer.data(), static_cast<DWORD>(bytesToRead), &bytesRead, NULL)) {
        return "";
    }

    if (bytesRead > 0) {
        buffer.resize(bytesRead);
        filePosition_.QuadPart += bytesRead;
    } else {
        buffer.clear();
    }

    return buffer;
}

void VRChatLogHandler::processLogContent(const std::string& content) {
    if (content.find(FISH_HOOK_KEYWORD) != std::string::npos) {
        if (callback_) {
            try {
                callback_(content);
            } catch (...) {
            }
        }
    }
}