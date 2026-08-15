#include "CalibrationWindow.h"
#include "resource.h"
#include <LumaShot/Geometry.h>
#include <algorithm>
#include <utility>
#include <windowsx.h>

namespace lumashot {
namespace {
constexpr wchar_t ClassName[] = L"LumaShot.Calibration";
constexpr int PanelSurface = 400;
constexpr int OutputBrightness = 401;
constexpr int HighlightCompression = 402;
constexpr int Reset = 403;
constexpr int Apply = 404;
constexpr int Cancel = 405;

POINT PointFromLParam(LPARAM value) { return {GET_X_LPARAM(value), GET_Y_LPARAM(value)}; }
bool PointIn(const RECT& rect, POINT point) { return PtInRect(&rect, point) != FALSE; }

void FillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF outline) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void AlphaFill(HDC target, const RECT& rect, BYTE alpha) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HDC source = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, 1, 1);
    const auto old = SelectObject(source, bitmap);
    SetPixel(source, 0, 0, RGB(20, 20, 23));
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(target, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               source, 0, 0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(bitmap);
    DeleteDC(source);
}

void FillTranslucentPanel(HDC dc, const RECT& rect, BYTE alpha, COLORREF outline) {
    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, 22, 22);
    SelectClipRgn(dc, clip);
    AlphaFill(dc, rect, alpha);
    RestoreDC(dc, saved);
    DeleteObject(clip);
    HPEN border = CreatePen(PS_SOLID, 1, outline);
    const auto oldPen = SelectObject(dc, border);
    const auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 22, 22);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);
}
} // namespace

CalibrationWindow::CalibrationWindow(HINSTANCE instance, CaptureFrameSet frames, Language language,
                                     HdrCalibration calibration)
    : instance_(instance), frames_(std::move(frames)), language_(language),
      initialCalibration_(calibration), calibration_(calibration) {
    POINT cursor{};
    GetCursorPos(&cursor);
    uiMonitor_ = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    previewSource_ = ColorPipeline::compose(frames_, frames_.virtualDesktop);
    frames_.monitors.clear();
    frames_.monitors.shrink_to_fit();
    updatePreview();
}

bool CalibrationWindow::run() {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = ClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_LUMASHOT));
    RegisterClassExW(&cls);
    const RectI bounds = frames_.virtualDesktop;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName,
                            Localized(StringId::HdrCalibrationTitle, language_).data(), WS_POPUP,
                            bounds.left, bounds.top, bounds.width(), bounds.height(),
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    layoutPanel(true);
    MSG message{};
    while (!finished_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    return accepted_;
}

LRESULT CALLBACK CalibrationWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<CalibrationWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CalibrationWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CalibrationWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, hoveredControl_ == PanelSurface ? IDC_SIZEALL
                                         : hoveredControl_ >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE: {
        const POINT point = PointFromLParam(lparam);
        const int hovered = controlAt(point);
        if (hovered != hoveredControl_) {
            hoveredControl_ = hovered;
            InvalidateRect(hwnd_, &panelRect_, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tracking);
        if (draggingPanel_ && (wparam & MK_LBUTTON) != 0) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            panelRect_ = dragInitialPanel_;
            OffsetRect(&panelRect_, point.x - dragStart_.x, point.y - dragStart_.y);
            const int dx = panelRect_.left < 12 ? 12 - panelRect_.left
                         : panelRect_.right > client.right - 12 ? client.right - 12 - panelRect_.right : 0;
            const int dy = panelRect_.top < 12 ? 12 - panelRect_.top
                         : panelRect_.bottom > client.bottom - 12 ? client.bottom - 12 - panelRect_.bottom : 0;
            OffsetRect(&panelRect_, dx, dy);
            updatePanelControls();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if ((pressedControl_ == OutputBrightness || pressedControl_ == HighlightCompression) &&
            (wparam & MK_LBUTTON) != 0) {
            setCalibrationFromPoint(pressedControl_, point.x);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hoveredControl_ = -1;
        InvalidateRect(hwnd_, &panelRect_, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const POINT point = PointFromLParam(lparam);
        pressedControl_ = controlAt(point);
        if (pressedControl_ < 0) return 0;
        SetCapture(hwnd_);
        if (pressedControl_ == PanelSurface) {
            draggingPanel_ = true;
            dragStart_ = point;
            dragInitialPanel_ = panelRect_;
        } else if (pressedControl_ == OutputBrightness || pressedControl_ == HighlightCompression) {
            setCalibrationFromPoint(pressedControl_, point.x);
        }
        InvalidateRect(hwnd_, &panelRect_, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (pressedControl_ < 0) return 0;
        const int pressed = pressedControl_;
        const POINT point = PointFromLParam(lparam);
        if (pressed == OutputBrightness || pressed == HighlightCompression)
            setCalibrationFromPoint(pressed, point.x);
        const bool activate = !draggingPanel_ && controlAt(point) == pressed;
        pressedControl_ = -1;
        draggingPanel_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (activate && pressed != OutputBrightness && pressed != HighlightCompression)
            activateControl(pressed);
        if (hwnd_) InvalidateRect(hwnd_, &panelRect_, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        pressedControl_ = -1;
        draggingPanel_ = false;
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) finish(false);
        else if (wparam == VK_RETURN) finish(true);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_CLOSE:
        finish(false);
        return 0;
    case WM_DESTROY:
        releaseBackBuffer();
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void CalibrationWindow::layoutPanel(bool resetPosition) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    constexpr int panelWidth = 420;
    constexpr int panelHeight = 224;
    if (resetPosition || IsRectEmpty(&panelRect_)) {
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(uiMonitor_, &monitor)) monitor.rcWork = ToWin32Rect(frames_.virtualDesktop);
        const int monitorLeft = monitor.rcWork.left - frames_.virtualDesktop.left;
        const int monitorTop = monitor.rcWork.top - frames_.virtualDesktop.top;
        const int monitorRight = monitor.rcWork.right - frames_.virtualDesktop.left;
        const int monitorBottom = monitor.rcWork.bottom - frames_.virtualDesktop.top;
        const int x = std::clamp(monitorRight - panelWidth - 32, monitorLeft + 12,
                                 std::max(monitorLeft + 12, monitorRight - panelWidth - 12));
        const int y = std::clamp(monitorTop + (monitorBottom - monitorTop - panelHeight) / 2,
                                 monitorTop + 12, std::max(monitorTop + 12, monitorBottom - panelHeight - 12));
        panelRect_ = {x, y, x + panelWidth, y + panelHeight};
    } else {
        const int dx = panelRect_.left < 12 ? 12 - panelRect_.left
                     : panelRect_.right > client.right - 12 ? client.right - 12 - panelRect_.right : 0;
        const int dy = panelRect_.top < 12 ? 12 - panelRect_.top
                     : panelRect_.bottom > client.bottom - 12 ? client.bottom - 12 - panelRect_.bottom : 0;
        OffsetRect(&panelRect_, dx, dy);
    }
    updatePanelControls();
}

void CalibrationWindow::updatePanelControls() {
    const int left = panelRect_.left;
    const int top = panelRect_.top;
    resetRect_ = {panelRect_.right - 92, top + 12, panelRect_.right - 16, top + 42};
    outputSliderRect_ = {left + 20, top + 88, panelRect_.right - 20, top + 108};
    compressionSliderRect_ = {left + 20, top + 137, panelRect_.right - 20, top + 157};
    cancelRect_ = {panelRect_.right - 204, panelRect_.bottom - 48, panelRect_.right - 112, panelRect_.bottom - 14};
    applyRect_ = {panelRect_.right - 104, panelRect_.bottom - 48, panelRect_.right - 16, panelRect_.bottom - 14};
}

int CalibrationWindow::controlAt(POINT point) const noexcept {
    if (PointIn(outputSliderRect_, point)) return OutputBrightness;
    if (PointIn(compressionSliderRect_, point)) return HighlightCompression;
    if (PointIn(resetRect_, point)) return Reset;
    if (PointIn(applyRect_, point)) return Apply;
    if (PointIn(cancelRect_, point)) return Cancel;
    if (PointIn(panelRect_, point)) return PanelSurface;
    return -1;
}

void CalibrationWindow::activateControl(int id) {
    if (id == Reset) {
        calibration_ = {};
        updatePreview();
    } else if (id == Apply) {
        finish(true);
    } else if (id == Cancel) {
        finish(false);
    }
}

int CalibrationWindow::calibrationValue(int id) const noexcept {
    return id == OutputBrightness ? calibration_.outputBrightnessPercent
                                  : calibration_.highlightCompressionPercent;
}

void CalibrationWindow::setCalibrationFromPoint(int id, int x) {
    const RECT& slider = id == OutputBrightness ? outputSliderRect_ : compressionSliderRect_;
    const int start = slider.left;
    const int end = std::max(start + 1, static_cast<int>(slider.right));
    const int minimum = id == OutputBrightness ? HdrCalibration::MinimumOutputBrightness
                                                : HdrCalibration::MinimumHighlightCompression;
    const int maximum = id == OutputBrightness ? HdrCalibration::MaximumOutputBrightness
                                                : HdrCalibration::MaximumHighlightCompression;
    const int value = minimum + MulDiv(std::clamp(x, start, end) - start, maximum - minimum, end - start);
    if (value == calibrationValue(id)) return;
    if (id == OutputBrightness) calibration_.outputBrightnessPercent = value;
    else calibration_.highlightCompressionPercent = value;
    updatePreview();
}

void CalibrationWindow::updatePreview() {
    preview_ = ColorPipeline::toneMapToSdr(previewSource_, calibration_);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CalibrationWindow::paint() {
    PAINTSTRUCT ps{};
    HDC windowDc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const bool buffered = ensureBackBuffer(windowDc, client.right, client.bottom);
    HDC dc = buffered ? backBufferDc_ : windowDc;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(preview_.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(preview_.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, preview_.width, preview_.height,
                  preview_.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);

    layoutPanel(false);
    RECT shadow = panelRect_;
    OffsetRect(&shadow, 3, 5);
    FillTranslucentPanel(dc, shadow, 90, RGB(25, 25, 28));
    FillTranslucentPanel(dc, panelRect_, 205, RGB(105, 105, 112));
    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    HFONT bodyFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    const auto oldFont = SelectObject(dc, titleFont);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT title{panelRect_.left + 18, panelRect_.top + 10, resetRect_.left - 8, panelRect_.top + 39};
    const auto titleText = Localized(StringId::HdrCalibrationTitle, language_);
    DrawTextW(dc, titleText.data(), static_cast<int>(titleText.size()), &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, bodyFont);
    SetTextColor(dc, RGB(205, 205, 210));
    RECT hint{panelRect_.left + 18, panelRect_.top + 40, panelRect_.right - 18, panelRect_.top + 64};
    const auto hintText = Localized(StringId::CalibrationInstructions, language_);
    DrawTextW(dc, hintText.data(), static_cast<int>(hintText.size()), &hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const auto drawButton = [&](const RECT& rect, int id, StringId label, bool primary) {
        const bool hovered = hoveredControl_ == id;
        const bool pressed = pressedControl_ == id;
        const COLORREF fill = primary ? (pressed ? RGB(0, 82, 148) : hovered ? RGB(16, 110, 190) : RGB(0, 120, 212))
                                      : (pressed ? RGB(56, 56, 61) : hovered ? RGB(66, 66, 72) : RGB(43, 43, 48));
        FillRoundRect(dc, rect, 8, fill, primary ? fill : RGB(92, 92, 98));
        SetTextColor(dc, RGB(255, 255, 255));
        const auto text = Localized(label, language_);
        RECT textBounds = rect;
        DrawTextW(dc, text.data(), static_cast<int>(text.size()), &textBounds,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    drawButton(resetRect_, Reset, StringId::ResetCalibration, false);
    drawButton(cancelRect_, Cancel, StringId::Cancel, false);
    drawButton(applyRect_, Apply, StringId::Apply, true);

    const auto drawSlider = [&](int id, StringId labelId, const RECT& slider, int minimum, int maximum) {
        RECT label{slider.left, slider.top - 22, slider.right, slider.top - 2};
        const auto labelText = Localized(labelId, language_);
        SetTextColor(dc, RGB(225, 225, 230));
        DrawTextW(dc, labelText.data(), static_cast<int>(labelText.size()), &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        wchar_t value[16]{};
        swprintf_s(value, L"%d%%", calibrationValue(id));
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, value, -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const int centerY = (slider.top + slider.bottom) / 2;
        RECT track{slider.left, centerY - 2, slider.right, centerY + 2};
        FillRoundRect(dc, track, 4, RGB(84, 84, 90), RGB(84, 84, 90));
        const int thumbX = slider.left + MulDiv(calibrationValue(id) - minimum,
                                                slider.right - slider.left, maximum - minimum);
        RECT active{slider.left, centerY - 2, thumbX, centerY + 2};
        if (active.right > active.left) FillRoundRect(dc, active, 4, RGB(52, 152, 255), RGB(52, 152, 255));
        HBRUSH thumb = CreateSolidBrush(RGB(250, 250, 252));
        HPEN thumbPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 212));
        const auto oldBrush = SelectObject(dc, thumb);
        const auto oldPen = SelectObject(dc, thumbPen);
        const int radius = hoveredControl_ == id || pressedControl_ == id ? 8 : 7;
        Ellipse(dc, thumbX - radius, centerY - radius, thumbX + radius + 1, centerY + radius + 1);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(thumbPen);
        DeleteObject(thumb);
    };
    drawSlider(OutputBrightness, StringId::HdrOutputBrightness, outputSliderRect_,
               HdrCalibration::MinimumOutputBrightness, HdrCalibration::MaximumOutputBrightness);
    drawSlider(HighlightCompression, StringId::HdrHighlightCompression, compressionSliderRect_,
               HdrCalibration::MinimumHighlightCompression, HdrCalibration::MaximumHighlightCompression);

    SelectObject(dc, oldFont);
    DeleteObject(bodyFont);
    DeleteObject(titleFont);
    if (buffered) BitBlt(windowDc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
}

bool CalibrationWindow::ensureBackBuffer(HDC reference, int width, int height) {
    if (backBufferDc_ && backBufferBitmap_ && backBufferWidth_ == width && backBufferHeight_ == height) return true;
    releaseBackBuffer();
    if (width <= 0 || height <= 0) return false;
    backBufferDc_ = CreateCompatibleDC(reference);
    if (!backBufferDc_) return false;
    backBufferBitmap_ = CreateCompatibleBitmap(reference, width, height);
    if (!backBufferBitmap_) { releaseBackBuffer(); return false; }
    backBufferPrevious_ = SelectObject(backBufferDc_, backBufferBitmap_);
    if (!backBufferPrevious_ || backBufferPrevious_ == HGDI_ERROR) {
        backBufferPrevious_ = nullptr;
        releaseBackBuffer();
        return false;
    }
    backBufferWidth_ = width;
    backBufferHeight_ = height;
    return true;
}

void CalibrationWindow::releaseBackBuffer() noexcept {
    if (backBufferDc_ && backBufferPrevious_) SelectObject(backBufferDc_, backBufferPrevious_);
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    if (backBufferDc_) DeleteDC(backBufferDc_);
    backBufferDc_ = nullptr;
    backBufferBitmap_ = nullptr;
    backBufferPrevious_ = nullptr;
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

void CalibrationWindow::finish(bool accepted) {
    if (finished_) return;
    accepted_ = accepted;
    if (!accepted) calibration_ = initialCalibration_;
    finished_ = true;
    if (hwnd_) DestroyWindow(hwnd_);
}

} // namespace lumashot
