#include "window_frame.h"

#include "eui_neo.h"

#include <algorithm>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

namespace imclash::frame {

#if defined(_WIN32)
namespace {

HWND gWindow = nullptr;
WNDPROC gPreviousProc = nullptr;
bool gHookInstalled = false;

HWND gTrayWindow = nullptr;
WNDPROC gTrayPreviousProc = nullptr;
bool gTrayHooked = false;
ULONGLONG gSuppressTrayUpUntil = 0;

float gTitleBarHeight = 36.0f;
float gControlsWidth = 36.0f;
float gLeftControlsWidth = 0.0f;
bool gTitleBarDragEnabled = true;

constexpr float kResizeBorder = 5.0f;
constexpr UINT kTrayCallbackMessage = WM_USER + 1;
constexpr WPARAM kTrayShowCommandId = 1000;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT_PTR kTrayHookTimer = 2;
constexpr UINT_PTR kTrayMenuTimer = 3;
constexpr UINT_PTR kStartupHideTimer = 4;

std::function<void()> gTickHandler;

UINT showMessageId() {
    static const UINT id = RegisterWindowMessageW(L"imclash-show");
    return id;
}

HWND nativeWindow() {
    if (gWindow != nullptr && IsWindow(gWindow)) return gWindow;
    auto* glfwWindow = static_cast<GLFWwindow*>(core::window::currentContextKey());
    if (glfwWindow == nullptr) return nullptr;
    gWindow = glfwGetWin32Window(glfwWindow);
    return gWindow;
}

float scaleFor(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

LRESULT CALLBACK trayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayCallbackMessage) {
        if (lParam == WM_LBUTTONDBLCLK) {
            KillTimer(hwnd, kTrayMenuTimer);
            gSuppressTrayUpUntil = GetTickCount64() + 400;
            showMainWindow();
            return 0;
        }
        if (lParam == WM_LBUTTONUP) {
            if (GetTickCount64() < gSuppressTrayUpUntil) return 0;
            KillTimer(hwnd, kTrayMenuTimer);
            const UINT delay = GetDoubleClickTime();
            SetTimer(hwnd, kTrayMenuTimer, delay > 0 ? delay : 400, nullptr);
            return 0;
        }
        if (lParam == WM_RBUTTONUP) {
            // The application is left-click only.
            return 0;
        }
    } else if (message == WM_TIMER && wParam == kTrayMenuTimer) {
        KillTimer(hwnd, kTrayMenuTimer);
        return CallWindowProcW(gTrayPreviousProc, hwnd, kTrayCallbackMessage, 0,
                               WM_LBUTTONUP);
    } else if (message == WM_COMMAND && wParam == kTrayShowCommandId) {
        // The EUI tray menu's Show item restores the hidden window directly;
        // center it first so an autostarted window does not return off-screen.
        centerWindow();
    }
    return CallWindowProcW(gTrayPreviousProc, hwnd, message, wParam, lParam);
}

void ensureTrayHook() {
    if (gTrayHooked) return;
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"TRAY", nullptr)) != nullptr) {
        DWORD processId = 0;
        GetWindowThreadProcessId(tray, &processId);
        if (processId == GetCurrentProcessId()) break;
    }
    if (tray == nullptr || !IsWindow(tray)) return;

    gTrayPreviousProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(tray, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(trayProc)));
    if (gTrayPreviousProc != nullptr) {
        gTrayWindow = tray;
        gTrayHooked = true;
        KillTimer(nativeWindow(), kTrayHookTimer);
    }
}

LRESULT CALLBACK borderlessProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == showMessageId()) {
        showMainWindow();
        return 0;
    }
    switch (message) {
        case WM_NCCALCSIZE:
            if (wParam == TRUE) return 0;
            break;

        case WM_NCHITTEST: {
            const LRESULT result =
                CallWindowProcW(gPreviousProc, hwnd, message, wParam, lParam);
            if (result != HTCLIENT) return result;

            RECT window{};
            if (!GetWindowRect(hwnd, &window)) return result;
            const float scale = scaleFor(hwnd);
            const int border = static_cast<int>(kResizeBorder * scale);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int left = x - window.left;
            const int top = y - window.top;
            const int right = window.right - x;
            const int bottom = window.bottom - y;

            const bool nearLeft = left < border;
            const bool nearRight = right < border;
            const bool nearTop = top < border;
            const bool nearBottom = bottom < border;
            if (nearTop && nearLeft) return HTTOPLEFT;
            if (nearTop && nearRight) return HTTOPRIGHT;
            if (nearBottom && nearLeft) return HTBOTTOMLEFT;
            if (nearBottom && nearRight) return HTBOTTOMRIGHT;
            if (nearLeft) return HTLEFT;
            if (nearRight) return HTRIGHT;
            if (nearTop) return HTTOP;
            if (nearBottom) return HTBOTTOM;

            const int titleHeight = static_cast<int>(gTitleBarHeight * scale);
            const int controlsStart =
                (window.right - window.left) - static_cast<int>(gControlsWidth * scale);
            const int leftControlsEnd = static_cast<int>(gLeftControlsWidth * scale);
            if (gTitleBarDragEnabled && top < titleHeight && left > leftControlsEnd &&
                left < controlsStart) {
                return HTCAPTION;
            }
            return result;
        }

        case WM_NCLBUTTONDBLCLK:
            // No maximize state: the title bar is only a drag band.
            if (SendMessageW(hwnd, WM_NCHITTEST, wParam, lParam) == HTCAPTION) {
                return 0;
            }
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MAXIMIZE) return 0;
            break;

        case WM_TIMER:
            if (wParam == kTrayHookTimer) {
                ensureTrayHook();
            } else if (wParam == kTickTimer) {
                if (gTickHandler) gTickHandler();
            } else if (wParam == kStartupHideTimer) {
                KillTimer(hwnd, kStartupHideTimer);
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_NCACTIVATE:
        case WM_NCPAINT:
            return TRUE;

        default:
            break;
    }
    return CallWindowProcW(gPreviousProc, hwnd, message, wParam, lParam);
}

} // namespace
#endif

void installBorderlessWindow(float titleBarLogicalHeight,
                             float leftControlsWidth,
                             float rightControlsWidth) {
    gTitleBarHeight = titleBarLogicalHeight > 0.0f ? titleBarLogicalHeight : 36.0f;
    gLeftControlsWidth = std::max(0.0f, leftControlsWidth);
    gControlsWidth = rightControlsWidth > 0.0f ? rightControlsWidth : 36.0f;
#if defined(_WIN32)
    HWND hwnd = nativeWindow();
    if (hwnd == nullptr) return;

    if (!gHookInstalled) {
        gPreviousProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(borderlessProc)));
        gHookInstalled = gPreviousProc != nullptr;
    }

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    // No native caption buttons: the app draws its own minimize control and
    // exits through the tray menu. WS_THICKFRAME keeps the resize frame and
    // DWM shadow; WM_NCCALCSIZE hides the frame itself.
    style |= WS_THICKFRAME | WS_CAPTION;
    style &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE);

    enum : DWORD { kCornerPreference = 33 };
    enum : DWORD { kCornerRound = 2 };
    const DWORD preference = kCornerRound;
    DwmSetWindowAttribute(hwnd, kCornerPreference, &preference, sizeof(preference));

    SetTimer(hwnd, kTickTimer, 1000, nullptr);
    SetTimer(hwnd, kTrayHookTimer, 150, nullptr);
#endif
}

void hideToTray() {
#if defined(_WIN32)
    if (HWND hwnd = nativeWindow()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
#endif
}

void centerWindow() {
#if defined(_WIN32)
    HWND hwnd = nativeWindow();
    if (hwnd == nullptr) return;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int x = info.rcWork.left + std::max(0, (workWidth - width) / 2);
    const int y = info.rcWork.top + std::max(0, (workHeight - height) / 2);
    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void prepareSilentStartup() {
#if defined(_WIN32)
    // EUI shows the main window after onStart. Park it far off-screen so the
    // initial show cannot flash; scheduleStartupHide then hides it to the tray.
    if (HWND hwnd = nativeWindow()) {
        SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

void showMainWindow() {
#if defined(_WIN32)
    HWND hwnd = nativeWindow();
    if (hwnd == nullptr) return;
    centerWindow();
    if (gTrayHooked && gTrayWindow != nullptr && IsWindow(gTrayWindow)) {
        PostMessageW(gTrayWindow, WM_COMMAND, kTrayShowCommandId, 0);
        SetForegroundWindow(hwnd);
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
#endif
}

void scheduleStartupHide() {
#if defined(_WIN32)
    if (HWND hwnd = nativeWindow()) SetTimer(hwnd, kStartupHideTimer, 700, nullptr);
#endif
}

void setTickHandler(std::function<void()> handler) {
    gTickHandler = std::move(handler);
}

void setTitleBarDragEnabled(bool enabled) {
    gTitleBarDragEnabled = enabled;
}

float dpiScale() {
#if defined(_WIN32)
    HWND hwnd = nativeWindow();
    return hwnd != nullptr ? scaleFor(hwnd) : 1.0f;
#else
    return 1.0f;
#endif
}

} // namespace imclash::frame
