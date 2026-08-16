#include "App.h"
#include <commctrl.h>
#include <shlobj_core.h>
#include <string>
#include <winrt/base.h>

namespace {
std::wstring ExecutableIdentity(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) return path;
    ULARGE_INTEGER modified{}, size{};
    modified.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    modified.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    size.HighPart = attributes.nFileSizeHigh;
    size.LowPart = attributes.nFileSizeLow;
    return std::wstring(path) + L"|" + std::to_wstring(modified.QuadPart) + L"|" + std::to_wstring(size.QuadPart);
}

bool IsNewShellIconIdentity(const std::wstring& identity) noexcept {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LumaShot", 0, nullptr, 0,
                        KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return true;
    wchar_t stored[512]{};
    DWORD bytes = sizeof(stored);
    const LSTATUS status = RegGetValueW(key, nullptr, L"ShellIconIdentity", RRF_RT_REG_SZ,
                                        nullptr, stored, &bytes);
    RegCloseKey(key);
    return status != ERROR_SUCCESS || identity != stored;
}

void RememberShellIconIdentity(const std::wstring& identity) noexcept {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LumaShot", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    const DWORD bytes = static_cast<DWORD>((identity.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, L"ShellIconIdentity", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(identity.c_str()), bytes);
    RegCloseKey(key);
}

void RefreshExecutableIcon() noexcept {
    wchar_t path[MAX_PATH]{};
    constexpr DWORD pathCapacity = static_cast<DWORD>(ARRAYSIZE(path));
    const DWORD length = GetModuleFileNameW(nullptr, path, pathCapacity);
    if (length > 0 && length < pathCapacity) {
        const std::wstring identity = ExecutableIdentity(path);
        if (IsNewShellIconIdentity(identity)) {
            // A replaced executable can retain its previous icon at the same path.
            // Invalidate the Shell image cache once per binary, then refresh both
            // the item and its already-open Explorer folder synchronously.
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_DWORD | SHCNF_FLUSH, nullptr, nullptr);
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, path, nullptr);
            wchar_t directory[MAX_PATH]{};
            wcscpy_s(directory, path);
            if (wchar_t* separator = wcsrchr(directory, L'\\')) {
                *separator = L'\0';
                SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW | SHCNF_FLUSH, directory, nullptr);
            }
            RememberShellIconIdentity(identity);
        } else {
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, path, nullptr);
        }
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RefreshExecutableIcon();
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
