#include <Windows.h>
#include <imm.h>

#include <cstdint>

namespace {
constexpr WPARAM kImeGetOpenStatus = 0x0005;
constexpr WPARAM kImeSetOpenStatus = 0x0006;
constexpr UINT kAltReleasedMessage = WM_APP + 1;
constexpr UINT_PTR kVerificationTimer = 1;
constexpr UINT_PTR kTimeoutTimer = 2;
constexpr ULONG_PTR kApplicationInputMarker = static_cast<ULONG_PTR>(0x494D45414C545357ULL);

enum class Stage {
    WaitForLeftAlt,
    VerifyLeftAlt,
    WaitForRightAlt,
    VerifyRightAlt,
};

HWND gWindow = nullptr;
HHOOK gKeyboardHook = nullptr;
WNDPROC gOriginalWindowProcedure = nullptr;
Stage gStage = Stage::WaitForLeftAlt;

bool SetImeOpenStatus(const HWND window, const bool open) {
    const HWND imeWindow = ImmGetDefaultIMEWnd(window);
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

bool ImeOpenStatus(const HWND window, bool& open) {
    const HWND imeWindow = ImmGetDefaultIMEWnd(window);
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

void Finish(const int result) {
    if (gKeyboardHook != nullptr) {
        UnhookWindowsHookEx(gKeyboardHook);
        gKeyboardHook = nullptr;
    }
    PostQuitMessage(result);
}

LRESULT CALLBACK ProbeWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    switch (message) {
    case kAltReleasedMessage:
        SetTimer(window, kVerificationTimer, 300, nullptr);
        return 0;

    case WM_TIMER: {
        if (wParam == kTimeoutTimer) {
            Finish(gStage == Stage::WaitForLeftAlt ? 50 : 51);
            return 0;
        }
        if (wParam != kVerificationTimer) {
            break;
        }
        KillTimer(window, kVerificationTimer);
        bool open = false;
        if (!ImeOpenStatus(window, open)) {
            Finish(20);
            return 0;
        }
        if (gStage == Stage::VerifyLeftAlt) {
            if (open) {
                Finish(30);
                return 0;
            }
            if (!SetImeOpenStatus(window, false)) {
                Finish(21);
                return 0;
            }
            gStage = Stage::WaitForRightAlt;
            SetWindowTextW(window, L"左Alt: 成功　次に右Altを単押ししてください");
            return 0;
        }
        if (gStage == Stage::VerifyRightAlt) {
            Finish(open ? 0 : 40);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        Finish(60);
        return 0;

    default:
        break;
    }
    return CallWindowProcW(gOriginalWindowProcedure, window, message, wParam, lParam);
}

LRESULT CALLBACK KeyboardProcedure(const int code, const WPARAM wParam, const LPARAM lParam) {
    if (code >= 0 && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
        const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (key.dwExtraInfo != kApplicationInputMarker) {
            const DWORD virtualKey = key.vkCode == VK_MENU
                ? ((key.flags & LLKHF_EXTENDED) != 0 ? VK_RMENU : VK_LMENU)
                : key.vkCode;
            if (gStage == Stage::WaitForLeftAlt && virtualKey == VK_LMENU) {
                gStage = Stage::VerifyLeftAlt;
                PostMessageW(gWindow, kAltReleasedMessage, 0, 0);
            } else if (gStage == Stage::WaitForRightAlt && virtualKey == VK_RMENU) {
                gStage = Stage::VerifyRightAlt;
                PostMessageW(gWindow, kAltReleasedMessage, 0, 0);
            }
        }
    }
    return CallNextHookEx(gKeyboardHook, code, wParam, lParam);
}
}  // namespace

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, int) {
    gWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"EDIT",
        L"左Altを単押ししてください",
        WS_OVERLAPPEDWINDOW | ES_LEFT | ES_READONLY,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        120,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (gWindow == nullptr) {
        return 10;
    }

    gOriginalWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        gWindow,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ProbeWindowProcedure)));
    if (gOriginalWindowProcedure == nullptr) {
        DestroyWindow(gWindow);
        return 11;
    }

    ShowWindow(gWindow, SW_SHOW);
    SetForegroundWindow(gWindow);
    SetFocus(gWindow);
    if (!SetImeOpenStatus(gWindow, true)) {
        DestroyWindow(gWindow);
        return 12;
    }
    bool initiallyOpen = false;
    if (!ImeOpenStatus(gWindow, initiallyOpen) || !initiallyOpen) {
        DestroyWindow(gWindow);
        return 14;
    }

    gKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, instance, 0);
    if (gKeyboardHook == nullptr) {
        DestroyWindow(gWindow);
        return 13;
    }
    SetTimer(gWindow, kTimeoutTimer, 10'000, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (gKeyboardHook != nullptr) {
        UnhookWindowsHookEx(gKeyboardHook);
    }
    if (IsWindow(gWindow) != FALSE) {
        SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalWindowProcedure));
        DestroyWindow(gWindow);
    }
    return static_cast<int>(message.wParam);
}
