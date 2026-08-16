#pragma once

#include "AltTapDetector.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class InputHookHost final {
public:
    InputHookHost() = default;
    ~InputHookHost();

    InputHookHost(const InputHookHost&) = delete;
    InputHookHost& operator=(const InputHookHost&) = delete;

    bool Start(HWND notificationWindow) noexcept;
    void Stop() noexcept;
    void SetEnabled(bool enabled) noexcept;

private:
    static LRESULT CALLBACK KeyboardProcedure(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseProcedure(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RawInputWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void ThreadMain() noexcept;
    bool InitializeThread() noexcept;
    void UninitializeThread() noexcept;
    bool InstallInitialHooks() noexcept;
    void RefreshHooks() noexcept;
    void RemoveHooks() noexcept;
    LRESULT HandleKeyboardMessage(int code, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMouseMessage(int code, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleRawInputWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void HandleRawInput(HRAWINPUT rawInputHandle) noexcept;
    void ProcessKeyboardEvent(
        std::uint32_t virtualKey,
        bool isDown,
        std::uint32_t timestampMilliseconds) noexcept;
    void ProcessMouseActivity() noexcept;
    void InitializeKeyState() noexcept;
    void ResetInputState() noexcept;
    bool IsAnotherKeyDown() const noexcept;
    void RequestImeChange(HWND targetWindow, bool open) const noexcept;

    HWND notificationWindow_ = nullptr;
    HWND rawInputWindow_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HINSTANCE module_ = nullptr;
    std::atomic<DWORD> threadId_{0};
    std::thread thread_;
    std::mutex startMutex_;
    std::condition_variable startCondition_;
    bool startCompleted_ = false;
    bool startSucceeded_ = false;
    std::atomic_bool enabled_{true};
    AltTapDetector detector_;
    std::array<bool, 256> keyDown_{};
    HWND leftAltTarget_ = nullptr;
    HWND rightAltTarget_ = nullptr;
    std::uint32_t lastKeyboardHookTimestamp_ = 0;
    std::uint32_t lastMouseHookTimestamp_ = 0;

    static InputHookHost* current_;
};
