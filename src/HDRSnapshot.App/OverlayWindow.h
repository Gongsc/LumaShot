#pragma once
#include "ColorPipeline.h"
#include <HDRSnapshot/AnnotationDocument.h>
#include <HDRSnapshot/Localization.h>
#include <functional>
#include <optional>

namespace hdrsnapshot {

enum class OverlayAction { Copy, Save };
using CommitHandler = std::function<bool(HWND, OverlayAction, const CaptureFrameSet&, RectI, const AnnotationDocument&)>;

class OverlayWindow {
public:
    OverlayWindow(HINSTANCE instance, CaptureFrameSet frames, CaptureMode mode, Language language, bool includeCursor);
    bool run(const CommitHandler& commit);
    [[nodiscard]] CaptureMode mode() const noexcept { return mode_; }

private:
    enum class DragKind { None, NewSelection, Move, Left, Top, Right, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Drawing };
    struct Button { RECT rect{}; int id{}; StringId label{}; };

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND textEditor_{};
    CaptureFrameSet frames_;
    ImageBgra8 preview_;
    CaptureMode mode_;
    Language language_;
    bool includeCursor_{};
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

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    void rebuildButtons();
    [[nodiscard]] int buttonAt(POINT clientPoint) const noexcept;
    void activateButton(int id);
    void setMode(CaptureMode mode);
    void updateWindowHover(POINT clientPoint);
    [[nodiscard]] HWND windowAt(POINT screenPoint) const;
    [[nodiscard]] DragKind hitSelection(POINT screenPoint) const noexcept;
    [[nodiscard]] PointF relativePoint(POINT screenPoint) const noexcept;
    void beginDrawing(POINT screenPoint);
    void updateDrawing(POINT screenPoint);
    void finishDrawing();
    void beginText(POINT screenPoint);
    void commitText(bool cancel);
    bool perform(OverlayAction action);
};

} // namespace hdrsnapshot
