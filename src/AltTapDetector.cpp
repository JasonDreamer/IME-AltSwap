#include "AltTapDetector.h"

#include <Windows.h>

AltTapAction AltTapDetector::OnKey(
    const std::uint32_t virtualKey,
    const bool isDown,
    const std::uint32_t timestampMilliseconds,
    const bool anotherKeyIsAlreadyDown) {
    if (virtualKey == VK_LMENU) {
        if (isDown) {
            if (!leftDown_) {
                leftDown_ = true;
                leftCandidate_ = !rightDown_ && !anotherKeyIsAlreadyDown;
                leftDownTimestamp_ = timestampMilliseconds;
                if (rightDown_) {
                    rightCandidate_ = false;
                }
            }
            return AltTapAction::None;
        }

        const std::uint32_t heldMilliseconds = timestampMilliseconds - leftDownTimestamp_;
        const bool wasTap = leftDown_ && leftCandidate_ &&
            heldMilliseconds <= MaximumTapDurationMilliseconds;
        leftDown_ = false;
        leftCandidate_ = false;
        return wasTap ? AltTapAction::ImeOff : AltTapAction::None;
    }

    if (virtualKey == VK_RMENU) {
        if (isDown) {
            if (!rightDown_) {
                rightDown_ = true;
                rightCandidate_ = !leftDown_ && !anotherKeyIsAlreadyDown;
                rightDownTimestamp_ = timestampMilliseconds;
                if (leftDown_) {
                    leftCandidate_ = false;
                }
            }
            return AltTapAction::None;
        }

        const std::uint32_t heldMilliseconds = timestampMilliseconds - rightDownTimestamp_;
        const bool wasTap = rightDown_ && rightCandidate_ &&
            heldMilliseconds <= MaximumTapDurationMilliseconds;
        rightDown_ = false;
        rightCandidate_ = false;
        return wasTap ? AltTapAction::ImeOn : AltTapAction::None;
    }

    if (isDown) {
        if (leftDown_) {
            leftCandidate_ = false;
        }
        if (rightDown_) {
            rightCandidate_ = false;
        }
    }

    return AltTapAction::None;
}

void AltTapDetector::CancelTapCandidates() noexcept {
    leftCandidate_ = false;
    rightCandidate_ = false;
}

void AltTapDetector::Reset() noexcept {
    leftDown_ = false;
    rightDown_ = false;
    leftCandidate_ = false;
    rightCandidate_ = false;
    leftDownTimestamp_ = 0;
    rightDownTimestamp_ = 0;
}
