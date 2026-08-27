#include "CalibrationWindow.h"
#include "resource.h"
#include <LumaShot/Geometry.h>
#include <algorithm>
#include <commctrl.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
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
constexpr int OutputLabel = 406;
constexpr int CompressionLabel = 407;
constexpr int OutputValue = 408;
constexpr int CompressionValue = 409;

POINT PointFromLParam(LPARAM value) { return {GET_X_LPARAM(value), GET_Y_LPARAM(value)}; }
bool PointIn(const RECT& rect, POINT point) { return PtInRect(&rect, point) != FALSE; }

bool IsHighContrastEnabled() noexcept {
    HIGHCONTRASTW value{sizeof(value)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0) &&
           (value.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

UINT DpiForMonitor(HMONITOR monitor) noexcept {
    UINT x{96}, y{96};
    return monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) ? x : 96;
}

void AlphaFill(HDC target, const RECT& rect, BYTE alpha, COLORREF color) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HDC source = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, 1, 1);
    const auto old = SelectObject(source, bitmap);
    SetPixel(source, 0, 0, color);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(target, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               source, 0, 0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(bitmap);
    DeleteDC(source);
}

void FillTranslucentPanel(HDC dc, const RECT& rect, BYTE alpha, COLORREF fill, COLORREF outline,
                          int radius) {
    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius, radius);
    SelectClipRgn(dc, clip);
    AlphaFill(dc, rect, alpha, fill);
    RestoreDC(dc, saved);
    DeleteObject(clip);
    HPEN border = CreatePen(PS_SOLID, 1, outline);
    const auto oldPen = SelectObject(dc, border);
    const auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
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
    uiDpi_ = DpiForMonitor(uiMonitor_);
    previewSource_ = ColorPipeline::compose(frames_, frames_.virtualDesktop);
    frames_.monitors.clear();
    frames_.monitors.shrink_to_fit();
    updatePreview();
}

bool CalibrationWindow::run() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = ClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_LUMASHOT));
    RegisterClassExW(&cls);
    const RectI bounds = frames_.virtualDesktop;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, ClassName,
                            Localized(StringId::HdrCalibrationTitle, language_).data(), WS_POPUP | WS_CLIPCHILDREN,
                            bounds.left, bounds.top, bounds.width(), bounds.height(),
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    createControls();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    layoutPanel(true);
    MSG message{};
    while (!finished_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        bool handled = false;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            SendMessageW(hwnd_, WM_KEYDOWN, message.wParam, message.lParam);
            handled = true;
        } else if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            const int focusedId = GetDlgCtrlID(GetFocus());
            if (focusedId == Reset || focusedId == Apply || focusedId == Cancel) {
                SendMessageW(GetFocus(), BM_CLICK, 0, 0);
                handled = true;
            } else {
                SendMessageW(hwnd_, WM_KEYDOWN, message.wParam, message.lParam);
                handled = true;
            }
        }
        if (!handled && !IsDialogMessageW(hwnd_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    return accepted_;
}

void CalibrationWindow::createControls() {
    const auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               0, 0, 1, 1, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    const auto outputLabel = Localized(StringId::HdrOutputBrightness, language_);
    const auto compressionLabel = Localized(StringId::HdrHighlightCompression, language_);
    add(L"STATIC", outputLabel.data(), SS_LEFT, OutputLabel);
    add(L"STATIC", compressionLabel.data(), SS_LEFT, CompressionLabel);
    add(L"STATIC", L"", SS_RIGHT, OutputValue);
    add(L"STATIC", L"", SS_RIGHT, CompressionValue);
    HWND output = add(TRACKBAR_CLASSW, outputLabel.data(), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                      OutputBrightness);
    HWND compression = add(TRACKBAR_CLASSW, compressionLabel.data(), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                           HighlightCompression);
    SendMessageW(output, TBM_SETRANGE, TRUE,
                 MAKELPARAM(HdrCalibration::MinimumOutputBrightness, HdrCalibration::MaximumOutputBrightness));
    SendMessageW(compression, TBM_SETRANGE, TRUE,
                 MAKELPARAM(HdrCalibration::MinimumHighlightCompression, HdrCalibration::MaximumHighlightCompression));
    SendMessageW(output, TBM_SETLINESIZE, 0, 1);
    SendMessageW(compression, TBM_SETLINESIZE, 0, 1);
    add(L"BUTTON", Localized(StringId::ResetCalibration, language_).data(), BS_PUSHBUTTON | WS_TABSTOP, Reset);
    add(L"BUTTON", Localized(StringId::Cancel, language_).data(), BS_PUSHBUTTON | WS_TABSTOP, Cancel);
    add(L"BUTTON", Localized(StringId::Apply, language_).data(), BS_DEFPUSHBUTTON | WS_TABSTOP, Apply);

    if (controlFont_) DeleteObject(controlFont_);
    controlFont_ = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Segoe UI Variable Text");
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont_), TRUE);
    refreshControlTheme();
    updateControlValues();
}

void CalibrationWindow::refreshControlTheme() {
    const bool highContrast = IsHighContrastEnabled();
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SetWindowTheme(child, highContrast ? nullptr : L"DarkMode_Explorer", nullptr);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void CalibrationWindow::updateControlValues() {
    if (!hwnd_) return;
    SendDlgItemMessageW(hwnd_, OutputBrightness, TBM_SETPOS, TRUE,
                        calibration_.outputBrightnessPercent);
    SendDlgItemMessageW(hwnd_, HighlightCompression, TBM_SETPOS, TRUE,
                        calibration_.highlightCompressionPercent);
    wchar_t value[16]{};
    swprintf_s(value, L"%d%%", calibration_.outputBrightnessPercent);
    SetDlgItemTextW(hwnd_, OutputValue, value);
    swprintf_s(value, L"%d%%", calibration_.highlightCompressionPercent);
    SetDlgItemTextW(hwnd_, CompressionValue, value);
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
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED &&
            (LOWORD(wparam) == Reset || LOWORD(wparam) == Apply || LOWORD(wparam) == Cancel)) {
            activateControl(LOWORD(wparam));
            return 0;
        }
        break;
    case WM_HSCROLL: {
        const HWND control = reinterpret_cast<HWND>(lparam);
        const int id = control ? GetDlgCtrlID(control) : 0;
        if (id == OutputBrightness || id == HighlightCompression) {
            const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
            if (id == OutputBrightness) calibration_.outputBrightnessPercent = value;
            else calibration_.highlightCompressionPercent = value;
            updatePreview();
            return 0;
        }
        break;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        refreshControlTheme();
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, IsHighContrastEnabled() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(225, 225, 230));
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
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
            const int margin = scale(12);
            const int dx = panelRect_.left < margin ? margin - panelRect_.left
                         : panelRect_.right > client.right - margin ? client.right - margin - panelRect_.right : 0;
            const int dy = panelRect_.top < margin ? margin - panelRect_.top
                         : panelRect_.bottom > client.bottom - margin ? client.bottom - margin - panelRect_.bottom : 0;
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
        const bool panelWasDragging = draggingPanel_;
        const POINT point = PointFromLParam(lparam);
        if (pressed == OutputBrightness || pressed == HighlightCompression)
            setCalibrationFromPoint(pressed, point.x);
        const bool activate = !draggingPanel_ && controlAt(point) == pressed;
        pressedControl_ = -1;
        draggingPanel_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (panelWasDragging) updatePanelDpi();
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
        if (controlFont_) DeleteObject(controlFont_);
        controlFont_ = nullptr;
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void CalibrationWindow::layoutPanel(bool resetPosition) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int panelWidth = scale(420);
    const int panelHeight = scale(224);
    if (resetPosition || IsRectEmpty(&panelRect_)) {
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(uiMonitor_, &monitor)) monitor.rcWork = ToWin32Rect(frames_.virtualDesktop);
        const int monitorLeft = monitor.rcWork.left - frames_.virtualDesktop.left;
        const int monitorTop = monitor.rcWork.top - frames_.virtualDesktop.top;
        const int monitorRight = monitor.rcWork.right - frames_.virtualDesktop.left;
        const int monitorBottom = monitor.rcWork.bottom - frames_.virtualDesktop.top;
        const int x = std::clamp(monitorRight - panelWidth - scale(32), monitorLeft + scale(12),
                                 std::max(monitorLeft + scale(12), monitorRight - panelWidth - scale(12)));
        const int y = std::clamp(monitorTop + (monitorBottom - monitorTop - panelHeight) / 2,
                                 monitorTop + scale(12),
                                 std::max(monitorTop + scale(12), monitorBottom - panelHeight - scale(12)));
        panelRect_ = {x, y, x + panelWidth, y + panelHeight};
    } else {
        const int margin = scale(12);
        const int dx = panelRect_.left < margin ? margin - panelRect_.left
                     : panelRect_.right > client.right - margin ? client.right - margin - panelRect_.right : 0;
        const int dy = panelRect_.top < margin ? margin - panelRect_.top
                     : panelRect_.bottom > client.bottom - margin ? client.bottom - margin - panelRect_.bottom : 0;
        OffsetRect(&panelRect_, dx, dy);
    }
    updatePanelControls();
}

void CalibrationWindow::updatePanelDpi() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const POINT center{(panelRect_.left + panelRect_.right) / 2,
                       (panelRect_.top + panelRect_.bottom) / 2};
    const POINT desktop = MapPointBetweenRects(center, FromWin32Rect(client), frames_.virtualDesktop);
    const HMONITOR monitor = MonitorFromPoint(desktop, MONITOR_DEFAULTTONEAREST);
    const UINT dpi = DpiForMonitor(monitor);
    if (!monitor || dpi == 0 || (monitor == uiMonitor_ && dpi == uiDpi_)) return;

    uiMonitor_ = monitor;
    uiDpi_ = dpi;
    const int width = scale(420);
    const int height = scale(224);
    panelRect_ = {center.x - width / 2, center.y - height / 2,
                  center.x - width / 2 + width, center.y - height / 2 + height};

    if (controlFont_) DeleteObject(controlFont_);
    controlFont_ = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Segoe UI Variable Text");
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont_), TRUE);
    layoutPanel(false);
}

void CalibrationWindow::updatePanelControls() {
    const int left = panelRect_.left;
    const int top = panelRect_.top;
    resetRect_ = {panelRect_.right - scale(92), top + scale(12), panelRect_.right - scale(16), top + scale(42)};
    outputSliderRect_ = {left + scale(20), top + scale(84), panelRect_.right - scale(20), top + scale(114)};
    compressionSliderRect_ = {left + scale(20), top + scale(132), panelRect_.right - scale(20), top + scale(162)};
    cancelRect_ = {panelRect_.right - scale(204), panelRect_.bottom - scale(48),
                   panelRect_.right - scale(112), panelRect_.bottom - scale(14)};
    applyRect_ = {panelRect_.right - scale(104), panelRect_.bottom - scale(48),
                  panelRect_.right - scale(16), panelRect_.bottom - scale(14)};

    const auto move = [&](int id, const RECT& rect) {
        if (HWND control = GetDlgItem(hwnd_, id))
            MoveWindow(control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
    };
    move(Reset, resetRect_);
    move(OutputBrightness, outputSliderRect_);
    move(HighlightCompression, compressionSliderRect_);
    move(Cancel, cancelRect_);
    move(Apply, applyRect_);
    move(OutputLabel, {left + scale(20), top + scale(65), panelRect_.right - scale(90), top + scale(85)});
    move(OutputValue, {panelRect_.right - scale(86), top + scale(65), panelRect_.right - scale(20), top + scale(85)});
    move(CompressionLabel, {left + scale(20), top + scale(113), panelRect_.right - scale(90), top + scale(133)});
    move(CompressionValue, {panelRect_.right - scale(86), top + scale(113), panelRect_.right - scale(20), top + scale(133)});
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
    if (hwnd_) {
        updateControlValues();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
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
    const bool highContrast = IsHighContrastEnabled();
    RECT shadow = panelRect_;
    OffsetRect(&shadow, scale(3), scale(5));
    if (!highContrast)
        FillTranslucentPanel(dc, shadow, 90, RGB(20, 20, 23), RGB(25, 25, 28), scale(22));
    FillTranslucentPanel(dc, panelRect_, highContrast ? 255 : 205,
                         highContrast ? GetSysColor(COLOR_WINDOW) : RGB(20, 20, 23),
                         highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(105, 105, 112), scale(22));
    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(-scale(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    const auto oldFont = SelectObject(dc, titleFont);
    SetTextColor(dc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(255, 255, 255));
    RECT title{panelRect_.left + scale(18), panelRect_.top + scale(10),
               resetRect_.left - scale(8), panelRect_.top + scale(39)};
    const auto titleText = Localized(StringId::HdrCalibrationTitle, language_);
    DrawTextW(dc, titleText.data(), static_cast<int>(titleText.size()), &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, controlFont_);
    SetTextColor(dc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(205, 205, 210));
    RECT hint{panelRect_.left + scale(18), panelRect_.top + scale(40),
              panelRect_.right - scale(18), panelRect_.top + scale(64)};
    const auto hintText = Localized(StringId::CalibrationInstructions, language_);
    DrawTextW(dc, hintText.data(), static_cast<int>(hintText.size()), &hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, oldFont);
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

int CalibrationWindow::scale(int value) const noexcept {
    return MulDiv(value, static_cast<int>(uiDpi_), 96);
}

} // namespace lumashot
