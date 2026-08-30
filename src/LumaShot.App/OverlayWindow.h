#pragma once
#include "ColorPipeline.h"
#include <LumaShot/AnnotationDocument.h>
#include <LumaShot/Localization.h>
#include <functional>
#include <optional>

namespace lumashot {

enum class OverlayAction { Copy, Save };
using CommitHandler = std::function<bool(HWND, OverlayAction, const CaptureFrameSet&, RectI,
                                         const AnnotationDocument&, HdrCalibration)>;

class OverlayWindow {
public:
    OverlayWindow(HINSTANCE instance, CaptureFrameSet frames, CaptureMode mode, Language language,
                  bool includeCursor, bool copyOnEnter, HdrCalibration calibration);
    bool run(const CommitHandler& commit);
    [[nodiscard]] CaptureMode mode() const noexcept { return mode_; }
    [[nodiscard]] HdrCalibration calibration() const noexcept { return calibration_; }

private:
    enum class DragKind { None, NewSelection, Move, Left, Top, Right, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Drawing, MovingText };
    struct Button { RECT rect{}; int id{}; StringId label{}; };

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND textEditor_{};
    HWND textFontCombo_{};
    HWND textSizeCombo_{};
    HWND tooltip_{};
    HFONT textEditorFont_{};
    HFONT textControlsFont_{};
    HBRUSH textEditorBrush_{};
    RECT textEditorFrame_{};
    CaptureFrameSet frames_;
    ImageF16 previewSource_;
    ImageBgra8 preview_;
    ImageBgra8 dimmedPreview_;
    ImageBgra8 maskedPreview_;
    RectI maskedSelectionPixels_{};
    CaptureMode mode_;
    Language language_;
    bool includeCursor_{};
    bool copyOnEnter_{};
    HdrCalibration calibration_;
    RectI selection_{};
    RectI initialSelection_{};
    POINT dragStart_{};
    DragKind dragKind_{DragKind::None};
    AnnotationTool tool_{AnnotationTool::None};
    std::size_t colorIndex_{};
    std::size_t lineWidthIndex_{1};
    AnnotationDocument annotations_;
    std::optional<Annotation> draft_;
    PointF textOrigin_{};
    std::optional<std::size_t> hoveredTextIndex_;
    std::optional<std::size_t> movingTextIndex_;
    std::optional<TextAnnotation> movingText_;
    PointF movingTextInitialOrigin_{};
    std::wstring textFontFamily_{L"Segoe UI Variable Text"};
    int textFontSize_{20};
    HWND hoveredWindow_{};
    HMONITOR uiMonitor_{};
    bool selectionLocked_{};
    int hoveredButton_{-1};
    int pressedButton_{-1};
    bool finished_{};
    bool succeeded_{};
    CommitHandler commit_;
    std::vector<Button> modeButtons_;
    std::vector<Button> toolButtons_;
    HDC backBufferDc_{};
    HBITMAP backBufferBitmap_{};
    HGDIOBJ backBufferPrevious_{};
    int backBufferWidth_{};
    int backBufferHeight_{};
    POINT mouseClient_{};
    bool mouseInside_{};
    UINT uiDpi_{96};

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);
    static LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                       UINT_PTR id, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void createControls();
    void drawButton(const DRAWITEMSTRUCT& item);
    void paint();
    [[nodiscard]] bool ensureBackBuffer(HDC reference, int width, int height);
    void releaseBackBuffer() noexcept;
    void updateCalibrationPreview();
    void updateMaskedPreview();
    [[nodiscard]] bool magnifierVisible() const noexcept;
    [[nodiscard]] RECT magnifierRect(POINT clientPoint, const RECT& client) const noexcept;
    void drawMagnifier(HDC dc, const RECT& client) const;
    void rebuildButtons();
    void updateUiDpi();
    [[nodiscard]] int buttonAt(POINT clientPoint) const noexcept;
    void activateButton(int id);
    void setMode(CaptureMode mode);
    void updateWindowHover(POINT clientPoint);
    [[nodiscard]] bool lockHoveredWindow();
    [[nodiscard]] HWND windowAt(POINT screenPoint) const;
    [[nodiscard]] DragKind hitSelection(POINT screenPoint) const noexcept;
    [[nodiscard]] HCURSOR cursorForPoint(POINT clientPoint) const noexcept;
    [[nodiscard]] static HCURSOR cursorForDrag(DragKind dragKind) noexcept;
    [[nodiscard]] PointF relativePoint(POINT screenPoint) const noexcept;
    [[nodiscard]] POINT desktopFromClient(POINT clientPoint) const noexcept;
    [[nodiscard]] POINT clientFromDesktop(POINT desktopPoint) const noexcept;
    void beginDrawing(POINT screenPoint);
    void updateDrawing(POINT screenPoint);
    void finishDrawing();
    void beginText(POINT screenPoint);
    void commitText(bool cancel);
    void updateTextEditorFont();
    [[nodiscard]] RectI textBounds(const TextAnnotation& text) const;
    [[nodiscard]] std::optional<std::size_t> textAt(POINT screenPoint) const;
    void beginMovingText(std::size_t index, POINT screenPoint);
    void updateMovingText(POINT screenPoint);
    void finishMovingText(bool cancel);
    bool perform(OverlayAction action);
    [[nodiscard]] int scale(int value) const noexcept;
};

} // namespace lumashot
