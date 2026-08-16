#include "ImeController.h"

#include <Windows.h>
#include <imm.h>

#include <array>
#include <cstdint>

namespace {
constexpr UINT kMessageTimeoutMilliseconds = 50;
constexpr ULONG_PTR kInjectedInputMarker = static_cast<ULONG_PTR>(0x494D45414C545357ULL);
constexpr WPARAM kImeGetOpenStatus = 0x0005;
constexpr WPARAM kImeSetOpenStatus = 0x0006;

ImeTarget CaptureCurrentTarget() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return {};
    }

    const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO information{};
    information.cbSize = sizeof(information);
    if (threadId != 0 && GetGUIThreadInfo(threadId, &information) != FALSE) {
        if (information.hwndFocus != nullptr) {
            return {foreground, information.hwndFocus};
        }
        if (information.hwndActive != nullptr) {
            return {foreground, information.hwndActive};
        }
    }

    return {foreground, foreground};
}

bool SetWithImm32(const HWND target, const bool open) noexcept {
    if (target == nullptr) {
        return false;
    }

    const HWND imeWindow = ImmGetDefaultIMEWnd(target);
    if (imeWindow == nullptr) {
        return false;
    }

    DWORD_PTR ignoredResult = 0;
    const LRESULT sent = SendMessageTimeoutW(
        imeWindow,
        WM_IME_CONTROL,
        kImeSetOpenStatus,
        static_cast<LPARAM>(open),
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        kMessageTimeoutMilliseconds,
        &ignoredResult);
    if (sent == 0) {
        return false;
    }

    DWORD_PTR currentState = 0;
    const LRESULT queried = SendMessageTimeoutW(
        imeWindow,
        WM_IME_CONTROL,
        kImeGetOpenStatus,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        kMessageTimeoutMilliseconds,
        &currentState);
    return queried != 0 && (currentState != 0) == open;
}

bool SetWithExplicitImeKey(const bool open) noexcept {
    std::array<INPUT, 2> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = static_cast<WORD>(open ? VK_IME_ON : VK_IME_OFF);
    input[0].ki.dwExtraInfo = kInjectedInputMarker;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT)) == input.size();
}
}  // namespace

ImeTarget ImeController::CaptureTarget() noexcept {
    return CaptureCurrentTarget();
}

bool ImeController::IsTargetCurrent(const ImeTarget& target) noexcept {
    return target.foreground != nullptr &&
        IsWindow(target.foreground) != FALSE &&
        GetForegroundWindow() == target.foreground;
}

bool ImeController::SetOpenStatus(const ImeTarget& target, const bool open) noexcept {
    if (!IsTargetCurrent(target)) {
        return false;
    }

    const ImeTarget current = CaptureCurrentTarget();
    if (current.foreground != target.foreground || current.focused == nullptr) {
        return false;
    }
    if (SetWithImm32(current.focused, open)) {
        return true;
    }
    if (!IsTargetCurrent(target)) {
        return false;
    }
    return SetWithExplicitImeKey(open);
}
