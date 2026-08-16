#include "App.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    App app(instance);
    if (!app.Initialize()) {
        return 1;
    }
    return app.Run();
}
