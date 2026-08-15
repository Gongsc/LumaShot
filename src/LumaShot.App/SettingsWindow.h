#pragma once
#include "ColorPipeline.h"
#include <commctrl.h>

namespace lumashot {
class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HWND owner, AppSettings settings, ImageF16 previewSource = {});
    [[nodiscard]] bool run();
    void activate() const noexcept;
    [[nodiscard]] const AppSettings& settings() const noexcept { return settings_; }

private:
    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    AppSettings settings_;
    ImageF16 previewSource_;
    ImageBgra8 calibrationPreview_;
    Language displayLanguage_{};
    UINT dpi_{96};
    bool dark_{};
    bool done_{};
    bool accepted_{};
    int hoveredAction_{-1};
    int hoveredInput_{-1};
    int hoveredSlider_{-1};
    int draggingSlider_{-1};
    HFONT titleFont_{};
    HFONT bodyFont_{};
    HFONT captionFont_{};
    HBRUSH backgroundBrush_{};
    HBRUSH cardBrush_{};
    HBRUSH inputBrush_{};

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK ActionButtonProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK InputControlProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK CalibrationSliderProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void createControls();
    void layoutControls();
    void paint();
    void drawActionButton(const DRAWITEMSTRUCT& item);
    void drawComboItem(const DRAWITEMSTRUCT& item);
    void drawInputControl(HWND control, int id, HDC target = nullptr);
    void drawCalibrationSlider(HWND control, int id, HDC target = nullptr);
    void setCalibrationFromPoint(HWND control, int id, int x);
    void adjustCalibration(int id, int delta);
    void updateCalibrationPreview();
    void drawCalibrationPreview(HDC dc, const RECT& bounds);
    [[nodiscard]] int calibrationValue(int id) const noexcept;
    void refreshTheme();
    void createUiResources();
    void releaseUiResources() noexcept;
    void readControls();
    [[nodiscard]] int scale(int value) const noexcept;
};
} // namespace lumashot
