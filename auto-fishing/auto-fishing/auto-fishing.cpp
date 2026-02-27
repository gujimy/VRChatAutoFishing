
#include "framework.h"
#include "auto-fishing.h"
#include "AutoFishingApp.h"
#include <iostream>

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
AutoFishingApp* g_pApp = nullptr;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_AUTOFISHING, szWindowClass, MAX_LOADSTRING);
    
    // Check for multiple instances using a named mutex
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"VRChat_AutoFishing_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Another instance is already running
        // Find and activate the existing window using the correct window class name
        HWND hExistingWnd = FindWindowW(szWindowClass, NULL);
        if (hExistingWnd)
        {
            // If window is minimized or hidden in tray, restore it
            if (IsIconic(hExistingWnd))
            {
                ShowWindow(hExistingWnd, SW_RESTORE);
            }
            else if (!IsWindowVisible(hExistingWnd))
            {
                ShowWindow(hExistingWnd, SW_SHOW);
            }
            // Bring window to foreground
            SetForegroundWindow(hExistingWnd);
        }
        if (hMutex)
        {
            CloseHandle(hMutex);
        }
        return FALSE;
    }
    MyRegisterClass(hInstance);

    if (!InitInstance (hInstance, nCmdShow))
    {
        if (hMutex)
        {
            CloseHandle(hMutex);
        }
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AUTOFISHING));

    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Clean up mutex before exiting
    if (hMutex)
    {
        CloseHandle(hMutex);
    }
    
    return (int) msg.wParam;
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AUTOFISHING));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_AUTOFISHING);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance;
   std::string version = FishingConfig::VERSION;
   std::wstring wVersion(version.begin(), version.end());
   std::wstring windowTitle = L"VRChat Auto Fishing v" + wVersion;

   HWND hWnd = CreateWindowW(szWindowClass, windowTitle.c_str(),
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, 0, 480, 560, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   INITCOMMONCONTROLSEX icex;
   icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
   icex.dwICC = ICC_BAR_CLASSES;
   InitCommonControlsEx(&icex);

   g_pApp = new AutoFishingApp(hWnd);

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDOWN) {
            if (g_pApp) g_pApp->restoreFromTray();
        } else if (lParam == WM_RBUTTONDOWN) {
            if (g_pApp) g_pApp->showTrayMenu();
        }
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            case 1: // From tray menu "Show"
                if (g_pApp) g_pApp->restoreFromTray();
                break;
            case 2: // From tray menu "Exit"
                DestroyWindow(hWnd);
                break;
            default:
                if (g_pApp) {
                    g_pApp->onCommand(wParam, lParam);
                }
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_HSCROLL:
        if (g_pApp) {
            g_pApp->onHScroll(wParam, lParam);
        }
        break;
    case WM_HOTKEY:
        if (g_pApp) {
            switch (wParam) {
            case ID_HOTKEY_TOGGLE_WINDOW:
                // Toggle window visibility
                if (IsWindowVisible(hWnd)) {
                    g_pApp->minimizeToTray();
                } else {
                    g_pApp->restoreFromTray();
                }
                break;
            case ID_HOTKEY_START:
                g_pApp->startFishing();
                break;
            case ID_HOTKEY_STOP:
                g_pApp->stopFishing();
                break;
            case ID_HOTKEY_RESTART:
                g_pApp->restartFishing();
                break;
            }
        }
        break;
    case WM_TIMER:
        if (g_pApp) {
            g_pApp->onTimer(wParam);
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_SYSCOMMAND:
        if (wParam == SC_MINIMIZE) {
            if (g_pApp) g_pApp->minimizeToTray();
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_CLOSE:
        if (g_pApp) g_pApp->minimizeToTray();
        return 0; // Prevent window from closing
    case WM_DESTROY:
        if (g_pApp) {
            delete g_pApp;
            g_pApp = nullptr;
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
