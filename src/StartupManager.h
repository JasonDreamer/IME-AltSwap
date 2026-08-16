#pragma once

class StartupManager final {
public:
    static bool IsEnabled() noexcept;
    static bool SetEnabled(bool enabled) noexcept;
    static void EnableOnFirstRun() noexcept;
};
