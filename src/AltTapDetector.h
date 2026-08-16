#pragma once

#include <cstdint>

enum class AltTapAction {
    None,
    ImeOff,
    ImeOn,
};

class AltTapDetector final {
public:
    static constexpr std::uint32_t MaximumTapDurationMilliseconds = 500;

    AltTapAction OnKey(
        std::uint32_t virtualKey,
        bool isDown,
        std::uint32_t timestampMilliseconds,
        bool anotherKeyIsAlreadyDown = false);
    void CancelTapCandidates() noexcept;
    void Reset() noexcept;

private:
    bool leftDown_ = false;
    bool rightDown_ = false;
    bool leftCandidate_ = false;
    bool rightCandidate_ = false;
    std::uint32_t leftDownTimestamp_ = 0;
    std::uint32_t rightDownTimestamp_ = 0;
};
