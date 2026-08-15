#include "App.h"
#include <commctrl.h>
#include <winrt/base.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\LumaShot.SingleInstance");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(lumashot::App::WindowClassName, nullptr)) PostMessageW(existing, lumashot::App::ActivateMessage, 0, 0);
        CloseHandle(mutex); return 0;
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS}; InitCommonControlsEx(&controls);
    const int result = lumashot::App(instance).run();
    CloseHandle(mutex); return result;
}
