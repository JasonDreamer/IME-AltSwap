#include "InputHookHost.h"

#include "AppMessages.h"

#include <array>
#include <cstddef>

namespace {
constexpr wchar_t kRawInputWindowClassName[] = L"IMEAltSwap.InputMonitorWindow";
constexpr UINT_PTR kHookRefreshTimerId = 1;
constexpr UINT kHookRefreshIntervalMilliseconds = 30'000;
constexpr UINT kResetInputStateMessage = WM_APP + 1;
constexpr std::uint32_t kLeftAlt = VK_LMENU;
constexpr std::uint32_t kRightAlt = VK_RMENU;
constexpr ULONG_PTR kInjectedInputMarker = static_cast<ULONG_PTR>(0x494D45414C545357ULL);
constexpr LONG kRawInputTimestampToleranceMilliseconds = 50;

bool IsKeyDownMessage(const WPARAM message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool IsKeyUpMessage(const WPARAM message) noexcept {
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

bool IsImeStateVirtualKey(const int virtualKey) noexcept {
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

std::uint32_t NormalizeAltKey(const KBDLLHOOKSTRUCT& key) noexcept {
    if (key.vkCode != VK_MENU) {
        return key.vkCode;
    }
    return (key.flags & LLKHF_EXTENDED) != 0 ? kRightAlt : kLeftAlt;
}

std::uint32_t NormalizeRawAltKey(const RAWKEYBOARD& key) noexcept {
    if (key.VKey != VK_MENU) {
        return key.VKey;
    }
    return (key.Flags & RI_KEY_E0) != 0 ? kRightAlt : kLeftAlt;
}

bool HookCoveredRawInput(const std::uint32_t hookTimestamp, const std::uint32_t rawTimestamp) noexcept {
    if (hookTimestamp == 0) {
        return false;
    }
    const LONG difference = static_cast<LONG>(hookTimestamp - rawTimestamp);
    return difference >= -kRawInputTimestampToleranceMilliseconds &&
        difference <= kRawInputTimestampToleranceMilliseconds;
}

}  // namespace

InputHookHost* InputHookHost::current_ = nullptr;

InputHookHost::~InputHookHost() {
    Stop();
}

bool InputHookHost::Start(const HWND notificationWindow) noexcept {
    if (notificationWindow == nullptr || thread_.joinable()) {
        return false;
    }

    notificationWindow_ = notificationWindow;
    startCompleted_ = false;
    startSucceeded_ = false;
    try {
        thread_ = std::thread(&InputHookHost::ThreadMain, this);
    } catch (...) {
        notificationWindow_ = nullptr;
        return false;
    }

    {
        std::unique_lock lock(startMutex_);
        startCondition_.wait(lock, [this] { return startCompleted_; });
    }
    if (!startSucceeded_) {
        thread_.join();
        notificationWindow_ = nullptr;
        return false;
    }
    return true;
}

void InputHookHost::Stop() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    const DWORD threadId = threadId_.load(std::memory_order_acquire);
    if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    thread_.join();
    notificationWindow_ = nullptr;
}

void InputHookHost::SetEnabled(const bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
    const DWORD threadId = threadId_.load(std::memory_order_acquire);
    if (threadId != 0) {
        PostThreadMessageW(threadId, kResetInputStateMessage, 0, 0);
    }
}

LRESULT CALLBACK InputHookHost::KeyboardProcedure(
    const int code,
    const WPARAM wParam,
    const LPARAM lParam) {
    return current_ != nullptr
        ? current_->HandleKeyboardMessage(code, wParam, lParam)
        : CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK InputHookHost::MouseProcedure(
    const int code,
    const WPARAM wParam,
    const LPARAM lParam) {
    return current_ != nullptr
        ? current_->HandleMouseMessage(code, wParam, lParam)
        : CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK InputHookHost::RawInputWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    InputHookHost* host = reinterpret_cast<InputHookHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        host = static_cast<InputHookHost*>(creation->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    return host != nullptr
        ? host->HandleRawInputWindowMessage(window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

void InputHookHost::ThreadMain() noexcept {
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);
    const bool initialized = InitializeThread();
    {
        std::lock_guard lock(startMutex_);
        startSucceeded_ = initialized;
        startCompleted_ = true;
    }
    startCondition_.notify_one();

    if (initialized) {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.hwnd == nullptr && message.message == kResetInputStateMessage) {
                ResetInputState();
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    UninitializeThread();
    threadId_.store(0, std::memory_order_release);
}

bool InputHookHost::InitializeThread() noexcept {
    module_ = GetModuleHandleW(nullptr);
    current_ = this;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = RawInputWindowProcedure;
    windowClass.hInstance = module_;
    windowClass.lpszClassName = kRawInputWindowClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    rawInputWindow_ = CreateWindowExW(
        0,
        kRawInputWindowClassName,
        L"IME AltSwap input monitor",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        module_,
        this);
    if (rawInputWindow_ == nullptr) {
        return false;
    }

    std::array<RAWINPUTDEVICE, 2> devices{};
    devices[0] = {0x01, 0x06, RIDEV_INPUTSINK, rawInputWindow_};
    devices[1] = {0x01, 0x02, RIDEV_INPUTSINK, rawInputWindow_};
    if (RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE)) == FALSE) {
        return false;
    }
    if (SetTimer(rawInputWindow_, kHookRefreshTimerId, kHookRefreshIntervalMilliseconds, nullptr) == 0) {
        return false;
    }

    if (!InstallInitialHooks()) {
        return false;
    }
    InitializeKeyState();
    return true;
}

void InputHookHost::UninitializeThread() noexcept {
    RemoveHooks();
    current_ = nullptr;

    if (rawInputWindow_ != nullptr) {
        KillTimer(rawInputWindow_, kHookRefreshTimerId);
        std::array<RAWINPUTDEVICE, 2> devices{};
        devices[0] = {0x01, 0x06, RIDEV_REMOVE, nullptr};
        devices[1] = {0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE));
        DestroyWindow(rawInputWindow_);
        rawInputWindow_ = nullptr;
    }
}

bool InputHookHost::InstallInitialHooks() noexcept {
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, module_, 0);
    if (keyboardHook_ == nullptr) {
        return false;
    }
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseProcedure, module_, 0);
    if (mouseHook_ == nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
        return false;
    }
    return true;
}

void InputHookHost::RefreshHooks() noexcept {
    const HHOOK replacementKeyboard = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, module_, 0);
    const HHOOK replacementMouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProcedure, module_, 0);
    if (replacementKeyboard == nullptr || replacementMouse == nullptr) {
        if (replacementKeyboard != nullptr) {
            UnhookWindowsHookEx(replacementKeyboard);
        }
        if (replacementMouse != nullptr) {
            UnhookWindowsHookEx(replacementMouse);
        }
        return;
    }

    const HHOOK previousKeyboard = keyboardHook_;
    const HHOOK previousMouse = mouseHook_;
    keyboardHook_ = replacementKeyboard;
    mouseHook_ = replacementMouse;
    if (previousKeyboard != nullptr) {
        UnhookWindowsHookEx(previousKeyboard);
    }
    if (previousMouse != nullptr) {
        UnhookWindowsHookEx(previousMouse);
    }
}

void InputHookHost::RemoveHooks() noexcept {
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_ != nullptr) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
}

LRESULT InputHookHost::HandleKeyboardMessage(
    const int code,
    const WPARAM wParam,
    const LPARAM lParam) noexcept {
    if (code < 0 || (!IsKeyDownMessage(wParam) && !IsKeyUpMessage(wParam))) {
        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }

    const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const std::uint32_t virtualKey = NormalizeAltKey(key);
    const bool isDown = IsKeyDownMessage(wParam);
    recentKeyboardHookEvents_[nextKeyboardHookEvent_] = {key.time, virtualKey, isDown};
    nextKeyboardHookEvent_ = (nextKeyboardHookEvent_ + 1) % recentKeyboardHookEvents_.size();
    if (key.dwExtraInfo != kInjectedInputMarker) {
        ProcessKeyboardEvent(
            virtualKey,
            isDown,
            key.time);
    }
    return CallNextHookEx(keyboardHook_, code, wParam, lParam);
}

LRESULT InputHookHost::HandleMouseMessage(
    const int code,
    const WPARAM wParam,
    const LPARAM lParam) noexcept {
    if (code >= 0) {
        const auto& mouse = *reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        lastMouseHookTimestamp_ = mouse.time;
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            ProcessMouseActivity();
            break;
        default:
            break;
        }
    }
    return CallNextHookEx(mouseHook_, code, wParam, lParam);
}

LRESULT InputHookHost::HandleRawInputWindowMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) noexcept {
    switch (message) {
    case WM_INPUT:
        HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        break;
    case WM_TIMER:
        if (wParam == kHookRefreshTimerId) {
            RefreshHooks();
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void InputHookHost::HandleRawInput(const HRAWINPUT rawInputHandle) noexcept {
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(rawInputHandle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == UINT_MAX) {
        return;
    }

    const std::uint32_t timestamp = static_cast<std::uint32_t>(GetMessageTime());
    if (input.header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& keyboard = input.data.keyboard;
        if (keyboard.VKey != 0xFF) {
            const std::uint32_t virtualKey = NormalizeRawAltKey(keyboard);
            const bool isDown = (keyboard.Flags & RI_KEY_BREAK) == 0;
            for (const RecentKeyboardHookEvent& hookEvent : recentKeyboardHookEvents_) {
                if (hookEvent.virtualKey == virtualKey &&
                    hookEvent.isDown == isDown &&
                    HookCoveredRawInput(hookEvent.timestamp, timestamp)) {
                    return;
                }
            }
            RefreshHooks();
            ProcessKeyboardEvent(
                virtualKey,
                isDown,
                timestamp);
        }
        return;
    }

    if (input.header.dwType == RIM_TYPEMOUSE) {
        if (HookCoveredRawInput(lastMouseHookTimestamp_, timestamp)) {
            return;
        }
        RefreshHooks();
        lastMouseHookTimestamp_ = timestamp;
        const USHORT buttons = input.data.mouse.usButtonFlags;
        if ((buttons & (RI_MOUSE_LEFT_BUTTON_DOWN |
                        RI_MOUSE_RIGHT_BUTTON_DOWN |
                        RI_MOUSE_MIDDLE_BUTTON_DOWN |
                        RI_MOUSE_BUTTON_4_DOWN |
                        RI_MOUSE_BUTTON_5_DOWN |
                        RI_MOUSE_WHEEL |
                        RI_MOUSE_HWHEEL)) != 0) {
            ProcessMouseActivity();
        }
    }
}

void InputHookHost::ProcessKeyboardEvent(
    const std::uint32_t virtualKey,
    const bool isDown,
    const std::uint32_t timestampMilliseconds) noexcept {
    if (virtualKey == 0x07 || virtualKey >= keyDown_.size()) {
        return;
    }

    const bool wasDown = keyDown_[virtualKey];
    keyDown_[virtualKey] = isDown;
    if (!enabled_.load(std::memory_order_acquire)) {
        return;
    }

    const bool isInitialAltDown = isDown && !wasDown &&
        (virtualKey == kLeftAlt || virtualKey == kRightAlt);
    const bool anotherKeyIsAlreadyDown = isInitialAltDown && IsAnotherKeyDown();
    const AltTapAction action = detector_.OnKey(
        virtualKey,
        isDown,
        timestampMilliseconds,
        anotherKeyIsAlreadyDown);

    if (isInitialAltDown) {
        PostMessageW(
            notificationWindow_,
            kCancelAltMenuMessage,
            virtualKey == kRightAlt ? 1 : 0,
            0);
        Sleep(5);
    }
    if (action == AltTapAction::ImeOff) {
        RequestImeChange(false);
    } else if (action == AltTapAction::ImeOn) {
        keyDown_[VK_CONTROL] = false;
        keyDown_[VK_LCONTROL] = false;
        keyDown_[VK_RCONTROL] = false;
        RequestImeChange(true);
    }
}

void InputHookHost::ProcessMouseActivity() noexcept {
    if (enabled_.load(std::memory_order_acquire)) {
        detector_.CancelTapCandidates();
    }
}

void InputHookHost::InitializeKeyState() noexcept {
    keyDown_.fill(false);
}

void InputHookHost::ResetInputState() noexcept {
    detector_.Reset();
    InitializeKeyState();
}

bool InputHookHost::IsAnotherKeyDown() const noexcept {
    for (int virtualKey = VK_BACK; virtualKey <= 0xFE; ++virtualKey) {
        if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
            continue;
        }
        if (IsImeStateVirtualKey(virtualKey)) {
            continue;
        }
        if (keyDown_[virtualKey]) {
            return true;
        }
    }
    return false;
}

void InputHookHost::RequestImeChange(const bool open) const noexcept {
    PostMessageW(
        notificationWindow_,
        kImeSwitchRequestMessage,
        static_cast<WPARAM>(open),
        0);
}
