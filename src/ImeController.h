#pragma once

#include <Windows.h>

struct ImeTarget final {
    HWND foreground = nullptr;
    HWND focused = nullptr;
};

class ImeController final {
public:
    static ImeTarget CaptureTarget() noexcept;
    static bool IsTargetCurrent(const ImeTarget& target) noexcept;
    static bool SetOpenStatus(const ImeTarget& target, bool open) noexcept;
};
