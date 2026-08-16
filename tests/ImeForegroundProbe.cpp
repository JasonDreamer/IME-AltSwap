#include <Windows.h>
#include <imm.h>

namespace {
constexpr wchar_t kWindowClassName[] = L"IMEAltSwap.ForegroundProbe";
constexpr WPARAM kImeGetOpenStatus = 0x0005;
constexpr WPARAM kImeSetOpenStatus = 0x0006;
constexpr UINT kVerifyMessage = WM_APP + 1;
constexpr UINT_PTR kVerificationTimer = 1;
constexpr UINT_PTR kTimeoutTimer = 2;
constexpr ULONG_PTR kApplicationInputMarker = static_cast<ULONG_PTR>(0x494D45414C545357ULL);

enum class Stage {
    WaitForLeftDown,
    WaitForLeftUp,
    VerifyLeft,
    WaitForRightDown,
    WaitForRightUp,
    VerifyRight,
};

HWND gMessageWindow = nullptr;
HWND gTargetWindow = nullptr;
HHOOK gKeyboardHook = nullptr;
Stage gStage = Stage::WaitForLeftDown;

DWORD NormalizeAltKey(const KBDLLHOOKSTRUCT& key) {
    if (key.vkCode != VK_MENU) {
        return key.vkCode;
    }
    return (key.flags & LLKHF_EXTENDED) != 0 ? VK_RMENU : VK_LMENU;
}

HWND FocusedWindow(const HWND foreground) {
    const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO information{};
    information.cbSize = sizeof(information);
    if (threadId != 0 && GetGUIThreadInfo(threadId, &information) != FALSE) {
        if (information.hwndFocus != nullptr) {
            return information.hwndFocus;
        }
        if (information.hwndActive != nullptr) {
            return information.hwndActive;
        }
    }
    return foreground;
}

bool ImeOpenStatus(const HWND foreground, bool& open) {
    if (foreground == nullptr || IsWindow(foreground) == FALSE) {
        return false;
    }
    const HWND imeWindow = ImmGetDefaultIMEWnd(FocusedWindow(foreground));
    if (imeWindow == nullptr) {
        return false;
    }
    DWORD_PTR status = 0;
    if (SendMessageTimeoutW(
            imeWindow,
            WM_IME_CONTROL,
            kImeGetOpenStatus,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            100,
            &status) == 0) {
        return false;
    }
    open = status != 0;
    return true;
}

bool SetImeOpenStatus(const HWND foreground, const bool open) {
    if (foreground == nullptr || IsWindow(foreground) == FALSE) {
        return false;
    }
    const HWND imeWindow = ImmGetDefaultIMEWnd(FocusedWindow(foreground));
    if (imeWindow == nullptr) {
        return false;
    }
    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               imeWindow,
               WM_IME_CONTROL,
               kImeSetOpenStatus,
               static_cast<LPARAM>(open),
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               100,
               &ignored) != 0;
}

void Finish(const int result) {
    if (gKeyboardHook != nullptr) {
        UnhookWindowsHookEx(gKeyboardHook);
        gKeyboardHook = nullptr;
    }
    PostQuitMessage(result);
}

LRESULT CALLBACK WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    switch (message) {
    case kVerifyMessage:
        SetTimer(window, kVerificationTimer, 400, nullptr);
        return 0;

    case WM_TIMER: {
        if (wParam == kTimeoutTimer) {
            Finish(gStage == Stage::WaitForLeftDown || gStage == Stage::WaitForLeftUp ? 50 : 51);
            return 0;
        }
        if (wParam != kVerificationTimer) {
            break;
        }
        KillTimer(window, kVerificationTimer);
        bool open = false;
        if (!ImeOpenStatus(gTargetWindow, open)) {
            Finish(20);
            return 0;
        }
        if (gStage == Stage::VerifyLeft) {
            if (open) {
                Finish(30);
                return 0;
            }
            gStage = Stage::WaitForRightDown;
            return 0;
        }
        if (gStage == Stage::VerifyRight) {
            Finish(open ? 0 : 40);
            return 0;
        }
        break;
    }

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK KeyboardProcedure(const int code, const WPARAM wParam, const LPARAM lParam) {
    if (code >= 0) {
        const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (key.dwExtraInfo != kApplicationInputMarker) {
            const DWORD virtualKey = NormalizeAltKey(key);
            if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) &&
                gStage == Stage::WaitForLeftDown && virtualKey == VK_LMENU) {
                gTargetWindow = GetForegroundWindow();
                gStage = Stage::WaitForLeftUp;
            } else if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) &&
                       gStage == Stage::WaitForLeftUp && virtualKey == VK_LMENU) {
                gStage = Stage::VerifyLeft;
                PostMessageW(gMessageWindow, kVerifyMessage, 0, 0);
            } else if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) &&
                       gStage == Stage::WaitForRightDown && virtualKey == VK_RMENU) {
                gTargetWindow = GetForegroundWindow();
                gStage = Stage::WaitForRightUp;
            } else if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) &&
                       gStage == Stage::WaitForRightUp && virtualKey == VK_RMENU) {
                gStage = Stage::VerifyRight;
                PostMessageW(gMessageWindow, kVerifyMessage, 0, 0);
            }
        }
    }
    return CallNextHookEx(gKeyboardHook, code, wParam, lParam);
}
}  // namespace

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return 10;
    }

    gMessageWindow = CreateWindowExW(
        0,
        kWindowClassName,
        L"IME AltSwap foreground probe",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr);
    if (gMessageWindow == nullptr) {
        return 11;
    }

    gTargetWindow = GetForegroundWindow();
    if (!SetImeOpenStatus(gTargetWindow, true)) {
        DestroyWindow(gMessageWindow);
        return 12;
    }
    bool initiallyOpen = false;
    if (!ImeOpenStatus(gTargetWindow, initiallyOpen) || !initiallyOpen) {
        DestroyWindow(gMessageWindow);
        return 14;
    }

    gKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, instance, 0);
    if (gKeyboardHook == nullptr) {
        DestroyWindow(gMessageWindow);
        return 13;
    }
    SetTimer(gMessageWindow, kTimeoutTimer, 15'000, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (gKeyboardHook != nullptr) {
        UnhookWindowsHookEx(gKeyboardHook);
    }
    DestroyWindow(gMessageWindow);
    return static_cast<int>(message.wParam);
}
