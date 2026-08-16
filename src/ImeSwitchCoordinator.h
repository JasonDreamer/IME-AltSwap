#pragma once

#include "ImeController.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class ImeSwitchCoordinator final {
public:
    ImeSwitchCoordinator() = default;
    ~ImeSwitchCoordinator();

    ImeSwitchCoordinator(const ImeSwitchCoordinator&) = delete;
    ImeSwitchCoordinator& operator=(const ImeSwitchCoordinator&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    void Request(const ImeTarget& target, bool open) noexcept;

private:
    struct SwitchRequest final {
        ImeTarget target;
        bool open = false;
    };

    void WorkerLoop() noexcept;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<SwitchRequest> pending_;
    std::thread worker_;
    bool stopping_ = false;
};
