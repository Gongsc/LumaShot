#include "OverlayWindow.h"
#include "CaptureService.h"
#include <HDRSnapshot/Geometry.h>
#include <HDRSnapshot/Localization.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <windowsx.h>

namespace hdrsnapshot {
namespace {
constexpr wchar_t ClassName[] = L"HDRSnapshot.Overlay";
constexpr UINT CommitTextMessage = WM_APP + 41;
constexpr int ModeRegion = 100;
constexpr int ModeWindow = 101;
constexpr int ModeMonitor = 102;
constexpr int ModeAll = 103;
constexpr int ToolPen = 200;
constexpr int ToolRectangle = 201;
constexpr int ToolArrow = 202;
constexpr int ToolText = 203;
constexpr int ToolUndo = 204;
constexpr int ToolRedo = 205;
constexpr int ToolColor = 206;
constexpr int ToolWidth = 207;
constexpr int ActionCopy = 208;
constexpr int ActionSave = 209;
constexpr int ActionCancel = 210;
constexpr std::array<ColorRgba, 6> AnnotationColors{{
    {255, 55, 55, 255}, {255, 213, 55, 255}, {60, 205, 95, 255},
    {50, 205, 230, 255}, {65, 125, 255, 255}, {255, 255, 255, 255}}};
constexpr std::array<float, 3> AnnotationWidths{2.0f, 4.0f, 8.0f};

POINT PointFromLParam(LPARAM value) { return POINT{GET_X_LPARAM(value), GET_Y_LPARAM(value)}; }
bool PointIn(const RECT& rect, POINT point) { return PtInRect(&rect, point) != FALSE; }

COLORREF ToColor(ColorRgba color) { return RGB(color.r, color.g, color.b); }

void AlphaFill(HDC target, RECT rect, BYTE alpha) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HDC source = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, 1, 1);
    const auto old = SelectObject(source, bitmap);
    SetPixel(source, 0, 0, RGB(0, 0, 0));
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(target, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, source, 0, 0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(bitmap);
    DeleteDC(source);
}

void DrawArrowGdi(HDC dc, PointF start, PointF end, HPEN pen) {
    const auto old = SelectObject(dc, pen);
    MoveToEx(dc, static_cast<int>(start.x), static_cast<int>(start.y), nullptr);
    LineTo(dc, static_cast<int>(end.x), static_cast<int>(end.y));
    const float dx = end.x - start.x, dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.01f) {
        const float ux = dx / length, uy = dy / length, head = 18.0f, wing = 9.0f;
        MoveToEx(dc, static_cast<int>(end.x), static_cast<int>(end.y), nullptr);
        LineTo(dc, static_cast<int>(end.x - ux * head - uy * wing), static_cast<int>(end.y - uy * head + ux * wing));
        MoveToEx(dc, static_cast<int>(end.x), static_cast<int>(end.y), nullptr);
        LineTo(dc, static_cast<int>(end.x - ux * head + uy * wing), static_cast<int>(end.y - uy * head - ux * wing));
    }
    SelectObject(dc, old);
}

HPEN CreateFluentPen(COLORREF color, int width) {
    LOGBRUSH brush{BS_SOLID, color, 0};
    return ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                        static_cast<DWORD>(std::max(1, width)), &brush, 0, nullptr);
}

void DrawButtonIcon(HDC dc, int id, const RECT& bounds, COLORREF color,
                    ColorRgba annotationColor, int annotationWidth) {
    const int cx = (bounds.left + bounds.right) / 2;
    const int cy = (bounds.top + bounds.bottom) / 2;
    const int left = cx - 10;
    const int top = cy - 10;
    const int right = cx + 10;
    const int bottom = cy + 10;
    HPEN pen = CreateFluentPen(color, 2);
    const auto oldPen = SelectObject(dc, pen);
    const auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));

    const auto line = [&](int x1, int y1, int x2, int y2) {
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
    };
    switch (id) {
    case ModeRegion:
        line(left, top + 6, left, top); line(left, top, left + 6, top);
        line(right - 6, top, right, top); line(right, top, right, top + 6);
        line(left, bottom - 6, left, bottom); line(left, bottom, left + 6, bottom);
        line(right - 6, bottom, right, bottom); line(right, bottom - 6, right, bottom);
        break;
    case ModeWindow:
        RoundRect(dc, left, top + 1, right, bottom - 1, 3, 3);
        line(left, top + 6, right, top + 6);
        Ellipse(dc, left + 3, top + 3, left + 4, top + 4);
        break;
    case ModeMonitor:
        RoundRect(dc, left, top, right, bottom - 4, 3, 3);
        line(cx, bottom - 4, cx, bottom);
        line(cx - 5, bottom, cx + 5, bottom);
        break;
    case ModeAll:
        RoundRect(dc, left, top + 2, right - 4, bottom - 3, 3, 3);
        RoundRect(dc, left + 5, top - 2, right + 1, bottom - 7, 3, 3);
        break;
    case ToolPen: {
        line(left + 2, bottom - 3, right - 3, top + 2);
        line(left + 1, bottom, left + 4, bottom - 7);
        line(left + 1, bottom, left + 8, bottom - 3);
        line(right - 5, top, right, top + 5);
        break;
    }
    case ToolRectangle:
        RoundRect(dc, left + 1, top + 2, right - 1, bottom - 2, 3, 3);
        break;
    case ToolArrow:
        line(left + 1, bottom - 1, right - 1, top + 1);
        line(right - 8, top + 1, right - 1, top + 1);
        line(right - 1, top + 1, right - 1, top + 8);
        break;
    case ToolText: {
        HFONT font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        const auto previousFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT text{left, top - 2, right, bottom + 2};
        DrawTextW(dc, L"T", 1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previousFont);
        DeleteObject(font);
        SelectObject(dc, pen);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        break;
    }
    case ToolUndo:
        Arc(dc, left + 2, top + 2, right + 1, bottom + 1, right - 1, top + 6, left + 2, top + 8);
        line(left + 2, top + 8, left + 2, top + 2);
        line(left + 2, top + 8, left + 8, top + 8);
        break;
    case ToolRedo:
        Arc(dc, left - 1, top + 2, right - 2, bottom + 1, right - 2, top + 8, left + 1, top + 6);
        line(right - 2, top + 8, right - 2, top + 2);
        line(right - 8, top + 8, right - 2, top + 8);
        break;
    case ToolColor: {
        HBRUSH swatch = CreateSolidBrush(ToColor(annotationColor));
        SelectObject(dc, swatch);
        Ellipse(dc, left + 2, top + 2, right - 2, bottom - 2);
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        DeleteObject(swatch);
        break;
    }
    case ToolWidth: {
        line(left + 2, top + 4, right - 2, top + 4);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        pen = CreateFluentPen(color, std::max(2, annotationWidth / 2));
        SelectObject(dc, pen);
        line(left + 2, cy, right - 2, cy);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        pen = CreateFluentPen(color, std::max(3, annotationWidth));
        SelectObject(dc, pen);
        line(left + 2, bottom - 4, right - 2, bottom - 4);
        break;
    }
    case ActionCopy:
        RoundRect(dc, left + 5, top + 1, right, bottom - 4, 3, 3);
        RoundRect(dc, left, top + 6, right - 5, bottom + 1, 3, 3);
        break;
    case ActionSave:
        RoundRect(dc, left + 1, top, right - 1, bottom, 3, 3);
        line(left + 5, top, left + 5, top + 7);
        line(left + 5, top + 7, right - 5, top + 7);
        line(right - 5, top + 7, right - 5, top);
        RoundRect(dc, left + 5, cy + 3, right - 5, bottom, 2, 2);
        break;
    case ActionCancel:
        line(left + 2, top + 2, right - 2, bottom - 2);
        line(right - 2, top + 2, left + 2, bottom - 2);
        break;
    default:
        break;
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

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

} // namespace

OverlayWindow::OverlayWindow(HINSTANCE instance, CaptureFrameSet frames, CaptureMode mode, Language language, bool includeCursor)
    : instance_(instance), frames_(std::move(frames)), mode_(mode), language_(language), includeCursor_(includeCursor) {
    preview_ = ColorPipeline::toneMapToSdr(ColorPipeline::compose(frames_, frames_.virtualDesktop));
}

bool OverlayWindow::run(const CommitHandler& commit) {
    commit_ = commit;
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_; cls.lpfnWndProc = WindowProc; cls.lpszClassName = ClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_CROSS); cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&cls);
    const auto bounds = frames_.virtualDesktop;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName, L"HDRSnapshot", WS_POPUP,
                            bounds.left, bounds.top, bounds.width(), bounds.height(), nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    setMode(mode_);
    MSG message{};
    while (!finished_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    return succeeded_;
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK OverlayWindow::EditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) {
    if (message == WM_KEYDOWN && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
        PostMessageW(reinterpret_cast<HWND>(data), CommitTextMessage, wparam == VK_ESCAPE, 0);
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT OverlayWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, hoveredButton_ >= 0 ? IDC_HAND : (tool_ == AnnotationTool::Text ? IDC_IBEAM : IDC_CROSS))); return TRUE;
    case WM_MOUSEMOVE: {
        POINT client = PointFromLParam(lparam);
        const int hovered = buttonAt(client);
        if (hovered != hoveredButton_) {
            hoveredButton_ = hovered;
            SetCursor(LoadCursorW(nullptr, hoveredButton_ >= 0 ? IDC_HAND : (tool_ == AnnotationTool::Text ? IDC_IBEAM : IDC_CROSS)));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&tracking);
        if (pressedButton_ >= 0) return 0;
        POINT screen = client; ClientToScreen(hwnd_, &screen);
        if ((wparam & MK_LBUTTON) != 0) {
            if (dragKind_ == DragKind::Drawing) updateDrawing(screen);
            else if (dragKind_ != DragKind::None) {
                const int dx = screen.x - dragStart_.x, dy = screen.y - dragStart_.y;
                RectI next = initialSelection_;
                if (dragKind_ == DragKind::NewSelection) next = {dragStart_.x, dragStart_.y, screen.x, screen.y};
                if (dragKind_ == DragKind::Move) { next.left += dx; next.right += dx; next.top += dy; next.bottom += dy; }
                if (dragKind_ == DragKind::Left || dragKind_ == DragKind::TopLeft || dragKind_ == DragKind::BottomLeft) next.left += dx;
                if (dragKind_ == DragKind::Right || dragKind_ == DragKind::TopRight || dragKind_ == DragKind::BottomRight) next.right += dx;
                if (dragKind_ == DragKind::Top || dragKind_ == DragKind::TopLeft || dragKind_ == DragKind::TopRight) next.top += dy;
                if (dragKind_ == DragKind::Bottom || dragKind_ == DragKind::BottomLeft || dragKind_ == DragKind::BottomRight) next.bottom += dy;
                selection_ = ClampRect(NormalizeRect(next), frames_.virtualDesktop);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else if (mode_ == CaptureMode::Window && !selectionLocked_) updateWindowHover(client);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hoveredButton_ >= 0) { hoveredButton_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        POINT client = PointFromLParam(lparam);
        if (const int button = buttonAt(client); button >= 0) {
            pressedButton_ = button; SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_); return 0;
        }
        POINT screen = client; ClientToScreen(hwnd_, &screen);
        if (mode_ == CaptureMode::Window && !selectionLocked_) {
            if (hoveredWindow_ && !selection_.empty()) {
                try {
                    ShowWindow(hwnd_, SW_HIDE);
                    CaptureService service;
                    auto frame = service.captureWindow(hoveredWindow_, includeCursor_);
                    ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_);
                    selection_ = ClampRect(frame.desktopRect, frames_.virtualDesktop);
                    frames_.monitors.push_back(std::move(frame));
                    preview_ = ColorPipeline::toneMapToSdr(ColorPipeline::compose(frames_, frames_.virtualDesktop));
                    selectionLocked_ = true;
                } catch (...) {
                    ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_);
                    MessageBoxW(hwnd_, Localized(StringId::CaptureFailed, language_).data(), L"HDRSnapshot", MB_OK | MB_ICONERROR);
                }
            }
            rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        }
        if (!selection_.empty() && tool_ != AnnotationTool::None && ContainsPoint(selection_, screen.x, screen.y)) {
            if (tool_ == AnnotationTool::Text) beginText(screen); else beginDrawing(screen);
            return 0;
        }
        dragKind_ = hitSelection(screen);
        if (dragKind_ == DragKind::None) dragKind_ = DragKind::NewSelection;
        dragStart_ = screen; initialSelection_ = selection_;
        if (dragKind_ == DragKind::NewSelection) selection_ = {screen.x, screen.y, screen.x, screen.y};
        SetCapture(hwnd_);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (pressedButton_ >= 0) {
            const int pressed = pressedButton_;
            const bool activate = buttonAt(PointFromLParam(lparam)) == pressed;
            pressedButton_ = -1; ReleaseCapture(); InvalidateRect(hwnd_, nullptr, FALSE);
            if (activate) activateButton(pressed);
            return 0;
        }
        if (dragKind_ == DragKind::Drawing) finishDrawing();
        dragKind_ = DragKind::None; ReleaseCapture(); rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE); return 0;
    }
    case WM_CAPTURECHANGED:
        if (pressedButton_ >= 0) { pressedButton_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); }
        return 0;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wparam == VK_ESCAPE) { finished_ = true; DestroyWindow(hwnd_); return 0; }
        if (control && wparam == 'C') { perform(OverlayAction::Copy); return 0; }
        if (control && wparam == 'S') { perform(OverlayAction::Save); return 0; }
        if (control && wparam == 'Z') { annotations_.undo(); InvalidateRect(hwnd_, nullptr, FALSE); return 0; }
        if (control && wparam == 'Y') { annotations_.redo(); InvalidateRect(hwnd_, nullptr, FALSE); return 0; }
        return 0;
    }
    case CommitTextMessage: commitText(wparam != 0); return 0;
    case WM_DISPLAYCHANGE: finished_ = true; DestroyWindow(hwnd_); return 0;
    case WM_CLOSE: finished_ = true; DestroyWindow(hwnd_); return 0;
    case WM_DESTROY: hwnd_ = nullptr; return 0;
    default: return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void OverlayWindow::setMode(CaptureMode mode) {
    mode_ = mode; tool_ = AnnotationTool::None; selectionLocked_ = false; hoveredWindow_ = nullptr;
    POINT cursor{}; GetCursorPos(&cursor);
    if (mode == CaptureMode::VirtualDesktop) { selection_ = frames_.virtualDesktop; selectionLocked_ = true; }
    else if (mode == CaptureMode::Monitor) {
        const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        for (const auto& frame : frames_.monitors) if (frame.monitor == monitor) { selection_ = frame.desktopRect; break; }
        selectionLocked_ = true;
    } else selection_ = {};
    annotations_.clear(); rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::rebuildButtons() {
    RECT client{}; GetClientRect(hwnd_, &client);
    constexpr int buttonSize = 44, gap = 4;
    constexpr int modeCount = 4;
    constexpr int modeGroupWidth = buttonSize * modeCount + gap * (modeCount - 1);
    const int x = (client.right - modeGroupWidth) / 2;
    modeButtons_ = {{{x, 18, x + buttonSize, 18 + buttonSize}, ModeRegion, StringId::Region},
                    {{x + buttonSize + gap, 18, x + buttonSize * 2 + gap, 18 + buttonSize}, ModeWindow, StringId::Window},
                    {{x + (buttonSize + gap) * 2, 18, x + buttonSize * 3 + gap * 2, 18 + buttonSize}, ModeMonitor, StringId::Monitor},
                    {{x + (buttonSize + gap) * 3, 18, x + buttonSize * 4 + gap * 3, 18 + buttonSize}, ModeAll, StringId::VirtualDesktop}};
    toolButtons_.clear();
    if (selection_.empty()) return;
    constexpr int count = 11;
    constexpr int toolbarWidth = buttonSize * count + gap * (count - 1);
    POINT selectionTopLeft{selection_.left, selection_.top};
    POINT selectionBottomRight{selection_.right, selection_.bottom};
    ScreenToClient(hwnd_, &selectionTopLeft); ScreenToClient(hwnd_, &selectionBottomRight);
    int toolbarX = selectionTopLeft.x + (selectionBottomRight.x - selectionTopLeft.x - toolbarWidth) / 2;
    const int selectionTop = selectionTopLeft.y;
    const int selectionBottom = selectionBottomRight.y;
    const int below = selectionBottom + 8;
    const int above = selectionTop - buttonSize - 8;
    int toolbarY{};
    toolbarX = std::clamp(toolbarX, 8, std::max(8, static_cast<int>(client.right) - toolbarWidth - 8));
    if (below + buttonSize <= client.bottom - 8) toolbarY = below;
    else if (above >= 8) toolbarY = above;
    else toolbarY = std::clamp(selectionBottom - buttonSize - 12, 8, std::max(8, static_cast<int>(client.bottom) - buttonSize - 8));
    const std::array defs{
        std::pair{ToolPen, StringId::Pen}, std::pair{ToolRectangle, StringId::Rectangle}, std::pair{ToolArrow, StringId::Arrow},
        std::pair{ToolText, StringId::Text}, std::pair{ToolUndo, StringId::Undo}, std::pair{ToolRedo, StringId::Redo},
        std::pair{ToolColor, StringId::Color}, std::pair{ToolWidth, StringId::LineWidth},
        std::pair{ActionCopy, StringId::Copy}, std::pair{ActionSave, StringId::Save}, std::pair{ActionCancel, StringId::Cancel}};
    for (int i = 0; i < count; ++i) {
        const int buttonX = toolbarX + i * (buttonSize + gap);
        toolButtons_.push_back({{buttonX, toolbarY, buttonX + buttonSize, toolbarY + buttonSize}, defs[i].first, defs[i].second});
    }
}

int OverlayWindow::buttonAt(POINT point) const noexcept {
    for (const auto& button : modeButtons_) if (PointIn(button.rect, point)) return button.id;
    for (const auto& button : toolButtons_) if (PointIn(button.rect, point)) return button.id;
    return -1;
}

void OverlayWindow::activateButton(int id) {
    switch (id) {
    case ModeRegion: setMode(CaptureMode::Region); break; case ModeWindow: setMode(CaptureMode::Window); break;
    case ModeMonitor: setMode(CaptureMode::Monitor); break; case ModeAll: setMode(CaptureMode::VirtualDesktop); break;
    case ToolPen: tool_ = AnnotationTool::Pen; break; case ToolRectangle: tool_ = AnnotationTool::Rectangle; break;
    case ToolArrow: tool_ = AnnotationTool::Arrow; break; case ToolText: tool_ = AnnotationTool::Text; break;
    case ToolUndo: annotations_.undo(); break; case ToolRedo: annotations_.redo(); break;
    case ToolColor: colorIndex_ = (colorIndex_ + 1) % AnnotationColors.size(); break;
    case ToolWidth: lineWidthIndex_ = (lineWidthIndex_ + 1) % AnnotationWidths.size(); break;
    case ActionCopy: perform(OverlayAction::Copy); break; case ActionSave: perform(OverlayAction::Save); break;
    case ActionCancel: finished_ = true; DestroyWindow(hwnd_); break;
    default: return;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

HWND OverlayWindow::windowAt(POINT point) const {
    const DWORD currentProcess = GetCurrentProcessId();
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        if (!IsWindowVisible(window) || IsIconic(window) || window == hwnd_ || window == GetShellWindow()) continue;
        DWORD process{}; GetWindowThreadProcessId(window, &process);
        if (process == currentProcess) continue;
        if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) continue;
        wchar_t className[64]{}; GetClassNameW(window, className, ARRAYSIZE(className));
        if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 || wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) continue;
        DWORD cloaked{}; DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) continue;
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(window, &rect);
        if (PtInRect(&rect, point)) return window;
    }
    return nullptr;
}

void OverlayWindow::updateWindowHover(POINT clientPoint) {
    POINT screen = clientPoint; ClientToScreen(hwnd_, &screen);
    const HWND candidate = windowAt(screen);
    if (candidate == hoveredWindow_) return;
    hoveredWindow_ = candidate; selection_ = {};
    if (candidate) {
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(candidate, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(candidate, &rect);
        selection_ = ClampRect(FromWin32Rect(rect), frames_.virtualDesktop);
    }
    rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE);
}

OverlayWindow::DragKind OverlayWindow::hitSelection(POINT point) const noexcept {
    if (selection_.empty()) return DragKind::None;
    constexpr int radius = 7;
    const bool left = std::abs(point.x - selection_.left) <= radius, right = std::abs(point.x - selection_.right) <= radius;
    const bool top = std::abs(point.y - selection_.top) <= radius, bottom = std::abs(point.y - selection_.bottom) <= radius;
    if (left && top) return DragKind::TopLeft; if (right && top) return DragKind::TopRight;
    if (left && bottom) return DragKind::BottomLeft; if (right && bottom) return DragKind::BottomRight;
    if (left) return DragKind::Left; if (right) return DragKind::Right; if (top) return DragKind::Top; if (bottom) return DragKind::Bottom;
    return ContainsPoint(selection_, point.x, point.y) ? DragKind::Move : DragKind::None;
}

PointF OverlayWindow::relativePoint(POINT point) const noexcept {
    return {static_cast<float>(point.x - selection_.left), static_cast<float>(point.y - selection_.top)};
}

void OverlayWindow::beginDrawing(POINT point) {
    const PointF relative = relativePoint(point);
    const auto color = AnnotationColors[colorIndex_];
    const float width = AnnotationWidths[lineWidthIndex_];
    if (tool_ == AnnotationTool::Pen) draft_ = PenStroke{{relative}, color, width};
    else if (tool_ == AnnotationTool::Rectangle) draft_ = RectangleAnnotation{relative, relative, color, width};
    else if (tool_ == AnnotationTool::Arrow) draft_ = ArrowAnnotation{relative, relative, color, width};
    dragKind_ = DragKind::Drawing; SetCapture(hwnd_);
}

void OverlayWindow::updateDrawing(POINT point) {
    if (!draft_) return;
    point.x = std::clamp(static_cast<int>(point.x), selection_.left, selection_.right); point.y = std::clamp(static_cast<int>(point.y), selection_.top, selection_.bottom);
    const PointF relative = relativePoint(point);
    std::visit([&](auto& item) { using T = std::decay_t<decltype(item)>; if constexpr (std::is_same_v<T, PenStroke>) item.points.push_back(relative); else if constexpr (!std::is_same_v<T, TextAnnotation>) item.end = relative; }, *draft_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::finishDrawing() {
    if (draft_) annotations_.add(std::move(*draft_));
    draft_.reset(); ReleaseCapture();
}

void OverlayWindow::beginText(POINT point) {
    if (textEditor_) return;
    textOrigin_ = relativePoint(point);
    POINT client = point; ScreenToClient(hwnd_, &client);
    textEditor_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                  client.x, client.y, 280, 34, hwnd_, nullptr, instance_, nullptr);
    if (textEditor_) {
        SendMessageW(textEditor_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SetWindowSubclass(textEditor_, EditProc, 1, reinterpret_cast<DWORD_PTR>(hwnd_));
        SetFocus(textEditor_);
    }
}

void OverlayWindow::commitText(bool cancel) {
    if (!textEditor_) return;
    if (!cancel) {
        const int length = GetWindowTextLengthW(textEditor_);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(textEditor_, text.data(), length + 1); text.resize(length);
        if (!text.empty()) annotations_.add(TextAnnotation{textOrigin_, std::move(text), AnnotationColors[colorIndex_], 20.0f});
    }
    RemoveWindowSubclass(textEditor_, EditProc, 1); DestroyWindow(textEditor_); textEditor_ = nullptr;
    SetFocus(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
}

bool OverlayWindow::perform(OverlayAction action) {
    if (selection_.empty() || !commit_) return false;
    if (!commit_(hwnd_, action, frames_, selection_, annotations_)) return false;
    succeeded_ = true; finished_ = true; DestroyWindow(hwnd_); return true;
}

void OverlayWindow::paint() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
    RECT client{}; GetClientRect(hwnd_, &client);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = static_cast<LONG>(preview_.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(preview_.height); info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, preview_.width, preview_.height, preview_.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    int x{}, y{};
    if (!selection_.empty()) {
        POINT topLeft{selection_.left, selection_.top};
        POINT bottomRight{selection_.right, selection_.bottom};
        ScreenToClient(hwnd_, &topLeft); ScreenToClient(hwnd_, &bottomRight);
        RECT selected{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        RECT visible{};
        if (IntersectRect(&visible, &selected, &client)) {
            AlphaFill(dc, {client.left, client.top, client.right, visible.top}, 105);
            AlphaFill(dc, {client.left, visible.bottom, client.right, client.bottom}, 105);
            AlphaFill(dc, {client.left, visible.top, visible.left, visible.bottom}, 105);
            AlphaFill(dc, {visible.right, visible.top, client.right, visible.bottom}, 105);
        } else AlphaFill(dc, client, 105);
        x = topLeft.x; y = topLeft.y;
        HPEN border = CreatePen(PS_SOLID, 2, RGB(52, 152, 255)); const auto oldPen = SelectObject(dc, border); const auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, x, y, bottomRight.x, bottomRight.y); SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(border);
        const auto drawOne = [&](const Annotation& annotation) {
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                const int strokeWidth = [&] { if constexpr (std::is_same_v<T, TextAnnotation>) return 1; else return std::max(1, static_cast<int>(item.width)); }();
                HPEN pen = CreatePen(PS_SOLID, strokeWidth, ToColor(item.color));
                const auto old = SelectObject(dc, pen);
                if constexpr (std::is_same_v<T, PenStroke>) {
                    if (!item.points.empty()) { MoveToEx(dc, x + static_cast<int>(item.points[0].x), y + static_cast<int>(item.points[0].y), nullptr); for (std::size_t i = 1; i < item.points.size(); ++i) LineTo(dc, x + static_cast<int>(item.points[i].x), y + static_cast<int>(item.points[i].y)); }
                } else if constexpr (std::is_same_v<T, RectangleAnnotation>) {
                    const auto hollow = SelectObject(dc, GetStockObject(HOLLOW_BRUSH)); Rectangle(dc, x + static_cast<int>(item.start.x), y + static_cast<int>(item.start.y), x + static_cast<int>(item.end.x), y + static_cast<int>(item.end.y)); SelectObject(dc, hollow);
                } else if constexpr (std::is_same_v<T, ArrowAnnotation>) {
                    DrawArrowGdi(dc, {x + item.start.x, y + item.start.y}, {x + item.end.x, y + item.end.y}, pen);
                } else if constexpr (std::is_same_v<T, TextAnnotation>) {
                    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, ToColor(item.color)); RECT textRect{x + static_cast<int>(item.origin.x), y + static_cast<int>(item.origin.y), x + selection_.width(), y + selection_.height()}; DrawTextW(dc, item.text.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOPREFIX);
                }
                SelectObject(dc, old); DeleteObject(pen);
            }, annotation);
        };
        for (const auto& annotation : annotations_.items()) drawOne(annotation);
        if (draft_) drawOne(*draft_);
    } else AlphaFill(dc, client, 105);
    rebuildButtons();
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    const auto oldFont = SelectObject(dc, font);

    const auto drawGroupSurface = [&](const std::vector<Button>& buttons) {
        if (buttons.empty()) return;
        RECT surface{buttons.front().rect.left - 6, buttons.front().rect.top - 6,
                     buttons.back().rect.right + 6, buttons.back().rect.bottom + 6};
        FillRoundRect(dc, surface, 13, RGB(31, 31, 34), RGB(75, 75, 79));
    };
    drawGroupSurface(modeButtons_);
    drawGroupSurface(toolButtons_);

    auto drawButtons = [&](const std::vector<Button>& buttons) {
        for (const auto& button : buttons) {
            const bool active = (button.id == ModeRegion && mode_ == CaptureMode::Region) ||
                                (button.id == ModeWindow && mode_ == CaptureMode::Window) ||
                                (button.id == ModeMonitor && mode_ == CaptureMode::Monitor) ||
                                (button.id == ModeAll && mode_ == CaptureMode::VirtualDesktop) ||
                                (button.id == ToolPen && tool_ == AnnotationTool::Pen) ||
                                (button.id == ToolRectangle && tool_ == AnnotationTool::Rectangle) ||
                                (button.id == ToolArrow && tool_ == AnnotationTool::Arrow) ||
                                (button.id == ToolText && tool_ == AnnotationTool::Text);
            const bool hovered = button.id == hoveredButton_;
            const bool pressed = button.id == pressedButton_;
            const bool disabled = (button.id == ToolUndo && !annotations_.canUndo()) ||
                                  (button.id == ToolRedo && !annotations_.canRedo());
            const COLORREF background = pressed ? RGB(0, 82, 148)
                                      : active ? RGB(0, 120, 212)
                                      : hovered ? RGB(62, 62, 66)
                                                : RGB(42, 42, 45);
            const COLORREF outline = active ? RGB(72, 169, 244)
                                   : hovered ? RGB(92, 92, 97)
                                             : RGB(42, 42, 45);
            FillRoundRect(dc, button.rect, 9, background, outline);
            RECT iconRect = button.rect;
            if (pressed) OffsetRect(&iconRect, 0, 1);
            DrawButtonIcon(dc, button.id, iconRect,
                           disabled ? RGB(125, 125, 130) : RGB(250, 250, 250),
                           AnnotationColors[colorIndex_],
                           static_cast<int>(AnnotationWidths[lineWidthIndex_]));
        }
    };
    drawButtons(modeButtons_);
    drawButtons(toolButtons_);

    const Button* tooltipButton = nullptr;
    for (const auto& button : modeButtons_) if (button.id == hoveredButton_) tooltipButton = &button;
    for (const auto& button : toolButtons_) if (button.id == hoveredButton_) tooltipButton = &button;
    if (tooltipButton) {
        const auto label = Localized(tooltipButton->label, language_);
        SIZE size{};
        GetTextExtentPoint32W(dc, label.data(), static_cast<int>(label.size()), &size);
        const int tooltipWidth = size.cx + 20;
        const int tooltipHeight = 30;
        int tooltipX = (tooltipButton->rect.left + tooltipButton->rect.right - tooltipWidth) / 2;
        tooltipX = std::clamp(tooltipX, 8, std::max(8, static_cast<int>(client.right) - tooltipWidth - 8));
        int tooltipY = tooltipButton->id < ToolPen ? tooltipButton->rect.bottom + 10
                                                   : tooltipButton->rect.top - tooltipHeight - 10;
        if (tooltipY < 8) tooltipY = tooltipButton->rect.bottom + 10;
        if (tooltipY + tooltipHeight > client.bottom - 8) tooltipY = tooltipButton->rect.top - tooltipHeight - 10;
        RECT shadow{tooltipX + 2, tooltipY + 3, tooltipX + tooltipWidth + 2, tooltipY + tooltipHeight + 3};
        FillRoundRect(dc, shadow, 7, RGB(15, 15, 16), RGB(15, 15, 16));
        RECT tooltip{tooltipX, tooltipY, tooltipX + tooltipWidth, tooltipY + tooltipHeight};
        FillRoundRect(dc, tooltip, 7, RGB(46, 46, 49), RGB(88, 88, 92));
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, label.data(), static_cast<int>(label.size()), &tooltip,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd_, &ps);
}

} // namespace hdrsnapshot
