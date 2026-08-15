#include "App.h"
#include "AnnotationRenderer.h"
#include "CaptureService.h"
#include "ClipboardService.h"
#include "ColorPipeline.h"
#include "ImageExporter.h"
#include "IconFactory.h"
#include "OverlayWindow.h"
#include "SettingsWindow.h"
#include "resource.h"
#include <LumaShot/Localization.h>
#include <algorithm>
#include <chrono>
#include <shellapi.h>
#include <filesystem>
#include <utility>
#include <winrt/base.h>

namespace lumashot {
namespace {
constexpr int MenuCapture = 100;
constexpr int MenuRegion = 101;
constexpr int MenuWindow = 102;
constexpr int MenuMonitor = 103;
constexpr int MenuAll = 104;
constexpr int MenuSettings = 105;
constexpr int MenuExit = 106;

bool IsStartupConfigured() noexcept {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type{}, size{};
    const bool exists = RegQueryValueExW(key, L"LumaShot", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
    RegCloseKey(key);
    return exists;
}
}

App::App(HINSTANCE instance) : instance_(instance), settings_(settingsStore_.load()), language_(ResolveLanguage(settings_.language)) {
    settings_.launchAtLogin = IsStartupConfigured();
}

App::~App() {
    if (captureThread_.joinable()) captureThread_.request_stop();
    if (hwnd_) UnregisterHotKey(hwnd_, HotkeyId);
    removeTrayIcon();
    if (appIcon_ && appIcon_ != LoadIconW(nullptr, IDI_APPLICATION)) DestroyIcon(appIcon_);
}

int App::run() {
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance = instance_; cls.lpfnWndProc = WindowProc; cls.lpszClassName = WindowClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_LUMASHOT)); cls.hIconSm = cls.hIcon; RegisterClassExW(&cls);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, WindowClassName, L"LumaShot", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!hwnd_) return 1;
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    addTrayIcon(); registerHotkey();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT App::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == ActivateMessage) { beginCapture(settings_.lastCaptureMode); return 0; }
    switch (message) {
    case WM_HOTKEY: if (wparam == HotkeyId) beginCapture(settings_.lastCaptureMode); return 0;
    case TrayMessage:
        if (LOWORD(lparam) == WM_LBUTTONDBLCLK) beginCapture(settings_.lastCaptureMode);
        else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) { POINT point{}; GetCursorPos(&point); showTrayMenu(point); }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case MenuCapture: beginCapture(settings_.lastCaptureMode); break; case MenuRegion: beginCapture(CaptureMode::Region); break;
        case MenuWindow: beginCapture(CaptureMode::Window); break; case MenuMonitor: beginCapture(CaptureMode::Monitor); break;
        case MenuAll: beginCapture(CaptureMode::VirtualDesktop); break; case MenuSettings: showSettings(); break;
        case MenuExit: DestroyWindow(hwnd_); break;
        }
        return 0;
    case CaptureReadyMessage: finishCapture(std::unique_ptr<CapturePayload>(reinterpret_cast<CapturePayload*>(lparam))); return 0;
    case WM_ENDSESSION: if (wparam) DestroyWindow(hwnd_); return 0;
    case WM_DESTROY: removeTrayIcon(); PostQuitMessage(0); hwnd_ = nullptr; return 0;
    default: return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void App::addTrayIcon() {
    tray_ = {}; tray_.cbSize = sizeof(tray_); tray_.hWnd = hwnd_; tray_.uID = TrayId;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; tray_.uCallbackMessage = TrayMessage;
    appIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_LUMASHOT), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!appIcon_) appIcon_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
    tray_.hIcon = appIcon_; wcscpy_s(tray_.szTip, L"LumaShot");
    Shell_NotifyIconW(NIM_ADD, &tray_); tray_.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray_);
}

void App::removeTrayIcon() noexcept { if (tray_.hWnd) { Shell_NotifyIconW(NIM_DELETE, &tray_); tray_.hWnd = nullptr; } }

void App::showTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const auto add = [&](UINT flags, UINT id, StringId string) { const auto text = Localized(string, language_); AppendMenuW(menu, flags, id, text.data()); };
    add(MF_STRING | MF_DEFAULT, MenuCapture, StringId::Capture); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(MF_STRING, MenuRegion, StringId::Region); add(MF_STRING, MenuWindow, StringId::Window);
    add(MF_STRING, MenuMonitor, StringId::Monitor); add(MF_STRING, MenuAll, StringId::VirtualDesktop);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); add(MF_STRING, MenuSettings, StringId::Settings); add(MF_STRING, MenuExit, StringId::Exit);
    SetForegroundWindow(hwnd_); TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void App::notify(StringId title, StringId body, DWORD flags) noexcept {
    tray_.uFlags = NIF_INFO; tray_.dwInfoFlags = flags;
    const auto titleText = Localized(title, language_), bodyText = Localized(body, language_);
    wcsncpy_s(tray_.szInfoTitle, titleText.data(), _TRUNCATE); wcsncpy_s(tray_.szInfo, bodyText.data(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tray_); tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void App::registerHotkey() {
    UnregisterHotKey(hwnd_, HotkeyId);
    if (settings_.hotkey.virtualKey == 0 || !RegisterHotKey(hwnd_, HotkeyId, settings_.hotkey.modifiers, settings_.hotkey.virtualKey))
        notify(StringId::AppName, StringId::HotkeyUnavailable, NIIF_WARNING);
}

void App::beginCapture(CaptureMode mode) {
    if (capturing_.exchange(true)) return;
    settings_.lastCaptureMode = mode; (void)settingsStore_.save(settings_);
    const bool includeCursor = settings_.includeCursor;
    if (captureThread_.joinable()) captureThread_.join();
    const HWND target = hwnd_;
    captureThread_ = std::jthread([target, includeCursor](std::stop_token stop) {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto payload = std::make_unique<CapturePayload>();
        try {
            if (stop.stop_requested()) return;
            for (int attempt = 0; attempt < 2 && !payload->frames; ++attempt) {
                try {
                    CaptureService service;
                    payload->frames = std::make_unique<CaptureFrameSet>(service.captureDesktop(includeCursor));
                } catch (...) {
                    if (attempt != 0) throw;
                }
            }
        } catch (const std::exception& error) {
            const int size = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
            if (size > 1) { payload->error.resize(static_cast<std::size_t>(size)); MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, payload->error.data(), size); payload->error.pop_back(); }
        } catch (...) { payload->error = L"Unknown capture error"; }
        if (!stop.stop_requested() && PostMessageW(target, CaptureReadyMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
}

void App::finishCapture(std::unique_ptr<CapturePayload> payload) {
    capturing_ = false;
    if (!payload || !payload->frames) { notify(StringId::AppName, CaptureService::IsSupported() ? StringId::CaptureFailed : StringId::Unsupported, NIIF_ERROR); return; }
    OverlayWindow overlay(instance_, std::move(*payload->frames), settings_.lastCaptureMode, language_,
                          settings_.includeCursor, settings_.hdrCalibration);
    overlay.run([this](HWND owner, OverlayAction action, const CaptureFrameSet& frames, RectI selection,
                       const AnnotationDocument& annotations, HdrCalibration calibration) {
        return exportCapture(owner, action, frames, selection, annotations, calibration);
    });
    settings_.lastCaptureMode = overlay.mode();
    settings_.hdrCalibration = overlay.calibration();
    (void)settingsStore_.save(settings_);
}

bool App::exportCapture(HWND owner, OverlayAction action, const CaptureFrameSet& frames, RectI selection,
                        const AnnotationDocument& annotations, HdrCalibration calibration) {
    try {
        ImageF16 source = ColorPipeline::compose(frames, selection);
        if (source.width == 0 || source.height == 0) throw std::runtime_error("Empty selection");
        if (action == OverlayAction::Copy) {
            ImageBgra8 image = ColorPipeline::toneMapToSdr(source, calibration); AnnotationRenderer::renderSdr(image, annotations);
            if (!ClipboardService::copy(owner, image)) { MessageBoxW(owner, Localized(StringId::ClipboardFailed, language_).data(), L"LumaShot", MB_OK | MB_ICONERROR); return false; }
            notify(StringId::AppName, StringId::Copied); return true;
        }
        const auto choice = ImageExporter::showSaveDialog(owner, source.hdr, language_);
        if (!choice) return false;
        if (choice->format == ExportFormat::JpegXrHdr) { AnnotationRenderer::renderHdr(source, annotations); ImageExporter::saveJxr(choice->path, source); }
        else { ImageBgra8 image = ColorPipeline::toneMapToSdr(source, calibration); AnnotationRenderer::renderSdr(image, annotations); ImageExporter::savePng(choice->path, image); }
        notify(StringId::AppName, StringId::Saved); return true;
    } catch (...) {
        const auto message = action == OverlayAction::Copy ? StringId::ClipboardFailed : StringId::SaveFailed;
        MessageBoxW(owner, Localized(message, language_).data(), L"LumaShot", MB_OK | MB_ICONERROR); return false;
    }
}

void App::showSettings() {
    if (settingsWindow_) {
        settingsWindow_->activate();
        return;
    }

    ImageF16 preview;
    try {
        POINT cursor{};
        GetCursorPos(&cursor);
        const HMONITOR target = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        CaptureService service;
        const auto frames = service.captureDesktop(false, std::chrono::milliseconds(1800));
        const auto found = std::find_if(frames.monitors.begin(), frames.monitors.end(),
                                        [target](const MonitorFrame& frame) { return frame.monitor == target; });
        if (found != frames.monitors.end() && found->hdrEnabled) {
            preview = ColorPipeline::thumbnail(ColorPipeline::compose(frames, found->desktopRect), 480, 270);
        }
    } catch (...) {
    }
    SettingsWindow window(instance_, hwnd_, settings_, std::move(preview));
    settingsWindow_ = &window;
    struct SettingsWindowGuard {
        SettingsWindow*& slot;
        ~SettingsWindowGuard() { slot = nullptr; }
    } guard{settingsWindow_};

    const bool accepted = window.run();
    settingsWindow_ = nullptr;
    if (!accepted) return;
    settings_ = window.settings(); language_ = ResolveLanguage(settings_.language);
    (void)settingsStore_.save(settings_); applyStartupSetting(); registerHotkey();
}

void App::applyStartupSetting() noexcept {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (settings_.launchAtLogin) {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        const std::wstring command = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, L"LumaShot", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(key, L"LumaShot");
    RegCloseKey(key);
}

} // namespace lumashot
