#pragma once
#include <HDRSnapshot/SettingsStore.h>
#include <HDRSnapshot/Types.h>
#include <HDRSnapshot/Localization.h>
#include <HDRSnapshot/AnnotationDocument.h>
#include <shellapi.h>
#include <atomic>
#include <memory>
#include <thread>

namespace hdrsnapshot {

enum class OverlayAction;

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    int run();
    static constexpr wchar_t WindowClassName[] = L"HDRSnapshot.MessageWindow";
    static constexpr UINT ActivateMessage = WM_APP + 72;

private:
    struct CapturePayload { std::unique_ptr<CaptureFrameSet> frames; std::wstring error; };
    static constexpr UINT TrayMessage = WM_APP + 1;
    static constexpr UINT CaptureReadyMessage = WM_APP + 2;
    static constexpr UINT_PTR TrayId = 1;
    static constexpr int HotkeyId = 1;

    HINSTANCE instance_{};
    HWND hwnd_{};
    NOTIFYICONDATAW tray_{};
    HICON appIcon_{};
    SettingsStore settingsStore_;
    AppSettings settings_;
    Language language_{};
    std::atomic_bool capturing_{false};
    std::jthread captureThread_;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void addTrayIcon();
    void removeTrayIcon() noexcept;
    void showTrayMenu(POINT point);
    void notify(StringId title, StringId body, DWORD flags = NIIF_INFO) noexcept;
    void registerHotkey();
    void beginCapture(CaptureMode mode);
    void finishCapture(std::unique_ptr<CapturePayload> payload);
    void showSettings();
    void applyStartupSetting() noexcept;
    bool exportCapture(HWND owner, OverlayAction action, const CaptureFrameSet& frames, RectI selection, const AnnotationDocument& annotations);
};

} // namespace hdrsnapshot
