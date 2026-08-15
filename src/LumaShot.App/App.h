#pragma once
#include <LumaShot/SettingsStore.h>
#include <LumaShot/Types.h>
#include <LumaShot/Localization.h>
#include <LumaShot/AnnotationDocument.h>
#include <shellapi.h>
#include <atomic>
#include <memory>
#include <thread>

namespace lumashot {

enum class OverlayAction;
class SettingsWindow;

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    int run();
    static constexpr wchar_t WindowClassName[] = L"LumaShot.MessageWindow";
    static constexpr UINT ActivateMessage = WM_APP + 72;

private:
    struct CapturePayload {
        std::unique_ptr<CaptureFrameSet> frames;
        std::wstring error;
        CaptureMode mode{CaptureMode::Region};
        bool calibration{};
    };
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
    SettingsWindow* settingsWindow_{};
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
    void beginCapture(CaptureMode mode, bool calibration);
    void beginCalibration();
    void finishCapture(std::unique_ptr<CapturePayload> payload);
    void showSettings();
    void applyStartupSetting() noexcept;
    bool exportCapture(HWND owner, OverlayAction action, const CaptureFrameSet& frames, RectI selection,
                       const AnnotationDocument& annotations, HdrCalibration calibration);
};

} // namespace lumashot
