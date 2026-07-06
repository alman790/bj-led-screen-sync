#include "lib/platform/windows/app/WinApp.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WinApp app;
    return app.run(instance);
}
