#pragma once
#include <LumaShot/Types.h>
#include <commctrl.h>

namespace lumashot {
class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HWND owner, AppSettings settings);
    [[nodiscard]] bool run();
    void activate() const noexcept;
    [[nodiscard]] const AppSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] bool calibrationRequested() const noexcept { return calibrationRequested_; }

private:
    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    AppSettings settings_;
    Language displayLanguage_{};
    UINT dpi_{96};
    bool dark_{};
    bool highContrast_{};
    bool done_{};
    bool accepted_{};
    bool calibrationRequested_{};
    int hoveredAction_{-1};
    int hoveredInput_{-1};
    HFONT titleFont_{};
    HFONT bodyFont_{};
    HFONT captionFont_{};
    HBRUSH backgroundBrush_{};
    HBRUSH cardBrush_{};
    HBRUSH inputBrush_{};

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK ActionButtonProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK InputControlProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void createControls();
    void layoutControls();
    void paint();
    void drawActionButton(const DRAWITEMSTRUCT& item);
    void drawComboItem(const DRAWITEMSTRUCT& item);
    void drawInputControl(HWND control, int id, HDC target = nullptr);
    void refreshTheme();
    void createUiResources();
    void releaseUiResources() noexcept;
    void readControls();
    [[nodiscard]] int scale(int value) const noexcept;
};
} // namespace lumashot
