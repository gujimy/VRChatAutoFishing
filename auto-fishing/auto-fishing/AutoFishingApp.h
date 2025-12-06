#pragma once
#include "FishingConfig.h"
#include "OSCClient.h"
#include "VRChatLogHandler.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <map>
#include "nlohmann/json.hpp"

// Language enum
enum class Language {
    Chinese,
    English
};

#pragma comment(lib, "comctl32.lib")

// Control IDs
#define IDC_START_BUTTON        1001
#define IDC_CAST_SLIDER         1002
#define IDC_CAST_LABEL          1003
#define IDC_REST_SLIDER         1004
#define IDC_REST_LABEL          1005
#define IDC_TIMEOUT_SLIDER      1006
#define IDC_TIMEOUT_LABEL       1007
#define IDC_STATUS_LABEL        1008
#define IDC_REST_CHECKBOX       1009
#define IDC_RANDOM_CAST_CHECK   1010
#define IDC_RANDOM_MAX_SLIDER   1011
#define IDC_RANDOM_MAX_LABEL    1012
#define IDC_STATS_REELS         1013
#define IDC_STATS_BUCKET        1014
#define IDC_STATS_TIMEOUTS      1015
#define IDC_STATS_RUNTIME       1016

// Hotkey IDs
#define ID_HOTKEY_TOGGLE_WINDOW 2000
#define ID_HOTKEY_START         2001
#define ID_HOTKEY_STOP          2002
#define ID_HOTKEY_RESTART       2003

// Tray Icon Message
#define WM_TRAYICON (WM_USER + 1)

class AutoFishingApp {
private:
    Language currentLanguage;
    NOTIFYICONDATA nid;
    std::map<std::string, HICON> statusIcons;
    HWND hwnd;
    HWND hStartButton;
    HWND hCastSlider;
    HWND hCastLabel;
    HWND hRestTimeLabel_title;
    HWND hRestSlider;
    HWND hRestLabel;
    HWND hTimeoutTimeLabel_title;
    HWND hTimeoutSlider;
    HWND hTimeoutLabel;
    HWND hStatusLabel;
    HWND hRestCheckbox;
    HWND hRandomCastCheck;
    HWND hRandomMaxSlider;
    HWND hRandomMaxLabel;
    HWND hStatsReels;
    HWND hStatsBucket;
    HWND hStatsTimeouts;
    HWND hStatsRuntime;
    
    HFONT hFont;

    OSCClient* oscClient;
    VRChatLogHandler* logHandler;

    std::atomic<bool> running;
    std::atomic<bool> protected_;
    std::atomic<bool> appIsExiting;
    std::string currentAction;
    std::chrono::steady_clock::time_point lastCycleEnd;
    std::thread fishingThread;
    std::thread timeoutThread;
    std::thread statsThread;
    std::mutex statsMutex;
    std::atomic<int> timeoutId;
    bool firstCast;

    struct Stats {
        int reels;
        int timeouts;
        int bucketSuccess;
        std::chrono::steady_clock::time_point startTime;
    } stats;

    double castTime;
    double restTime;
    double timeoutLimit;
    bool restEnabled;
    bool randomCastEnabled;
    double randomCastMax;
    std::chrono::steady_clock::time_point detectedTime;

    void createControls();
    void updateStatus(const std::string& status);
    void updateTrayIcon();
    void updateStats();
    void updateStatsLoop();
    void sendClick(bool press);
    double getCastDuration();
    void performCast();
    void performReel(bool isTimeout = false);
    void forceReel();
    void fishOnHook(const std::string& logContent);
    void waitForFishBucket();
    bool checkFishPickup();
    void startTimeoutTimer();
    void handleTimeout();
    std::wstring stringToWString(const std::string& str);
    Language detectSystemLanguage();
    std::wstring getStatusDisplayText(const std::string& status);
    std::wstring getText(const std::string& key);
    void registerHotkeys();
    void unregisterHotkeys();
    void loadConfig();
    void saveConfig();
    HICON createColoredIcon(COLORREF color);
public:
    void setupTrayIcon();
    void minimizeToTray();
    void restoreFromTray();
    void showTrayMenu();
    AutoFishingApp(HWND hwnd);
    ~AutoFishingApp();

    void onCommand(WPARAM wParam, LPARAM lParam);
    void onHScroll(WPARAM wParam, LPARAM lParam);
    void onTimer(WPARAM wParam);

    void toggle();
    void startFishing();
    void stopFishing();
    void restartFishing();
    void emergencyRelease();

    HWND getHwnd() const { return hwnd; }
};