#include "AltTapDetector.h"

#include <Windows.h>

#include <cstdlib>
#include <iostream>

namespace {
void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}  // namespace

int main() {
    AltTapDetector detector;

    Require(detector.OnKey(VK_LMENU, true, 1000) == AltTapAction::None, "left Alt down");
    Require(detector.OnKey(VK_LMENU, false, 1100) == AltTapAction::ImeOff, "left Alt tap turns IME off");

    Require(detector.OnKey(VK_RMENU, true, 2000) == AltTapAction::None, "right Alt down");
    Require(detector.OnKey(VK_RMENU, false, 2100) == AltTapAction::ImeOn, "right Alt tap turns IME on");

    detector.OnKey(VK_LMENU, true, 3000);
    detector.OnKey('F', true, 3020);
    detector.OnKey('F', false, 3040);
    Require(detector.OnKey(VK_LMENU, false, 3060) == AltTapAction::None, "Alt+F remains a chord");

    detector.OnKey(VK_RMENU, true, 4000);
    detector.OnKey(VK_RMENU, true, 4300);
    Require(detector.OnKey(VK_RMENU, false, 4500) == AltTapAction::ImeOn, "Alt autorepeat does not reset tap time");

    detector.OnKey(VK_LMENU, true, 5000, true);
    Require(detector.OnKey(VK_LMENU, false, 5100) == AltTapAction::None, "pre-held key prevents a tap");

    detector.OnKey(VK_LMENU, true, 6000);
    detector.OnKey(VK_RMENU, true, 6020);
    Require(detector.OnKey(VK_LMENU, false, 6040) == AltTapAction::None, "two Alt keys are not left tap");
    Require(detector.OnKey(VK_RMENU, false, 6060) == AltTapAction::None, "two Alt keys are not right tap");

    detector.OnKey(VK_LMENU, true, 7000);
    detector.Reset();
    Require(detector.OnKey(VK_LMENU, false, 7100) == AltTapAction::None, "reset clears state");

    detector.OnKey(VK_LMENU, true, 8000);
    Require(detector.OnKey(VK_LMENU, false, 8501) == AltTapAction::None, "long left Alt hold is not a tap");

    detector.OnKey(VK_RMENU, true, 9000);
    detector.CancelTapCandidates();
    Require(detector.OnKey(VK_RMENU, false, 9100) == AltTapAction::None, "mouse use cancels Alt tap");

    detector.OnKey(VK_LMENU, true, 0xFFFFFF00u);
    Require(detector.OnKey(VK_LMENU, false, 0x00000020u) == AltTapAction::ImeOff, "timestamp wrap keeps short tap valid");

    detector.OnKey(VK_LMENU, true, 10000);
    Require(detector.OnKey(VK_LMENU, false, 10020) == AltTapAction::ImeOff, "rapid sequence accepts left tap");
    detector.OnKey(VK_RMENU, true, 10021);
    Require(detector.OnKey(VK_RMENU, false, 10041) == AltTapAction::ImeOn, "rapid sequence accepts right tap");

    std::cout << "All AltTapDetector tests passed.\n";
    return EXIT_SUCCESS;
}
