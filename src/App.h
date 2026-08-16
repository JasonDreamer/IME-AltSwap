#pragma once

#include "InputHookHost.h"
#include "ImeSwitchCoordinator.h"

#include <Windows.h>
#include <shellapi.h>

class App final {
public:
    explicit App(HINSTANCE instance) noexcept;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void AddTrayIcon();
    void RemoveTrayIcon() noexcept;
    void ShowTrayMenu();
    void InjectMenuCancellationKey() const noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HANDLE mutex_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    NOTIFYICONDATAW trayIcon_{};
    InputHookHost inputHookHost_;
    ImeSwitchCoordinator imeSwitchCoordinator_;
    ImeTarget leftAltTarget_{};
    ImeTarget rightAltTarget_{};
    bool enabled_ = true;
};
