#include <Windows.h>
#include <imm.h>

#include <array>
#include <chrono>
#include <cstdint>

namespace {
constexpr UINT kImeGetOpenStatus = 0x0005;
constexpr UINT kImeSetOpenStatus = 0x0006;
constexpr ULONG_PTR kProbeInputMarker = static_cast<ULONG_PTR>(0x52554E50524F4245ULL);
WNDPROC gOriginalWindowProcedure = nullptr;
bool gEnterKeyDownDelivered = false;
bool gEnterWasSystemKey = false;
bool gAltChordDelivered = false;
bool gChordKeyDownDelivered = false;

LRESULT CALLBACK ProbeWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_RETURN) {
        gEnterKeyDownDelivered = true;
        gEnterWasSystemKey = message == WM_SYSKEYDOWN;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == 'F') {
        gChordKeyDownDelivered = true;
        gAltChordDelivered = message == WM_SYSKEYDOWN;
    }
    return CallWindowProcW(gOriginalWindowProcedure, window, message, wParam, lParam);
}

bool IsImeStateVirtualKey(const int virtualKey) {
    if (virtualKey >= 0xF0 && virtualKey <= 0xFD) {
        return true;
    }
    switch (virtualKey) {
    case VK_KANA:
    case VK_IME_ON:
    case VK_JUNJA:
    case VK_FINAL:
    case VK_KANJI:
    case VK_IME_OFF:
    case VK_CONVERT:
    case VK_NONCONVERT:
    case VK_ACCEPT:
    case VK_MODECHANGE:
    case VK_PROCESSKEY:
    case VK_PACKET:
        return true;
    default:
        return false;
    }
}

int FindAnotherKeyDown() {
    for (int virtualKey = VK_BACK; virtualKey <= 0xFE; ++virtualKey) {
        if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
            IsImeStateVirtualKey(virtualKey)) {
            continue;
        }
        if ((GetAsyncKeyState(virtualKey) & 0x8000) != 0) {
            return virtualKey;
        }
    }
    return 0;
}

void PumpMessages(const std::chrono::milliseconds duration) {
    const auto finish = std::chrono::steady_clock::now() + duration;
    MSG message{};
    while (std::chrono::steady_clock::now() < finish) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

bool ImeOpenStatus(const HWND window, bool& open) {
    const HWND imeWindow = ImmGetDefaultIMEWnd(window);
    if (imeWindow == nullptr) {
        return false;
    }

    DWORD_PTR state = 0;
    if (SendMessageTimeoutW(
            imeWindow,
            WM_IME_CONTROL,
            kImeGetOpenStatus,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            200,
            &state) == 0) {
        return false;
    }
    open = state != 0;
    return true;
}

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
               200,
               &ignored) != 0;
}

INPUT KeyInput(const WORD virtualKey, const bool keyUp, const bool extended = false) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = (keyUp ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    input.ki.dwExtraInfo = kProbeInputMarker;
    return input;
}

void ReleaseProbeModifiers() {
    std::array input{
        KeyInput(VK_LMENU, true),
        KeyInput(VK_RMENU, true, true),
        KeyInput(VK_MENU, true),
        KeyInput(VK_MENU, true, true),
        KeyInput(VK_CONTROL, true),
        KeyInput(VK_LCONTROL, true),
        KeyInput(VK_RCONTROL, true, true),
        KeyInput(VK_SHIFT, true),
        KeyInput(VK_LSHIFT, true),
        KeyInput(VK_RSHIFT, true),
        KeyInput('F', true),
        KeyInput(VK_RETURN, true),
    };
    SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT));
}

bool SendAltTap(
    const WORD altKey,
    const std::chrono::milliseconds holdDuration = std::chrono::milliseconds(40)) {
    const bool extended = altKey == VK_RMENU;
    INPUT input = KeyInput(altKey, false, extended);
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        return false;
    }
    PumpMessages(holdDuration);
    input = KeyInput(altKey, true, extended);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool SendModifiedAltTap(const WORD modifier, const WORD altKey) {
    INPUT modifierDown = KeyInput(modifier, false);
    if (SendInput(1, &modifierDown, sizeof(INPUT)) != 1) {
        return false;
    }
    PumpMessages(std::chrono::milliseconds(30));
    const bool tapSent = SendAltTap(altKey, std::chrono::milliseconds(40));
    INPUT modifierUp = KeyInput(modifier, true);
    const bool modifierReleased = SendInput(1, &modifierUp, sizeof(INPUT)) == 1;
    return tapSent && modifierReleased;
}

bool SendAltWithMouseClick(const HWND window) {
    RECT bounds{};
    if (GetWindowRect(window, &bounds) == FALSE ||
        SetCursorPos((bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2) == FALSE) {
        return false;
    }

    INPUT altDown = KeyInput(VK_LMENU, false);
    if (SendInput(1, &altDown, sizeof(INPUT)) != 1) {
        return false;
    }
    PumpMessages(std::chrono::milliseconds(40));

    std::array<INPUT, 2> mouse{};
    mouse[0].type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    mouse[0].mi.dwExtraInfo = kProbeInputMarker;
    mouse[1] = mouse[0];
    mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(static_cast<UINT>(mouse.size()), mouse.data(), sizeof(INPUT)) != mouse.size()) {
        INPUT altUp = KeyInput(VK_LMENU, true);
        SendInput(1, &altUp, sizeof(INPUT));
        return false;
    }
    PumpMessages(std::chrono::milliseconds(40));

    INPUT altUp = KeyInput(VK_LMENU, true);
    return SendInput(1, &altUp, sizeof(INPUT)) == 1;
}

bool SendAltChord() {
    INPUT altDown = KeyInput(VK_LMENU, false);
    if (SendInput(1, &altDown, sizeof(INPUT)) != 1) {
        return false;
    }
    PumpMessages(std::chrono::milliseconds(40));

    INPUT key = KeyInput('F', false);
    if (SendInput(1, &key, sizeof(INPUT)) != 1) {
        return false;
    }
    PumpMessages(std::chrono::milliseconds(40));
    std::array input{
        KeyInput('F', true),
        KeyInput(VK_LMENU, true),
    };
    return SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT)) == input.size();
}

bool FocusProbeWindow(const HWND window) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 360, 120, SWP_SHOWWINDOW);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        SetActiveWindow(window);
        SetFocus(window);
        PumpMessages(std::chrono::milliseconds(100));
        if (GetForegroundWindow() == window && GetFocus() == window) {
            return true;
        }
    }
    return false;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ReleaseProbeModifiers();
    PumpMessages(std::chrono::milliseconds(100));

    POINT originalCursor{};
    GetCursorPos(&originalCursor);
    const HKL originalLayout = GetKeyboardLayout(0);
    const HKL japaneseLayout = LoadKeyboardLayoutW(L"00000411", KLF_ACTIVATE);
    if (japaneseLayout == nullptr) {
        return 10;
    }

    const HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"EDIT",
        L"IME AltSwap runtime verification",
        WS_OVERLAPPEDWINDOW | ES_LEFT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        360,
        120,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        ActivateKeyboardLayout(originalLayout, 0);
        return 11;
    }
    gOriginalWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        window,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ProbeWindowProcedure)));
    if (gOriginalWindowProcedure == nullptr) {
        DestroyWindow(window);
        ActivateKeyboardLayout(originalLayout, 0);
        return 11;
    }

    int result = 0;
    bool originalOpen = false;
    if (!FocusProbeWindow(window) || !ImeOpenStatus(window, originalOpen)) {
        result = 12;
    }

    bool open = false;
    if (result == 0 && (!SetImeOpenStatus(window, true) || !SendAltTap(VK_LMENU))) {
        result = 20;
    }
    PumpMessages(std::chrono::milliseconds(300));
    if (result == 0 && (!ImeOpenStatus(window, open) || open)) {
        result = 21;
    }

    if (result == 0 && (!SetImeOpenStatus(window, false) || !SendAltTap(VK_RMENU))) {
        result = 30;
    }
    PumpMessages(std::chrono::milliseconds(300));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 31;
    }

    gAltChordDelivered = false;
    gChordKeyDownDelivered = false;
    if (result == 0 && (!SetImeOpenStatus(window, true) || !SendAltChord())) {
        result = 40;
    }
    PumpMessages(std::chrono::milliseconds(300));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 41;
    }
    if (result == 0 && !gChordKeyDownDelivered) {
        result = 42;
    } else if (result == 0 && !gAltChordDelivered) {
        result = 43;
    }

    if (result == 0 &&
        (!SetImeOpenStatus(window, true) ||
         !SendAltTap(VK_LMENU, std::chrono::milliseconds(650)))) {
        result = 50;
    }
    PumpMessages(std::chrono::milliseconds(100));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 51;
    }

    if (result == 0 &&
        (!SetImeOpenStatus(window, false) ||
         !SendAltTap(VK_RMENU, std::chrono::milliseconds(650)))) {
        result = 60;
    }
    PumpMessages(std::chrono::milliseconds(100));
    if (result == 0 && (!ImeOpenStatus(window, open) || open)) {
        result = 61;
    }

    if (result == 0 && (!SetImeOpenStatus(window, true) || !SendModifiedAltTap(VK_CONTROL, VK_LMENU))) {
        result = 70;
    }
    PumpMessages(std::chrono::milliseconds(100));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 71;
    }

    if (result == 0 && (!SetImeOpenStatus(window, true) || !SendAltWithMouseClick(window))) {
        result = 80;
    }
    PumpMessages(std::chrono::milliseconds(100));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 81;
    }

    if (result == 0 && !FocusProbeWindow(window)) {
        result = 82;
    }

    if (result == 0 &&
        (!SetImeOpenStatus(window, false) ||
         !SendAltTap(VK_LMENU, std::chrono::milliseconds(25)))) {
        result = 90;
    }
    const int rapidSequenceHeldKey = FindAnotherKeyDown();
    if (result == 0 && rapidSequenceHeldKey != 0) {
        result = 1000 + rapidSequenceHeldKey;
    }
    if (result == 0 && !SendAltTap(VK_RMENU, std::chrono::milliseconds(25))) {
        result = 90;
    }
    PumpMessages(std::chrono::milliseconds(350));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 91;
    }

    if (result == 0 &&
        (!SetImeOpenStatus(window, true) ||
         !SendAltTap(VK_RMENU, std::chrono::milliseconds(25)) ||
         !SendAltTap(VK_LMENU, std::chrono::milliseconds(25)))) {
        result = 100;
    }
    PumpMessages(std::chrono::milliseconds(350));
    if (result == 0 && (!ImeOpenStatus(window, open) || open)) {
        result = 101;
    }

    if (result == 0 && !SetImeOpenStatus(window, false)) {
        result = 110;
    }
    for (int repetition = 0; result == 0 && repetition < 6; ++repetition) {
        if (!SendAltTap(VK_LMENU, std::chrono::milliseconds(20)) ||
            !SendAltTap(VK_RMENU, std::chrono::milliseconds(20))) {
            result = 110;
        }
    }
    PumpMessages(std::chrono::milliseconds(350));
    if (result == 0 && (!ImeOpenStatus(window, open) || !open)) {
        result = 111;
    }

    if (result == 0 && !FocusProbeWindow(window)) {
        result = 120;
    }
    gEnterKeyDownDelivered = false;
    gEnterWasSystemKey = false;
    if (result == 0 &&
        (!SetImeOpenStatus(window, true) ||
         !SendAltTap(VK_LMENU, std::chrono::milliseconds(40)))) {
        result = 120;
    }
    std::array enterInput{
        KeyInput(VK_RETURN, false),
        KeyInput(VK_RETURN, true),
    };
    if (result == 0 &&
        SendInput(static_cast<UINT>(enterInput.size()), enterInput.data(), sizeof(INPUT)) != enterInput.size()) {
        result = 120;
    }
    PumpMessages(std::chrono::milliseconds(100));
    if (result == 0 && (!ImeOpenStatus(window, open) || open)) {
        result = 124;
    }
    if (result == 0 && (GetForegroundWindow() != window || GetFocus() != window)) {
        result = 126;
    }
    if (result == 0 && !gEnterKeyDownDelivered) {
        result = 122;
    } else if (result == 0 && gEnterWasSystemKey) {
        result = 123;
    }

    ReleaseProbeModifiers();
    PumpMessages(std::chrono::milliseconds(100));
    SetImeOpenStatus(window, originalOpen);
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalWindowProcedure));
    DestroyWindow(window);
    ActivateKeyboardLayout(originalLayout, 0);
    SetCursorPos(originalCursor.x, originalCursor.y);
    return result;
}
