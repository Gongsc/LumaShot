#pragma once
#include "ColorPipeline.h"
#include <LumaShot/Localization.h>

namespace lumashot {

class CalibrationWindow {
public:
    CalibrationWindow(HINSTANCE instance, CaptureFrameSet frames, Language language,
                      HdrCalibration calibration);
    [[nodiscard]] bool run();
    [[nodiscard]] HdrCalibration calibration() const noexcept { return calibration_; }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    CaptureFrameSet frames_;
    ImageF16 previewSource_;
    ImageBgra8 preview_;
    Language language_{};
    HdrCalibration initialCalibration_{};
    HdrCalibration calibration_{};
    HMONITOR uiMonitor_{};
    RECT panelRect_{};
    RECT resetRect_{};
    RECT outputSliderRect_{};
    RECT compressionSliderRect_{};
    RECT applyRect_{};
    RECT cancelRect_{};
    POINT dragStart_{};
    RECT dragInitialPanel_{};
    int hoveredControl_{-1};
    int pressedControl_{-1};
    bool draggingPanel_{};
    bool finished_{};
    bool accepted_{};
    HDC backBufferDc_{};
    HBITMAP backBufferBitmap_{};
    HGDIOBJ backBufferPrevious_{};
    int backBufferWidth_{};
    int backBufferHeight_{};

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    void layoutPanel(bool resetPosition);
    void updatePanelControls();
    [[nodiscard]] int controlAt(POINT point) const noexcept;
    void activateControl(int id);
    void setCalibrationFromPoint(int id, int x);
    [[nodiscard]] int calibrationValue(int id) const noexcept;
    void updatePreview();
    [[nodiscard]] bool ensureBackBuffer(HDC reference, int width, int height);
    void releaseBackBuffer() noexcept;
    void finish(bool accepted);
};

} // namespace lumashot
