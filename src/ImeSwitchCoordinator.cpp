#include "ImeSwitchCoordinator.h"

#include <array>
#include <chrono>

namespace {
constexpr std::array kRetryDelays{
    std::chrono::milliseconds(20),
    std::chrono::milliseconds(60),
    std::chrono::milliseconds(140),
};
}

ImeSwitchCoordinator::~ImeSwitchCoordinator() {
    Stop();
}

bool ImeSwitchCoordinator::Start() noexcept {
    std::lock_guard lock(mutex_);
    if (worker_.joinable()) {
        return true;
    }

    stopping_ = false;
    try {
        worker_ = std::thread(&ImeSwitchCoordinator::WorkerLoop, this);
    } catch (...) {
        return false;
    }
    return true;
}

void ImeSwitchCoordinator::Stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (!worker_.joinable()) {
            return;
        }
        stopping_ = true;
        pending_.reset();
    }
    condition_.notify_one();
    worker_.join();
}

void ImeSwitchCoordinator::Request(const ImeTarget& target, const bool open) noexcept {
    if (target.foreground == nullptr || target.focused == nullptr) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !worker_.joinable()) {
            return;
        }
        pending_ = SwitchRequest{target, open};
    }
    condition_.notify_one();
}

void ImeSwitchCoordinator::WorkerLoop() noexcept {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
        if (stopping_) {
            break;
        }

        SwitchRequest active = *pending_;
        pending_.reset();
        std::size_t attempt = 0;

        while (!stopping_) {
            lock.unlock();
            if (ImeController::IsTargetCurrent(active.target)) {
                ImeController::SetOpenStatus(active.target, active.open);
            }
            lock.lock();

            if (stopping_) {
                break;
            }
            if (pending_.has_value()) {
                active = *pending_;
                pending_.reset();
                attempt = 0;
                continue;
            }
            if (attempt >= kRetryDelays.size()) {
                break;
            }

            condition_.wait_for(
                lock,
                kRetryDelays[attempt++],
                [this] { return stopping_ || pending_.has_value(); });
            if (pending_.has_value()) {
                active = *pending_;
                pending_.reset();
                attempt = 0;
            }
        }
    }
}
