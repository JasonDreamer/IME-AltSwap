#include "App.h"

#include "AppMessages.h"
#include "ImeController.h"
#include "StartupManager.h"
#include "resource.h"

#include <array>
#include <cwchar>

namespace {
constexpr wchar_t kWindowClassName[] = L"IMEAltSwap.HiddenWindow";
constexpr wchar_t kMutexName[] = L"Local\\IMEAltSwap.Singleton";
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kMenuEnabled = 1001;
constexpr UINT kMenuStartup = 1002;
constexpr UINT kMenuExit = 1003;
constexpr ULONG_PTR kInjectedInputMarker = static_cast<ULONG_PTR>(0x494D45414C545357ULL);
}  // namespace

App::App(const HINSTANCE instance) noexcept : instance_(instance) {}

App::~App() {
    inputHookHost_.Stop();
    imeSwitchCoordinator_.Stop();
    RemoveTrayIcon();
    if (window_ != nullptr && IsWindow(window_) != FALSE) {
        DestroyWindow(window_);
    }
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
    }
}

bool App::Initialize() {
    mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex_ == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        return false;
    }

    StartupManager::EnableOnFirstRun();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"IME AltSwap",
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();

    if (!imeSwitchCoordinator_.Start()) {
        return false;
    }
    return inputHookHost_.Start(window_);
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(creation->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app != nullptr
        ? app->HandleWindowMessage(window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleWindowMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        AddTrayIcon();
        return 0;
    }

    switch (message) {
    case kCancelAltMenuMessage:
        InjectMenuCancellationKey();
        return 0;

    case kDismissAltNavigationMessage:
        DismissAltNavigation();
        return 0;

    case kImeSwitchRequestMessage: {
        const HWND target = reinterpret_cast<HWND>(lParam);
        imeSwitchCoordinator_.Request({target, target}, wParam != 0);
        return 0;
    }

    case kTrayCallbackMessage:
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kMenuEnabled:
            enabled_ = !enabled_;
            inputHookHost_.SetEnabled(enabled_);
            return 0;
        case kMenuStartup:
            StartupManager::SetEnabled(!StartupManager::IsEnabled());
            return 0;
        case kMenuExit:
            DestroyWindow(window_);
            return 0;
        default:
            break;
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void App::AddTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = window_;
    trayIcon_.uID = kTrayIconId;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayIcon_.uCallbackMessage = kTrayCallbackMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"IME AltSwap - 左Alt: 英数 / 右Alt: かな");
    Shell_NotifyIconW(NIM_ADD, &trayIcon_);

    trayIcon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_);
}

void App::RemoveTrayIcon() noexcept {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void App::ShowTrayMenu() {
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING | (enabled_ ? MF_CHECKED : MF_UNCHECKED), kMenuEnabled, L"有効");
    AppendMenuW(
        menu,
        MF_STRING | (StartupManager::IsEnabled() ? MF_CHECKED : MF_UNCHECKED),
        kMenuStartup,
        L"Windows 起動時に開始");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"終了");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void App::InjectMenuCancellationKey() const noexcept {
    std::array<INPUT, 2> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = 0x07;
    input[0].ki.dwExtraInfo = kInjectedInputMarker;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT));
}

void App::DismissAltNavigation() const noexcept {
    std::array<INPUT, 2> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_ESCAPE;
    input[0].ki.dwExtraInfo = kInjectedInputMarker;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT));
}
