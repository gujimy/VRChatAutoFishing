
#include "AutoFishingApp.h"
#include "resource.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <regex>
#include <ctime>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// RAII guard for the protected_ flag
struct ProtectedGuard {
    std::atomic<bool>& flag;
    ProtectedGuard(std::atomic<bool>& f) : flag(f) {
        flag = true;
        // std::cout << "ProtectedGuard: flag set to true" << std::endl;
    }
    ~ProtectedGuard() {
        flag = false;
        // std::cout << "ProtectedGuard: flag set to false" << std::endl;
    }
};

AutoFishingApp::AutoFishingApp(HWND hwnd)
    : hwnd(hwnd), running(false), protected_(false), appIsExiting(false),
      reelTimeoutFlag_(false), firstCast(true),
      castTime(FishingConfig::DEFAULT_CAST_TIME),
      restTime(FishingConfig::DEFAULT_REST_TIME),
      timeoutLimit(FishingConfig::DEFAULT_TIMEOUT_MINUTES),
      randomCastEnabled(false), randomCastMax(1.0), noCastMode(false),
      timeoutId(0), reelTimeoutId_(0), castCycleId_(0),
      pendingBucketCycleId_(0), pendingBucketRetry_(0),
      fishPickupDetected_(false), hFont(nullptr) {
    
    // Detect system language
    currentLanguage = detectSystemLanguage();
    
    stats.reels = 0;
    stats.timeouts = 0;
    stats.bucketSuccess = 0;
    auto nowSteady = std::chrono::steady_clock::now();
    auto nowWall = std::chrono::system_clock::now();
    lastCycleEnd = nowSteady;
    waitHookStartedAt_ = nowSteady;
    currentCycleStartedAt_ = nowSteady;
    pendingBucketStartedAt_ = nowSteady;
    waitHookStartedWallAt_ = nowWall;
    pendingBucketMinEventAt_ = nowWall;
    lastBucketSavedAt_ = nowWall - std::chrono::seconds(60);
    lastHookSavedEventAt_ = nowWall - std::chrono::seconds(60);
    fishPickupDetectedAt_ = nowSteady;
    
    // Create a better font for Chinese text display
    hFont = CreateFontW(
        20,                        // Height - increased for better readability
        0,                         // Width
        0,                         // Escapement
        0,                         // Orientation
        FW_NORMAL,                 // Weight
        FALSE,                     // Italic
        FALSE,                     // Underline
        FALSE,                     // StrikeOut
        DEFAULT_CHARSET,           // CharSet
        OUT_DEFAULT_PRECIS,        // OutPrecision
        CLIP_DEFAULT_PRECIS,       // ClipPrecision
        CLEARTYPE_QUALITY,         // Quality - use ClearType for better rendering
        DEFAULT_PITCH | FF_DONTCARE, // PitchAndFamily
        L"Microsoft YaHei"         // Facename - good for Chinese
    );

    oscClient = new OSCClient("127.0.0.1", 9000);
    if (!oscClient->initialize()) {
        MessageBoxW(hwnd, L"Initialize OSC Client Failed", L"Error", MB_OK | MB_ICONERROR);
    }

    logHandler = new VRChatLogHandler([this](LogEventType eventType, const std::string& line) {
        this->onLogEvent(eventType, line);
    });
    logHandler->startMonitor();

    lastCastTime_ = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    createControls();
    sendClick(false);

    loadConfig(); // Load config after creating controls

    statsThread = std::thread(&AutoFishingApp::updateStatsLoop, this);

    registerHotkeys();

    // Create icons for tray status - English names internally
    statusIcons["Waiting"] = createColoredIcon(RGB(128, 128, 128)); // Gray
    statusIcons["Starting"] = createColoredIcon(RGB(255, 165, 0)); // Orange
    statusIcons["Casting"] = createColoredIcon(RGB(255, 69, 0)); // OrangeRed
    statusIcons["WaitingFish"] = createColoredIcon(RGB(0, 255, 0)); // Green
    statusIcons["Reeling"] = createColoredIcon(RGB(255, 215, 0)); // Gold
    statusIcons["WaitingBucket"] = createColoredIcon(RGB(147, 112, 219)); // MediumPurple
    statusIcons["Resting"] = createColoredIcon(RGB(135, 206, 235)); // SkyBlue
    statusIcons["Timeout"] = createColoredIcon(RGB(255, 99, 71)); // Tomato
    statusIcons["Stopped"] = createColoredIcon(RGB(128, 128, 128)); // Gray

    setupTrayIcon();
}

AutoFishingApp::~AutoFishingApp() {
    appIsExiting = true;
    saveConfig();
    Shell_NotifyIcon(NIM_DELETE, &nid);

    if (hFont) {
        DeleteObject(hFont);
    }

    for (auto const& [key, val] : statusIcons) {
        DestroyIcon(val);
    }

    unregisterHotkeys();
    stopFishing();
    
    if (statsThread.joinable()) {
        statsThread.join();
    }

    if (logHandler) {
        logHandler->stop();
        delete logHandler;
    }

    if (oscClient) {
        delete oscClient;
    }
}

std::wstring AutoFishingApp::stringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

Language AutoFishingApp::detectSystemLanguage() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    
    // Check if system language is Chinese (Simplified or Traditional)
    if (primaryLang == LANG_CHINESE) {
        return Language::Chinese;
    }
    
    // Default to English for all other languages
    return Language::English;
}

std::wstring AutoFishingApp::getText(const std::string& key) {
    auto versionToWString = []() -> std::wstring {
        std::string ver = FishingConfig::VERSION;
        return std::wstring(ver.begin(), ver.end());
    };

    if (key == "title") {
        const std::wstring version = versionToWString();
        if (currentLanguage == Language::Chinese) {
            return L"VRChat \u81ea\u52a8\u9493\u9c7c v" + version;
        }
        return L"VRChat Auto Fishing v" + version;
    }

    if (key == "tray_tooltip") {
        const std::wstring version = versionToWString();
        if (currentLanguage == Language::Chinese) {
            return L"VRChat \u81ea\u52a8\u9493\u9c7c v" + version;
        }
        return L"VRChat Auto Fishing v" + version;
    }

    static const std::map<std::string, std::map<Language, std::wstring>> textMap = {
        {"cast_time", {{Language::Chinese, L"\u84c4\u529b\u65f6\u95f4:"},
                       {Language::English, L"Cast Time:"}}},
        {"rest_time", {{Language::Chinese, L"\u4f11\u606f\u65f6\u95f4:"},
                       {Language::English, L"Rest Time:"}}},
        {"timeout_time", {{Language::Chinese, L"\u8d85\u65f6\u65f6\u95f4:"},
                          {Language::English, L"Timeout:"}}},
        {"random_cast", {{Language::Chinese, L"\u968f\u673a\u84c4\u529b\u65f6\u95f4"},
                         {Language::English, L"Random Cast Time"}}},
        {"random_max", {{Language::Chinese, L"\u968f\u673a\u6700\u5927\u503c:"},
                        {Language::English, L"Random Max:"}}},
        {"no_cast_mode", {{Language::Chinese, L"\u65e0\u629b\u7aff\u6a21\u5f0f"},
                          {Language::English, L"No Cast Mode"}}},
        {"start", {{Language::Chinese, L"\u5f00\u59cb"},
                   {Language::English, L"Start"}}},
        {"stop", {{Language::Chinese, L"\u505c\u6b62"},
                  {Language::English, L"Stop"}}},
        {"status", {{Language::Chinese, L"\u72b6\u6001:"},
                    {Language::English, L"Status:"}}},
        {"cast_runtime", {{Language::Chinese, L"\u672c\u6746\u65f6\u95f4:"},
                          {Language::English, L"Cast Time:"}}},
        {"statistics", {{Language::Chinese, L"\u7edf\u8ba1\u4fe1\u606f"},
                        {Language::English, L"Statistics"}}},
        {"reels", {{Language::Chinese, L"\u6536\u7aff\u6b21\u6570:"},
                   {Language::English, L"Reels:"}}},
        {"bucket", {{Language::Chinese, L"\u88c5\u6876\u6b21\u6570:"},
                    {Language::English, L"Bucket:"}}},
        {"timeouts", {{Language::Chinese, L"\u8d85\u65f6\u6b21\u6570:"},
                      {Language::English, L"Timeouts:"}}},
        {"runtime", {{Language::Chinese, L"\u8fd0\u884c\u65f6\u95f4:"},
                     {Language::English, L"Runtime:"}}},
        {"hotkeys", {{Language::Chinese, L"\u5feb\u6377\u952e: Ctrl+F4: \u663e\u793a/\u9690\u85cf  Ctrl+F5: \u5f00\u59cb  Ctrl+F6: \u505c\u6b62  Ctrl+F7: \u91cd\u9493"},
                     {Language::English, L"Hotkeys: Ctrl+F4: Show/Hide  Ctrl+F5: Start  Ctrl+F6: Stop  Ctrl+F7: Restart"}}}
    };
    
    auto it = textMap.find(key);
    if (it != textMap.end()) {
        auto langIt = it->second.find(currentLanguage);
        if (langIt != it->second.end()) {
            return langIt->second;
        }
    }
    return L"[Missing: " + std::wstring(key.begin(), key.end()) + L"]";
}

std::wstring AutoFishingApp::getStatusDisplayText(const std::string& status) {
    static const std::map<std::string, std::map<Language, std::wstring>> statusMap = {
        {"Waiting", {{Language::Chinese, L"\u7b49\u5f85"},
                     {Language::English, L"Waiting"}}},
        {"Starting", {{Language::Chinese, L"\u5f00\u59cb\u629b\u7aff"},
                      {Language::English, L"Starting Cast"}}},
        {"Casting", {{Language::Chinese, L"\u9c7c\u7aff\u84c4\u529b\u4e2d"},
                     {Language::English, L"Casting"}}},
        {"WaitingFish", {{Language::Chinese, L"\u7b49\u5f85\u9c7c\u4e0a\u94a9"},
                         {Language::English, L"Waiting Fish"}}},
        {"Reeling", {{Language::Chinese, L"\u6536\u7aff\u4e2d"},
                     {Language::English, L"Reeling"}}},
        {"WaitingBucket", {{Language::Chinese, L"\u7b49\u5f85\u9c7c\u88c5\u6876"},
                           {Language::English, L"Waiting Bucket"}}},
        {"Resting", {{Language::Chinese, L"\u4f11\u606f\u4e2d"},
                     {Language::English, L"Resting"}}},
        {"Timeout", {{Language::Chinese, L"\u8d85\u65f6\u6536\u7aff"},
                     {Language::English, L"Timeout Reel"}}},
        {"Stopped", {{Language::Chinese, L"\u5df2\u505c\u6b62"},
                     {Language::English, L"Stopped"}}}
    };
    
    auto it = statusMap.find(status);
    if (it != statusMap.end()) {
        auto langIt = it->second.find(currentLanguage);
        if (langIt != it->second.end()) {
            return langIt->second;
        }
    }
    return L"Unknown";
}

void AutoFishingApp::createControls() {
    int y = 20;
    int labelWidth = 100;
    int sliderWidth = 280;
    int valueWidth = 50;

    HWND hTitle = CreateWindowW(L"STATIC", getText("title").c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, y, 440, 30, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 40;

    hCastTimeLabel = CreateWindowW(L"STATIC", getText("cast_time").c_str(),
        WS_CHILD | WS_VISIBLE,
        10, y, labelWidth, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hCastTimeLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hCastSlider = CreateWindowW(TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        labelWidth + 10, y, sliderWidth, 30, hwnd, (HMENU)IDC_CAST_SLIDER, nullptr, nullptr);
    SendMessage(hCastSlider, TBM_SETRANGE, TRUE, MAKELPARAM(2, 20));
    SendMessage(hCastSlider, TBM_SETPOS, TRUE, 5);
    hCastLabel = CreateWindowW(L"STATIC", L"0.5s",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        labelWidth + sliderWidth + 20, y, valueWidth, 20, hwnd, (HMENU)IDC_CAST_LABEL, nullptr, nullptr);
    if (hFont) SendMessage(hCastLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 40;

    hRestTimeLabel_title = CreateWindowW(L"STATIC", getText("rest_time").c_str(),
        WS_CHILD | WS_VISIBLE,
        10, y, labelWidth, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hRestTimeLabel_title, WM_SETFONT, (WPARAM)hFont, TRUE);
    hRestSlider = CreateWindowW(TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        labelWidth + 10, y, sliderWidth, 30, hwnd, (HMENU)IDC_REST_SLIDER, nullptr, nullptr);
    SendMessage(hRestSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessage(hRestSlider, TBM_SETPOS, TRUE, 5);
    hRestLabel = CreateWindowW(L"STATIC", L"0.5s",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        labelWidth + sliderWidth + 20, y, valueWidth, 20, hwnd, (HMENU)IDC_REST_LABEL, nullptr, nullptr);
    if (hFont) SendMessage(hRestLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 40;

    hTimeoutTimeLabel_title = CreateWindowW(L"STATIC", getText("timeout_time").c_str(),
        WS_CHILD | WS_VISIBLE, // Always visible
        10, y, labelWidth, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hTimeoutTimeLabel_title, WM_SETFONT, (WPARAM)hFont, TRUE);
    hTimeoutSlider = CreateWindowW(TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, // Always visible
        labelWidth + 10, y, sliderWidth, 30, hwnd, (HMENU)IDC_TIMEOUT_SLIDER, nullptr, nullptr);
    SendMessage(hTimeoutSlider, TBM_SETRANGE, TRUE, MAKELPARAM(5, 20)); // Max 2.0 minutes
    SendMessage(hTimeoutSlider, TBM_SETPOS, TRUE, 10);
    hTimeoutLabel = CreateWindowW(L"STATIC", L"1.0min",
        WS_CHILD | WS_VISIBLE | SS_RIGHT, // Always visible
        labelWidth + sliderWidth + 20, y, valueWidth, 20, hwnd, (HMENU)IDC_TIMEOUT_LABEL, nullptr, nullptr);
    if (hFont) SendMessage(hTimeoutLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 40;

    hRandomCastCheck = CreateWindowW(L"BUTTON", getText("random_cast").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        10, y, 200, 20, hwnd, (HMENU)IDC_RANDOM_CAST_CHECK, nullptr, nullptr);
    if (hFont) SendMessage(hRandomCastCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    hNoCastCheckbox = CreateWindowW(L"BUTTON", getText("no_cast_mode").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        220, y, 200, 20, hwnd, (HMENU)IDC_NO_CAST_CHECKBOX, nullptr, nullptr);
    if (hFont) SendMessage(hNoCastCheckbox, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;

    hRandomMaxTitleLabel = CreateWindowW(L"STATIC", getText("random_max").c_str(),
        WS_CHILD | WS_VISIBLE,
        10, y, labelWidth, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hRandomMaxTitleLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hRandomMaxSlider = CreateWindowW(TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | WS_DISABLED,
        labelWidth + 10, y, sliderWidth, 30, hwnd, (HMENU)IDC_RANDOM_MAX_SLIDER, nullptr, nullptr);
    SendMessage(hRandomMaxSlider, TBM_SETRANGE, TRUE, MAKELPARAM(3, 20));
    SendMessage(hRandomMaxSlider, TBM_SETPOS, TRUE, 10);
    hRandomMaxLabel = CreateWindowW(L"STATIC", L"1.0s",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        labelWidth + sliderWidth + 20, y, valueWidth, 20, hwnd, (HMENU)IDC_RANDOM_MAX_LABEL, nullptr, nullptr);
    if (hFont) SendMessage(hRandomMaxLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 50;

    hStartButton = CreateWindowW(L"BUTTON", getText("start").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, y, 200, 40, hwnd, (HMENU)IDC_START_BUTTON, nullptr, nullptr);
    if (hFont) SendMessage(hStartButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 50;

    HWND hStatusTitleLabel = CreateWindowW(L"STATIC", getText("status").c_str(),
        WS_CHILD | WS_VISIBLE,
        10, y, 60, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hStatusTitleLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hStatusLabel = CreateWindowW(L"STATIC", L"[\u7b49\u5f85]",
        WS_CHILD | WS_VISIBLE,
        70, y, 190, 20, hwnd, (HMENU)IDC_STATUS_LABEL, nullptr, nullptr);
    if (hFont) SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    std::wstring castRuntimeText = getText("cast_runtime") + L"0s";
    hStatusCastRuntime = CreateWindowW(L"STATIC", castRuntimeText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        260, y, 190, 20, hwnd, (HMENU)IDC_STATUS_CAST_RUNTIME, nullptr, nullptr);
    if (hFont) SendMessage(hStatusCastRuntime, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;

    HWND hStatsTitle = CreateWindowW(L"STATIC", getText("statistics").c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, y, 440, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hStatsTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;

    HWND hReelsLabel = CreateWindowW(L"STATIC", getText("reels").c_str(),
        WS_CHILD | WS_VISIBLE,
        50, y, 100, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hReelsLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hStatsReels = CreateWindowW(L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE,
        150, y, 100, 20, hwnd, (HMENU)IDC_STATS_REELS, nullptr, nullptr);
    if (hFont) SendMessage(hStatsReels, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBucketLabel = CreateWindowW(L"STATIC", getText("bucket").c_str(),
        WS_CHILD | WS_VISIBLE,
        300, y, 100, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hBucketLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hStatsBucket = CreateWindowW(L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE,
        400, y, 100, 20, hwnd, (HMENU)IDC_STATS_BUCKET, nullptr, nullptr);
    if (hFont) SendMessage(hStatsBucket, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 25;

    HWND hTimeoutsLabel = CreateWindowW(L"STATIC", getText("timeouts").c_str(),
        WS_CHILD | WS_VISIBLE,
        50, y, 100, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hTimeoutsLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hStatsTimeouts = CreateWindowW(L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE,
        150, y, 100, 20, hwnd, (HMENU)IDC_STATS_TIMEOUTS, nullptr, nullptr);
    if (hFont) SendMessage(hStatsTimeouts, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hRuntimeLabel = CreateWindowW(L"STATIC", getText("runtime").c_str(),
        WS_CHILD | WS_VISIBLE,
        300, y, 100, 20, hwnd, nullptr, nullptr, nullptr);
    if (hFont) SendMessage(hRuntimeLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    hStatsRuntime = CreateWindowW(L"STATIC", L"0s",
        WS_CHILD | WS_VISIBLE,
        400, y, 100, 20, hwnd, (HMENU)IDC_STATS_RUNTIME, nullptr, nullptr);
    if (hFont) SendMessage(hStatsRuntime, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 30;

    // Hotkeys information
    HWND hHotkeysLabel = CreateWindowW(L"STATIC", getText("hotkeys").c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, y, 440, 20, hwnd, nullptr, nullptr, nullptr);
    
    // Use main font for hotkeys to make them more readable
    if (hFont) SendMessage(hHotkeysLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
}

void AutoFishingApp::onCommand(WPARAM wParam, LPARAM lParam) {
    int id = LOWORD(wParam);
    int event = HIWORD(wParam);

    switch (id) {
    case IDC_START_BUTTON:
        toggle();
        break;
    case IDC_RANDOM_CAST_CHECK:
        if (event == BN_CLICKED) {
            randomCastEnabled = (SendMessage(hRandomCastCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(hRandomMaxSlider, randomCastEnabled);
        }
        break;
    case IDC_NO_CAST_CHECKBOX:
        if (event == BN_CLICKED) {
            noCastMode = (SendMessage(hNoCastCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);

            // Hide/show cast time related controls
            int showCast = noCastMode ? SW_HIDE : SW_SHOW;
            ShowWindow(hCastTimeLabel, showCast);
            ShowWindow(hCastSlider, showCast);
            ShowWindow(hCastLabel, showCast);
            ShowWindow(hRandomCastCheck, showCast);
            ShowWindow(hRandomMaxTitleLabel, showCast);
            ShowWindow(hRandomMaxSlider, showCast);
            ShowWindow(hRandomMaxLabel, showCast);
        }
        break;
    }
}

void AutoFishingApp::onHScroll(WPARAM wParam, LPARAM lParam) {
    HWND hSlider = (HWND)lParam;
    int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);

    if (hSlider == hCastSlider) {
        castTime = pos * 0.1;
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << castTime << L"s";
        SetWindowTextW(hCastLabel, ss.str().c_str());
    }
    else if (hSlider == hRestSlider) {
        restTime = pos * 0.1;
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << restTime << L"s";
        SetWindowTextW(hRestLabel, ss.str().c_str());
    }
    else if (hSlider == hTimeoutSlider) {
        timeoutLimit = pos * 0.1;
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << timeoutLimit << L"min";
        SetWindowTextW(hTimeoutLabel, ss.str().c_str());
    }
    else if (hSlider == hRandomMaxSlider) {
        randomCastMax = pos * 0.1;
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << randomCastMax << L"s";
        SetWindowTextW(hRandomMaxLabel, ss.str().c_str());
    }
}

void AutoFishingApp::toggle() {
    // std::cout << "--- toggle() called ---" << std::endl;
    // std::cout << "  - running was: " << (running ? "true" : "false") << std::endl;
    running = !running;
    // std::cout << "  - running is now: " << (running ? "true" : "false") << std::endl;
    SetWindowTextW(hStartButton, running ? getText("stop").c_str() : getText("start").c_str());

    if (running) {
        firstCast = true;
        castCycleId_ = 0;
        fishPickupDetected_ = false;
        clearDeferredBucketTracking();
        auto nowSteady = std::chrono::steady_clock::now();
        auto nowWall = std::chrono::system_clock::now();
        waitHookStartedAt_ = nowSteady;
        waitHookStartedWallAt_ = nowWall;
        currentCycleStartedAt_ = nowSteady;
        lastHookSavedEventAt_ = nowWall - std::chrono::seconds(60);
        lastBucketSavedAt_ = nowWall - std::chrono::seconds(60);
        currentAction = "Starting";
        reelTimeoutFlag_ = false;
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            stats.startTime = std::chrono::steady_clock::now();
        }
        updateStatus(currentAction);
        updateStats();
        fishingThread = std::thread(&AutoFishingApp::performCast, this);
        fishingThread.detach();
    }
    else {
        timeoutId++;
        reelTimeoutId_++;
        clearDeferredBucketTracking();
        fishPickupDetected_ = false;
        emergencyRelease();
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            stats.reels = 0;
            stats.timeouts = 0;
            stats.bucketSuccess = 0;
        }
        updateStats();
    }
}

void AutoFishingApp::emergencyRelease() {
    sendClick(false);
    currentAction = "Stopped";
    updateStatus(currentAction);
}

void AutoFishingApp::updateStatus(const std::string& status) {
    currentAction = status;
    std::wstring wStatus = L"[" + getStatusDisplayText(status) + L"]";
    SetWindowTextW(hStatusLabel, wStatus.c_str());
    updateTrayIcon();
}

void AutoFishingApp::updateTrayIcon() {
    HICON newIcon = statusIcons.count(currentAction) ? statusIcons[currentAction] : statusIcons["Stopped"];
    if (newIcon) {
        nid.hIcon = newIcon;
        
        // Update tooltip with current status and statistics
        std::wstring statusText = getStatusDisplayText(currentAction);
        std::wstringstream tooltip;
        
        int reels, bucket;
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            reels = stats.reels;
            bucket = stats.bucketSuccess;
        }
        
        tooltip << getText("tray_tooltip") << L" - " << statusText;
        if (currentLanguage == Language::Chinese) {
            tooltip << L" | \u6536\u7aff: " << reels
                    << L" | \u88c5\u6876: " << bucket;
        } else {
            tooltip << L" | Reels: " << reels
                    << L" | Bucket: " << bucket;
        }
        
        wcsncpy_s(nid.szTip, tooltip.str().c_str(), _TRUNCATE);
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
}

void AutoFishingApp::updateStats() {
    std::lock_guard<std::mutex> lock(statsMutex);
    
    int castSeconds = 0;
    if (running) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - stats.startTime);
        int seconds = (int)duration.count();
        castSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(now - currentCycleStartedAt_).count();
        if (castSeconds < 0) castSeconds = 0;
        
        std::wstringstream ss;
        if (seconds >= 60) {
            ss << std::fixed << std::setprecision(1) << (seconds / 60.0) << L"min";
        } else {
            ss << seconds << L"s";
        }
        SetWindowTextW(hStatsRuntime, ss.str().c_str());
    }
    
    SetWindowTextW(hStatsReels, std::to_wstring(stats.reels).c_str());
    SetWindowTextW(hStatsBucket, std::to_wstring(stats.bucketSuccess).c_str());
    SetWindowTextW(hStatsTimeouts, std::to_wstring(stats.timeouts).c_str());

    std::wstringstream castSs;
    castSs << getText("cast_runtime") << castSeconds << L"s";
    SetWindowTextW(hStatusCastRuntime, castSs.str().c_str());
}

void AutoFishingApp::updateStatsLoop() {
    while (!appIsExiting) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (running && !appIsExiting) {
            updateStats();
        }
    }
}

void AutoFishingApp::sendClick(bool press) {
    if (oscClient) {
        oscClient->sendClick(press);
    }
}

double AutoFishingApp::getCastDuration() {
    if (randomCastEnabled) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(FishingConfig::MIN_CAST_TIME, randomCastMax);
        return dis(gen);
    }
    return castTime;
}

void AutoFishingApp::performCast() {
    if (!running) return;

    maybeRecoverMissingBucket();

    if (firstCast) {
        firstCast = false;
    }

    castCycleId_++;
    currentCycleStartedAt_ = std::chrono::steady_clock::now();

    if (noCastMode) {
        waitHookStartedAt_ = std::chrono::steady_clock::now();
        waitHookStartedWallAt_ = std::chrono::system_clock::now();
        currentAction = "WaitingFish";
        updateStatus(currentAction);
        startTimeoutTimer();
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(FishingConfig::CAST_WAIT_TIME * 1000)));
        return;
    }

    // Normal casting mode
    currentAction = "Casting";
    updateStatus(currentAction);
    double duration = getCastDuration();
    updateStats();

    sendClick(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(duration * 1000)));
    sendClick(false);

    if (!running) return;

    lastCastTime_ = std::chrono::steady_clock::now();
    waitHookStartedAt_ = std::chrono::steady_clock::now();
    waitHookStartedWallAt_ = std::chrono::system_clock::now();
    currentAction = "WaitingFish";
    updateStatus(currentAction);
    startTimeoutTimer();
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(FishingConfig::CAST_WAIT_TIME * 1000)));
}

bool AutoFishingApp::performReel(bool isTimeout) {
    currentAction = "Reeling";
    updateStatus(currentAction);
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.reels++;
    }
    updateStats();

    reelTimeoutFlag_ = false;
    startReelTimeoutTimer();

    sendClick(true);

    bool confirmed = false;

    if (isTimeout) {
        auto reelStart = std::chrono::steady_clock::now();
        while (running && !reelTimeoutFlag_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - reelStart).count();
            if (elapsed >= FishingConfig::TIMEOUT_REEL_WAIT) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        bool success = checkFishPickup();
        if (success && !reelTimeoutFlag_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - detectedTime).count() / 1000.0;
            double remaining = (std::max)(0.0, FishingConfig::FISH_PICKUP_WAIT_TIME - elapsed);
            if (remaining > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(remaining * 1000)));
            }
            confirmed = true;
        }
    }

    reelTimeoutId_++;
    fishPickupDetected_ = false;
    sendClick(false);
    return confirmed;
}

void AutoFishingApp::startReelTimeoutTimer() {
    int currentReelTimeoutId = ++reelTimeoutId_;
    
    reelTimeoutThread_ = std::thread([this, currentReelTimeoutId]() {
        int timeoutMs = static_cast<int>(FishingConfig::MAX_REEL_TIME * 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        
        if (reelTimeoutId_ == currentReelTimeoutId && running) {
            handleReelTimeout();
        }
    });
    reelTimeoutThread_.detach();
}

void AutoFishingApp::handleReelTimeout() {
    reelTimeoutFlag_ = true;
}

bool AutoFishingApp::checkFishPickup() {
    auto startTime = std::chrono::steady_clock::now();
    auto waitStartWithTolerance = waitHookStartedWallAt_ - std::chrono::milliseconds(200);

    while (running && !reelTimeoutFlag_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        
        if (elapsed >= FishingConfig::FISH_PICKUP_TIMEOUT) {
            break;
        }

        if (fishPickupDetected_) {
            detectedTime = fishPickupDetectedAt_;
            return true;
        }

        // Fallback detection: incremental stream + tail snapshot to reduce missed Toggles(True).
        std::string contentStream = logHandler ? logHandler->safeReadFile() : "";
        std::string contentTail = logHandler ? logHandler->readTail() : "";
        std::string merged = contentStream;
        if (!contentTail.empty()) {
            if (!merged.empty()) {
                merged += "\n";
            }
            merged += contentTail;
        }

        if (!merged.empty()) {
            std::istringstream stream(merged);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.find(VRChatLogHandler::FISH_PICKUP_KEYWORD) == std::string::npos) {
                    continue;
                }
                auto eventTime = extractLogTimestamp(line);
                if (eventTime && *eventTime >= waitStartWithTolerance) {
                    detectedTime = std::chrono::steady_clock::now();
                    fishPickupDetected_ = true;
                    fishPickupDetectedAt_ = detectedTime;
                    return true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return false;
}

void AutoFishingApp::startTimeoutTimer() {
    int currentTimeoutId = ++timeoutId;
    
    if (timeoutThread.joinable()) {
        timeoutThread.join();
    }
    
    timeoutThread = std::thread([this, currentTimeoutId]() {
        int timeoutMs = static_cast<int>(timeoutLimit * 60 * 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        
        if (timeoutId == currentTimeoutId && running && currentAction == "WaitingFish") {
            handleTimeout();
        }
    });
    timeoutThread.detach();
}

void AutoFishingApp::handleTimeout() {
    if (running && currentAction == "WaitingFish") {
        currentAction = "Timeout";
        updateStatus(currentAction);
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            stats.timeouts++;
        }
        updateStats();
        forceReel();
    }
}

void AutoFishingApp::forceReel() {
    if (protected_ || !running) return;

    ProtectedGuard guard(protected_);
    (void)performReel(true);

    if (!running) return;

    currentAction = "Resting";
    updateStatus(currentAction);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(FishingConfig::FORCE_REEL_REST * 1000)));

    if (running) {
        performCast();
    }
}

void AutoFishingApp::onLogEvent(LogEventType eventType, const std::string& line) {
    switch (eventType) {
        case LogEventType::FishOnHook:
            fishOnHook(line);
            break;
        case LogEventType::FishPickup:
            fishPickup(line);
            break;
        case LogEventType::BucketSave:
            bucketSave();
            break;
    }
}

std::optional<std::chrono::system_clock::time_point> AutoFishingApp::extractLogTimestamp(const std::string& line) const {
    static const std::regex kLineTimePattern(R"((\d{4})\.(\d{2})\.(\d{2}) (\d{2}):(\d{2}):(\d{2}))");
    std::smatch match;
    if (!std::regex_search(line, match, kLineTimePattern) || match.size() < 7) {
        return std::nullopt;
    }

    std::tm tmValue{};
    tmValue.tm_year = std::stoi(match[1].str()) - 1900;
    tmValue.tm_mon = std::stoi(match[2].str()) - 1;
    tmValue.tm_mday = std::stoi(match[3].str());
    tmValue.tm_hour = std::stoi(match[4].str());
    tmValue.tm_min = std::stoi(match[5].str());
    tmValue.tm_sec = std::stoi(match[6].str());
    tmValue.tm_isdst = -1;

    std::time_t localTime = std::mktime(&tmValue);
    if (localTime == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(localTime);
}

bool AutoFishingApp::tryConsumeDeferredBucket(const std::string& line) {
    if (!running || pendingBucketCycleId_ <= 0 || line.empty()) {
        return false;
    }

    if (line.find(VRChatLogHandler::FISH_HOOK_KEYWORD) == std::string::npos) {
        return false;
    }

    auto eventTime = extractLogTimestamp(line);
    if (!eventTime || *eventTime < pendingBucketMinEventAt_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.bucketSuccess++;
    }
    updateStats();

    lastBucketSavedAt_ = *eventTime;
    clearDeferredBucketTracking();
    return true;
}

void AutoFishingApp::startDeferredBucketTracking(int cycleId, const std::optional<std::chrono::system_clock::time_point>& minEventAt) {
    pendingBucketCycleId_ = cycleId;
    pendingBucketRetry_ = 0;
    pendingBucketStartedAt_ = std::chrono::steady_clock::now();
    pendingBucketMinEventAt_ = minEventAt.value_or(std::chrono::system_clock::time_point{});
}

void AutoFishingApp::clearDeferredBucketTracking() {
    pendingBucketCycleId_ = 0;
    pendingBucketRetry_ = 0;
    pendingBucketStartedAt_ = std::chrono::steady_clock::now();
    pendingBucketMinEventAt_ = std::chrono::system_clock::time_point{};
}

void AutoFishingApp::maybeRecoverMissingBucket() {
    if (!running || pendingBucketCycleId_ <= 0) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pendingBucketStartedAt_).count() / 1000.0;
    if (elapsed < FishingConfig::BUCKET_SAVE_TIMEOUT_SECONDS) {
        return;
    }

    if (pendingBucketRetry_ >= FishingConfig::BUCKET_RECOVERY_MAX_RETRY) {
        clearDeferredBucketTracking();
        return;
    }

    pendingBucketRetry_++;
    sendClick(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    sendClick(false);
    pendingBucketStartedAt_ = std::chrono::steady_clock::now();
}

void AutoFishingApp::fishOnHook(const std::string& line) {
    if (tryConsumeDeferredBucket(line)) {
        return;
    }

    auto eventTime = extractLogTimestamp(line);
    if (!eventTime) {
        return;
    }

    auto nowSteady = std::chrono::steady_clock::now();

    auto elapsedSinceCycle = std::chrono::duration_cast<std::chrono::seconds>(
        nowSteady - lastCycleEnd).count();

    if (!running || protected_ || currentAction != "WaitingFish" || elapsedSinceCycle < FishingConfig::CYCLE_COOLDOWN) {
        return;
    }

    auto sinceWait = std::chrono::duration_cast<std::chrono::milliseconds>(nowSteady - waitHookStartedAt_).count() / 1000.0;
    if (sinceWait < FishingConfig::HOOK_MIN_WAIT_SECONDS) {
        return;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(*eventTime - lastBucketSavedAt_).count() / 1000.0
        <= FishingConfig::BUCKET_EVENT_COOLDOWN_SECONDS) {
        return;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(*eventTime - lastHookSavedEventAt_).count() / 1000.0
        <= FishingConfig::SAVED_DATA_CLUSTER_SECONDS) {
        return;
    }

    auto waitStartWithTolerance = waitHookStartedWallAt_ - std::chrono::milliseconds(200);
    if (*eventTime < waitStartWithTolerance) {
        return;
    }

    ProtectedGuard guard(protected_);
    timeoutId++;
    lastCycleEnd = std::chrono::steady_clock::now();
    lastHookSavedEventAt_ = *eventTime;
    fishPickupDetected_ = false;
    bool reelConfirmed = performReel(false);

    if (!running) return;

    if (!reelConfirmed) {
        currentAction = "Resting";
        updateStatus(currentAction);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (running) {
            performCast();
        }
        lastCycleEnd = std::chrono::steady_clock::now();
        return;
    }

    startDeferredBucketTracking(castCycleId_, eventTime);
    currentAction = "Resting";
    updateStatus(currentAction);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(restTime * 1000)));

    if (running) {
        performCast();
    }

    lastCycleEnd = std::chrono::steady_clock::now();
}

void AutoFishingApp::fishPickup(const std::string& line) {
    if (!running || currentAction != "Reeling") {
        return;
    }

    auto eventTime = extractLogTimestamp(line);
    if (eventTime) {
        auto waitStartWithTolerance = waitHookStartedWallAt_ - std::chrono::milliseconds(200);
        if (*eventTime < waitStartWithTolerance) {
            return;
        }
    }

    fishPickupDetectedAt_ = std::chrono::steady_clock::now();
    fishPickupDetected_ = true;
}

void AutoFishingApp::bucketSave() {
}

void AutoFishingApp::startFishing() {
    if (!running) {
        toggle();
    }
}

void AutoFishingApp::stopFishing() {
    if (running) {
        toggle();
    }
}

void AutoFishingApp::restartFishing() {
    std::thread([this]() {
        if (running) {
            stopFishing();
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(FishingConfig::RESTART_WAIT_TIME * 1000)));
        }
        startFishing();
    }).detach();
}

void AutoFishingApp::onTimer(WPARAM wParam) {
    // Timer handler if needed
}

void AutoFishingApp::registerHotkeys() {
    // Register Ctrl + F4 for toggle window visibility
    if (!RegisterHotKey(hwnd, ID_HOTKEY_TOGGLE_WINDOW, MOD_CONTROL, VK_F4)) {
        MessageBoxW(hwnd, L"Failed to register Toggle Window hotkey (Ctrl+F4)", L"Error", MB_OK | MB_ICONERROR);
    }
    // Register Ctrl + F5 for starting
    if (!RegisterHotKey(hwnd, ID_HOTKEY_START, MOD_CONTROL, VK_F5)) {
        MessageBoxW(hwnd, L"Failed to register Start hotkey (Ctrl+F5)", L"Error", MB_OK | MB_ICONERROR);
    }
    // Register Ctrl + F6 for stopping
    if (!RegisterHotKey(hwnd, ID_HOTKEY_STOP, MOD_CONTROL, VK_F6)) {
        MessageBoxW(hwnd, L"Failed to register Stop hotkey (Ctrl+F6)", L"Error", MB_OK | MB_ICONERROR);
    }
    // Register Ctrl + F7 for restarting
    if (!RegisterHotKey(hwnd, ID_HOTKEY_RESTART, MOD_CONTROL, VK_F7)) {
        MessageBoxW(hwnd, L"Failed to register Restart hotkey (Ctrl+F7)", L"Error", MB_OK | MB_ICONERROR);
    }
}

void AutoFishingApp::unregisterHotkeys() {
    UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE_WINDOW);
    UnregisterHotKey(hwnd, ID_HOTKEY_START);
    UnregisterHotKey(hwnd, ID_HOTKEY_STOP);
    UnregisterHotKey(hwnd, ID_HOTKEY_RESTART);
}

void AutoFishingApp::loadConfig() {
    std::ifstream configFile("config.json");
    if (!configFile.is_open()) {
        // If config doesn't exist, save current defaults
        saveConfig();
        return;
    }

    try {
        json config;
        configFile >> config;

        castTime = config.value("castTime", FishingConfig::DEFAULT_CAST_TIME);
        restTime = config.value("restTime", FishingConfig::DEFAULT_REST_TIME);
        timeoutLimit = config.value("timeoutLimit", FishingConfig::DEFAULT_TIMEOUT_MINUTES);
        randomCastEnabled = config.value("randomCastEnabled", false);
        randomCastMax = config.value("randomCastMax", 1.0);
        noCastMode = config.value("noCastMode", false);

        // Update UI elements
        SendMessage(hCastSlider, TBM_SETPOS, TRUE, static_cast<int>(castTime * 10));
        SendMessage(hRestSlider, TBM_SETPOS, TRUE, static_cast<int>(restTime * 10));
        SendMessage(hTimeoutSlider, TBM_SETPOS, TRUE, static_cast<int>(timeoutLimit * 10));
        SendMessage(hRandomMaxSlider, TBM_SETPOS, TRUE, static_cast<int>(randomCastMax * 10));
        
        SendMessage(hRandomCastCheck, BM_SETCHECK, randomCastEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(hNoCastCheckbox, BM_SETCHECK, noCastMode ? BST_CHECKED : BST_UNCHECKED, 0);
        
        EnableWindow(hRandomMaxSlider, randomCastEnabled);
        
        // Update visibility based on noCastMode
        int showCast = noCastMode ? SW_HIDE : SW_SHOW;
        ShowWindow(hCastTimeLabel, showCast);
        ShowWindow(hCastSlider, showCast);
        ShowWindow(hCastLabel, showCast);
        ShowWindow(hRandomCastCheck, showCast);
        ShowWindow(hRandomMaxTitleLabel, showCast);
        ShowWindow(hRandomMaxSlider, showCast);
        ShowWindow(hRandomMaxLabel, showCast);
        
        // Manually trigger update for labels
        onHScroll(0, (LPARAM)hCastSlider);
        onHScroll(0, (LPARAM)hRestSlider);
        onHScroll(0, (LPARAM)hTimeoutSlider);
        onHScroll(0, (LPARAM)hRandomMaxSlider);

    } catch (const json::parse_error& e) {
        (void)e; // Mark as unused to prevent warning
        MessageBoxW(hwnd, L"Failed to parse config.json. Using default settings.", L"Config Error", MB_OK | MB_ICONWARNING);
    }
}

void AutoFishingApp::saveConfig() {
    json config;
    config["castTime"] = castTime;
    config["restTime"] = restTime;
    config["timeoutLimit"] = timeoutLimit;
    config["randomCastEnabled"] = randomCastEnabled;
    config["randomCastMax"] = randomCastMax;
    config["noCastMode"] = noCastMode;

    std::ofstream configFile("config.json");
    if (configFile.is_open()) {
        configFile << config.dump(4); // Pretty print with 4 spaces
    } else {
        MessageBoxW(hwnd, L"Failed to save config.json.", L"Config Error", MB_OK | MB_ICONERROR);
    }
}

void AutoFishingApp::setupTrayIcon() {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = statusIcons["Waiting"];
    wcscpy_s(nid.szTip, getText("tray_tooltip").c_str());
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void AutoFishingApp::minimizeToTray() {
    ShowWindow(hwnd, SW_HIDE);
}

void AutoFishingApp::restoreFromTray() {
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

void AutoFishingApp::showTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, 1, L"Show");
    AppendMenu(hMenu, MF_STRING, 2, L"Exit");
    
    SetForegroundWindow(hwnd); // Necessary for the menu to disappear when clicking away
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0); // Necessary to properly close the menu
    
    DestroyMenu(hMenu);
}

HICON AutoFishingApp::createColoredIcon(COLORREF color) {
    const int ICON_WIDTH = 64;
    const int ICON_HEIGHT = 64;

    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);

    // Create color bitmap
    HBITMAP hColorBitmap = CreateCompatibleBitmap(hdc, ICON_WIDTH, ICON_HEIGHT);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hColorBitmap);

    // Fill background with white
    HBRUSH hBackBrush = CreateSolidBrush(RGB(255, 255, 255));
    RECT rect = { 0, 0, ICON_WIDTH, ICON_HEIGHT };
    FillRect(memDC, &rect, hBackBrush);
    DeleteObject(hBackBrush);

    // Draw colored circle with black border
    int padding = 6;
    
    // First draw the colored fill
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 4, RGB(0, 0, 0)); // 4-pixel thick black border
    HGDIOBJ hOldBrush = SelectObject(memDC, hBrush);
    HGDIOBJ hOldPen = SelectObject(memDC, hPen);

    Ellipse(memDC, padding, padding, ICON_WIDTH - padding, ICON_HEIGHT - padding);

    SelectObject(memDC, hOldBrush);
    SelectObject(memDC, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
    SelectObject(memDC, hOldBitmap);

    // Create mask bitmap (white background, black circle)
    HDC maskDC = CreateCompatibleDC(hdc);
    HBITMAP hMaskBitmap = CreateCompatibleBitmap(hdc, ICON_WIDTH, ICON_HEIGHT);
    HBITMAP hOldMaskBitmap = (HBITMAP)SelectObject(maskDC, hMaskBitmap);

    // Fill mask with white (transparent area)
    HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(maskDC, &rect, hWhiteBrush);

    // Draw black circle (opaque area)
    HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
    SelectObject(maskDC, hBlackBrush);
    SelectObject(maskDC, hNullPen);
    
    Ellipse(maskDC, padding, padding, ICON_WIDTH - padding, ICON_HEIGHT - padding);
    
    DeleteObject(hNullPen);
    SelectObject(maskDC, hOldMaskBitmap);

    ICONINFO iconInfo = { TRUE, 0, 0, hMaskBitmap, hColorBitmap };
    HICON hIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hColorBitmap);
    DeleteObject(hMaskBitmap);
    DeleteDC(memDC);
    DeleteDC(maskDC);
    ReleaseDC(NULL, hdc);

    return hIcon;
}
