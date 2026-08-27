#include "OverlayWindow.h"
#include "CaptureService.h"
#include "resource.h"
#include <LumaShot/Geometry.h>
#include <LumaShot/Localization.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <windowsx.h>

namespace lumashot {
namespace {
using Microsoft::WRL::ComPtr;

constexpr wchar_t ClassName[] = L"LumaShot.Overlay";
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
constexpr int ActionCopy = 209;
constexpr int ActionSave = 210;
constexpr int ActionCancel = 211;
constexpr int MagnifierSampleSize = 17;
constexpr int MagnifierZoom = 8;
constexpr int MagnifierPadding = 5;
constexpr int MagnifierContentSize = MagnifierSampleSize * MagnifierZoom;
constexpr int MagnifierSize = MagnifierContentSize + MagnifierPadding * 2;
constexpr std::array<ColorRgba, 6> AnnotationColors{{
    {255, 55, 55, 255}, {255, 213, 55, 255}, {60, 205, 95, 255},
    {50, 205, 230, 255}, {65, 125, 255, 255}, {255, 255, 255, 255}}};
constexpr std::array<float, 3> AnnotationWidths{2.0f, 4.0f, 8.0f};

POINT PointFromLParam(LPARAM value) { return POINT{GET_X_LPARAM(value), GET_Y_LPARAM(value)}; }
bool PointIn(const RECT& rect, POINT point) { return PtInRect(&rect, point) != FALSE; }

COLORREF ToColor(ColorRgba color) { return RGB(color.r, color.g, color.b); }

ImageBgra8 DimPreview(const ImageBgra8& source, BYTE opacity) {
    ImageBgra8 result = source;
    const unsigned int retained = 255u - opacity;
    for (std::size_t offset = 0; offset + 3 < result.pixels.size(); offset += 4) {
        result.pixels[offset] = static_cast<std::uint8_t>((result.pixels[offset] * retained + 127u) / 255u);
        result.pixels[offset + 1] = static_cast<std::uint8_t>((result.pixels[offset + 1] * retained + 127u) / 255u);
        result.pixels[offset + 2] = static_cast<std::uint8_t>((result.pixels[offset + 2] * retained + 127u) / 255u);
    }
    return result;
}

RectI MonitorBounds(HMONITOR monitor, RectI fallback) {
    MONITORINFO info{sizeof(info)};
    return monitor && GetMonitorInfoW(monitor, &info) ? FromWin32Rect(info.rcMonitor) : fallback;
}

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

void DimOutsideSelection(HDC dc, const RECT& client, const RECT& selected, BYTE opacity, bool edgeShadow) {
    RECT visible{};
    if (!IntersectRect(&visible, &selected, &client)) {
        if (opacity > 0) AlphaFill(dc, client, opacity);
        return;
    }
    if (opacity > 0) {
        AlphaFill(dc, {client.left, client.top, client.right, visible.top}, opacity);
        AlphaFill(dc, {client.left, visible.bottom, client.right, client.bottom}, opacity);
        AlphaFill(dc, {client.left, visible.top, visible.left, visible.bottom}, opacity);
        AlphaFill(dc, {visible.right, visible.top, client.right, visible.bottom}, opacity);
    }
    if (!edgeShadow) return;

    constexpr std::array<std::pair<int, BYTE>, 3> shadowLayers{{
        {12, static_cast<BYTE>(18)}, {8, static_cast<BYTE>(22)}, {4, static_cast<BYTE>(28)}}};
    for (const auto [depth, alpha] : shadowLayers) {
        RECT band{};
        RECT candidate{visible.left - depth, visible.top - depth, visible.right + depth, visible.top};
        if (IntersectRect(&band, &candidate, &client)) AlphaFill(dc, band, alpha);
        candidate = {visible.left - depth, visible.bottom, visible.right + depth, visible.bottom + depth};
        if (IntersectRect(&band, &candidate, &client)) AlphaFill(dc, band, alpha);
        candidate = {visible.left - depth, visible.top, visible.left, visible.bottom};
        if (IntersectRect(&band, &candidate, &client)) AlphaFill(dc, band, alpha);
        candidate = {visible.right, visible.top, visible.right + depth, visible.bottom};
        if (IntersectRect(&band, &candidate, &client)) AlphaFill(dc, band, alpha);
    }
}

void DrawSelectionHandles(HDC dc, const RECT& selected) {
    constexpr int radius = 4;
    const int centerX = (selected.left + selected.right) / 2;
    const int centerY = (selected.top + selected.bottom) / 2;
    const std::array<POINT, 8> handles{{
        {selected.left, selected.top}, {centerX, selected.top}, {selected.right, selected.top},
        {selected.left, centerY}, {selected.right, centerY},
        {selected.left, selected.bottom}, {centerX, selected.bottom}, {selected.right, selected.bottom}}};
    HBRUSH fill = CreateSolidBrush(RGB(255, 255, 255));
    HPEN outline = CreatePen(PS_SOLID, 1, RGB(0, 120, 212));
    const auto oldBrush = SelectObject(dc, fill);
    const auto oldPen = SelectObject(dc, outline);
    for (const auto point : handles) {
        RoundRect(dc, point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1, 3, 3);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(outline);
    DeleteObject(fill);
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

void DrawLegacyButtonIcon(HDC dc, int id, const RECT& bounds, COLORREF color,
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

int ToolbarIconIndex(int id) noexcept {
    switch (id) {
    case ModeRegion: return 0;
    case ModeWindow: return 1;
    case ModeMonitor: return 2;
    case ModeAll: return 3;
    case ToolPen: return 4;
    case ToolRectangle: return 5;
    case ToolArrow: return 6;
    case ToolText: return 7;
    case ToolUndo: return 8;
    case ToolRedo: return 9;
    case ToolColor: return 10;
    case ToolWidth: return 11;
    case ActionCopy: return 12;
    case ActionSave: return 13;
    case ActionCancel: return 14;
    default: return -1;
    }
}

class ToolbarIconAtlas final {
public:
    ToolbarIconAtlas() noexcept { load(); }
    ~ToolbarIconAtlas() {
        if (memoryDc_) {
            if (previousBitmap_) SelectObject(memoryDc_, previousBitmap_);
            DeleteDC(memoryDc_);
        }
        if (bitmap_) DeleteObject(bitmap_);
    }
    ToolbarIconAtlas(const ToolbarIconAtlas&) = delete;
    ToolbarIconAtlas& operator=(const ToolbarIconAtlas&) = delete;

    [[nodiscard]] bool draw(HDC target, int index, const RECT& bounds, BYTE opacity) const noexcept {
        constexpr int iconSize = 24;
        constexpr int iconCount = 15;
        if (!memoryDc_ || index < 0 || index >= iconCount) return false;
        const int x = (bounds.left + bounds.right - iconSize) / 2;
        const int y = (bounds.top + bounds.bottom - iconSize) / 2;
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        return AlphaBlend(target, x, y, iconSize, iconSize,
                          memoryDc_, index * iconSize, 0, iconSize, iconSize, blend) != FALSE;
    }

private:
    HDC memoryDc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previousBitmap_{};

    void load() noexcept {
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_TOOLBAR_ICONS), RT_RCDATA);
        if (!resource) return;
        const HGLOBAL loaded = LoadResource(module, resource);
        if (!loaded) return;
        const DWORD byteCount = SizeofResource(module, resource);
        const auto* bytes = static_cast<const BYTE*>(LockResource(loaded));
        if (!bytes || byteCount == 0) return;

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), byteCount)) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return;

        UINT width{}, height{};
        if (FAILED(converter->GetSize(&width, &height)) || width != 360 || height != 24) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels{};
        HDC screen = GetDC(nullptr);
        bitmap_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!bitmap_ || !pixels ||
            FAILED(converter->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(pixels)))) {
            if (bitmap_) { DeleteObject(bitmap_); bitmap_ = nullptr; }
            return;
        }
        memoryDc_ = CreateCompatibleDC(nullptr);
        if (!memoryDc_) { DeleteObject(bitmap_); bitmap_ = nullptr; return; }
        previousBitmap_ = SelectObject(memoryDc_, bitmap_);
    }
};

void DrawButtonIcon(HDC dc, int id, const RECT& bounds, COLORREF color,
                    ColorRgba annotationColor, int annotationWidth) {
    static const ToolbarIconAtlas atlas;
    const BYTE opacity = GetRValue(color) < 200 ? 115 : 255;
    if (!atlas.draw(dc, ToolbarIconIndex(id), bounds, opacity)) {
        DrawLegacyButtonIcon(dc, id, bounds, color, annotationColor, annotationWidth);
        return;
    }

    const int centerX = (bounds.left + bounds.right) / 2;
    const int indicatorY = bounds.bottom - 4;
    HPEN indicator{};
    if (id == ToolColor) indicator = CreateFluentPen(ToColor(annotationColor), 3);
    else if (id == ToolWidth) indicator = CreateFluentPen(color, std::max(1, annotationWidth / 2));
    if (indicator) {
        const auto previous = SelectObject(dc, indicator);
        MoveToEx(dc, centerX - 5, indicatorY, nullptr);
        LineTo(dc, centerX + 5, indicatorY);
        SelectObject(dc, previous);
        DeleteObject(indicator);
    }
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

OverlayWindow::OverlayWindow(HINSTANCE instance, CaptureFrameSet frames, CaptureMode mode, Language language,
                             bool includeCursor, bool copyOnEnter, HdrCalibration calibration)
    : instance_(instance), frames_(std::move(frames)), mode_(mode), language_(language),
      includeCursor_(includeCursor), copyOnEnter_(copyOnEnter), calibration_(calibration) {
    previewSource_ = ColorPipeline::compose(frames_, frames_.virtualDesktop);
    updateCalibrationPreview();
}

bool OverlayWindow::run(const CommitHandler& commit) {
    commit_ = commit;
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_; cls.lpfnWndProc = WindowProc; cls.lpszClassName = ClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_CROSS); cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&cls);
    const auto bounds = frames_.virtualDesktop;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ClassName, L"LumaShot", WS_POPUP,
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
    case WM_SETCURSOR: {
        POINT client{};
        GetCursorPos(&client);
        ScreenToClient(hwnd_, &client);
        SetCursor(cursorForPoint(client));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        POINT client = PointFromLParam(lparam);
        const bool previousMagnifierVisible = magnifierVisible();
        const POINT previousMouseClient = mouseClient_;
        mouseClient_ = client;
        mouseInside_ = true;
        const int hovered = buttonAt(client);
        if (hovered != hoveredButton_) {
            hoveredButton_ = hovered;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (previousMagnifierVisible || magnifierVisible()) {
            RECT clientBounds{};
            GetClientRect(hwnd_, &clientBounds);
            RECT dirty{};
            if (previousMagnifierVisible && magnifierVisible()) {
                const RECT previous = magnifierRect(previousMouseClient, clientBounds);
                const RECT current = magnifierRect(mouseClient_, clientBounds);
                UnionRect(&dirty, &previous, &current);
            } else {
                dirty = magnifierRect(previousMagnifierVisible ? previousMouseClient : mouseClient_, clientBounds);
            }
            InflateRect(&dirty, 4, 4);
            InvalidateRect(hwnd_, &dirty, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&tracking);
        if (pressedButton_ >= 0) { SetCursor(cursorForPoint(client)); return 0; }
        POINT screen = desktopFromClient(client);
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
        SetCursor(cursorForPoint(client));
        return 0;
    }
    case WM_MOUSELEAVE: {
        const bool previousMagnifierVisible = magnifierVisible();
        RECT previousMagnifier{};
        if (previousMagnifierVisible) {
            RECT clientBounds{};
            GetClientRect(hwnd_, &clientBounds);
            previousMagnifier = magnifierRect(mouseClient_, clientBounds);
            InflateRect(&previousMagnifier, 4, 4);
        }
        mouseInside_ = false;
        if (hoveredButton_ >= 0) {
            hoveredButton_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (previousMagnifierVisible) {
            InvalidateRect(hwnd_, &previousMagnifier, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        POINT client = PointFromLParam(lparam);
        if (const int button = buttonAt(client); button >= 0) {
            pressedButton_ = button;
            SetCapture(hwnd_);
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_); return 0;
        }
        POINT screen = desktopFromClient(client);
        if (mode_ == CaptureMode::Window && !selectionLocked_) {
            (void)lockHoveredWindow();
            return 0;
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
        SetCursor(cursorForDrag(dragKind_));
        return 0;
    }
    case WM_LBUTTONUP: {
        if (pressedButton_ >= 0) {
            const int pressed = pressedButton_;
            const bool activate = buttonAt(PointFromLParam(lparam)) == pressed;
            pressedButton_ = -1; ReleaseCapture(); InvalidateRect(hwnd_, nullptr, FALSE);
            if (activate) activateButton(pressed);
            SetCursor(cursorForPoint(PointFromLParam(lparam)));
            return 0;
        }
        if (dragKind_ == DragKind::Drawing) finishDrawing();
        else {
            POINT screen = desktopFromClient(PointFromLParam(lparam));
            uiMonitor_ = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST);
        }
        dragKind_ = DragKind::None; ReleaseCapture(); rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE);
        SetCursor(cursorForPoint(PointFromLParam(lparam)));
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (pressedButton_ >= 0) { pressedButton_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); }
        return 0;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wparam == VK_ESCAPE) { finished_ = true; DestroyWindow(hwnd_); return 0; }
        if (copyOnEnter_ && wparam == VK_RETURN) {
            if (!textEditor_ && dragKind_ == DragKind::None && pressedButton_ < 0 &&
                (mode_ != CaptureMode::Window || selectionLocked_ || lockHoveredWindow())) {
                (void)perform(OverlayAction::Copy);
            }
            return 0;
        }
        if (control && wparam == 'C') { perform(OverlayAction::Copy); return 0; }
        if (control && wparam == 'S') { perform(OverlayAction::Save); return 0; }
        if (control && wparam == 'Z') { annotations_.undo(); InvalidateRect(hwnd_, nullptr, FALSE); return 0; }
        if (control && wparam == 'Y') { annotations_.redo(); InvalidateRect(hwnd_, nullptr, FALSE); return 0; }
        return 0;
    }
    case CommitTextMessage: commitText(wparam != 0); return 0;
    case WM_DISPLAYCHANGE: finished_ = true; DestroyWindow(hwnd_); return 0;
    case WM_CLOSE: finished_ = true; DestroyWindow(hwnd_); return 0;
    case WM_DESTROY: releaseBackBuffer(); hwnd_ = nullptr; return 0;
    default: return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void OverlayWindow::setMode(CaptureMode mode) {
    mode_ = mode; tool_ = AnnotationTool::None; selectionLocked_ = false; hoveredWindow_ = nullptr;
    POINT cursor{}; GetCursorPos(&cursor);
    uiMonitor_ = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (mode == CaptureMode::VirtualDesktop) { selection_ = frames_.virtualDesktop; selectionLocked_ = true; }
    else if (mode == CaptureMode::Monitor) {
        const MonitorFrame* selected = nullptr;
        for (const auto& frame : frames_.monitors) {
            if (frame.monitor == uiMonitor_) { selected = &frame; break; }
        }
        if (!selected) {
            for (const auto& frame : frames_.monitors) {
                if (ContainsPoint(frame.desktopRect, cursor.x, cursor.y)) { selected = &frame; break; }
            }
        }
        if (!selected && !frames_.monitors.empty()) selected = &frames_.monitors.front();
        selection_ = selected ? selected->desktopRect : frames_.virtualDesktop;
        if (selected && selected->monitor) uiMonitor_ = selected->monitor;
        selectionLocked_ = true;
    } else selection_ = {};
    annotations_.clear(); rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::rebuildButtons() {
    RECT client{}; GetClientRect(hwnd_, &client);
    constexpr int buttonSize = 44, gap = 4;
    constexpr int modeCount = 4;
    constexpr int modeGroupWidth = buttonSize * modeCount + gap * (modeCount - 1);
    const RectI monitorBounds = MonitorBounds(uiMonitor_, frames_.virtualDesktop);
    POINT monitorTopLeft = clientFromDesktop({monitorBounds.left, monitorBounds.top});
    POINT monitorBottomRight = clientFromDesktop({monitorBounds.right, monitorBounds.bottom});
    const int modeX = std::clamp(monitorTopLeft.x + (monitorBottomRight.x - monitorTopLeft.x - modeGroupWidth) / 2,
                                 monitorTopLeft.x + 8, std::max(monitorTopLeft.x + 8, monitorBottomRight.x - modeGroupWidth - 8));
    const int modeY = monitorTopLeft.y + 18;
    modeButtons_ = {{{modeX, modeY, modeX + buttonSize, modeY + buttonSize}, ModeRegion, StringId::Region},
                    {{modeX + buttonSize + gap, modeY, modeX + buttonSize * 2 + gap, modeY + buttonSize}, ModeWindow, StringId::Window},
                    {{modeX + (buttonSize + gap) * 2, modeY, modeX + buttonSize * 3 + gap * 2, modeY + buttonSize}, ModeMonitor, StringId::Monitor},
                    {{modeX + (buttonSize + gap) * 3, modeY, modeX + buttonSize * 4 + gap * 3, modeY + buttonSize}, ModeAll, StringId::VirtualDesktop}};
    toolButtons_.clear();
    if (selection_.empty()) return;
    constexpr int count = 11;
    constexpr int toolbarWidth = buttonSize * count + gap * (count - 1);
    const RectI toolbarScreen = PlaceToolbar(selection_, monitorBounds, toolbarWidth, buttonSize);
    POINT toolbarOrigin = clientFromDesktop({toolbarScreen.left, toolbarScreen.top});
    const int toolbarX = toolbarOrigin.x;
    const int toolbarY = toolbarOrigin.y;
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
    POINT screen = desktopFromClient(clientPoint);
    const HWND candidate = windowAt(screen);
    if (candidate == hoveredWindow_) return;
    hoveredWindow_ = candidate; selection_ = {};
    if (candidate) {
        uiMonitor_ = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST);
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(candidate, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(candidate, &rect);
        selection_ = ClampRect(FromWin32Rect(rect), frames_.virtualDesktop);
    }
    rebuildButtons(); InvalidateRect(hwnd_, nullptr, FALSE);
}

bool OverlayWindow::lockHoveredWindow() {
    if (!hoveredWindow_ || selection_.empty()) return false;
    try {
        ShowWindow(hwnd_, SW_HIDE);
        CaptureService service;
        auto frame = service.captureWindow(hoveredWindow_, includeCursor_);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        selection_ = ClampRect(frame.desktopRect, frames_.virtualDesktop);
        RECT selectedRect = ToWin32Rect(selection_);
        uiMonitor_ = frame.monitor ? frame.monitor : MonitorFromRect(&selectedRect, MONITOR_DEFAULTTONEAREST);
        frames_.monitors.push_back(std::move(frame));
        previewSource_ = ColorPipeline::compose(frames_, frames_.virtualDesktop);
        updateCalibrationPreview();
        selectionLocked_ = true;
    } catch (...) {
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        MessageBoxW(hwnd_, Localized(StringId::CaptureFailed, language_).data(), L"LumaShot", MB_OK | MB_ICONERROR);
    }
    rebuildButtons();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return selectionLocked_;
}

OverlayWindow::DragKind OverlayWindow::hitSelection(POINT point) const noexcept {
    if (selection_.empty()) return DragKind::None;
    constexpr int radius = 7;
    if (point.x < selection_.left - radius || point.x > selection_.right + radius ||
        point.y < selection_.top - radius || point.y > selection_.bottom + radius) return DragKind::None;
    const bool left = std::abs(point.x - selection_.left) <= radius, right = std::abs(point.x - selection_.right) <= radius;
    const bool top = std::abs(point.y - selection_.top) <= radius, bottom = std::abs(point.y - selection_.bottom) <= radius;
    if (left && top) return DragKind::TopLeft; if (right && top) return DragKind::TopRight;
    if (left && bottom) return DragKind::BottomLeft; if (right && bottom) return DragKind::BottomRight;
    if (left) return DragKind::Left; if (right) return DragKind::Right; if (top) return DragKind::Top; if (bottom) return DragKind::Bottom;
    return ContainsPoint(selection_, point.x, point.y) ? DragKind::Move : DragKind::None;
}

HCURSOR OverlayWindow::cursorForPoint(POINT clientPoint) const noexcept {
    if (pressedButton_ >= 0 || buttonAt(clientPoint) >= 0) return LoadCursorW(nullptr, IDC_HAND);
    if (GetCapture() == hwnd_ && dragKind_ != DragKind::None) return cursorForDrag(dragKind_);
    if (mode_ == CaptureMode::Window && !selectionLocked_) return LoadCursorW(nullptr, IDC_ARROW);

    const POINT desktopPoint = desktopFromClient(clientPoint);
    if (!selection_.empty() && tool_ != AnnotationTool::None &&
        ContainsPoint(selection_, desktopPoint.x, desktopPoint.y)) {
        return LoadCursorW(nullptr, tool_ == AnnotationTool::Text ? IDC_IBEAM : IDC_CROSS);
    }

    const DragKind hit = hitSelection(desktopPoint);
    return hit == DragKind::None ? LoadCursorW(nullptr, IDC_CROSS) : cursorForDrag(hit);
}

HCURSOR OverlayWindow::cursorForDrag(DragKind dragKind) noexcept {
    switch (dragKind) {
    case DragKind::Left:
    case DragKind::Right: return LoadCursorW(nullptr, IDC_SIZEWE);
    case DragKind::Top:
    case DragKind::Bottom: return LoadCursorW(nullptr, IDC_SIZENS);
    case DragKind::TopLeft:
    case DragKind::BottomRight: return LoadCursorW(nullptr, IDC_SIZENWSE);
    case DragKind::TopRight:
    case DragKind::BottomLeft: return LoadCursorW(nullptr, IDC_SIZENESW);
    case DragKind::Move: return LoadCursorW(nullptr, IDC_SIZEALL);
    case DragKind::NewSelection:
    case DragKind::Drawing: return LoadCursorW(nullptr, IDC_CROSS);
    default: return LoadCursorW(nullptr, IDC_ARROW);
    }
}

PointF OverlayWindow::relativePoint(POINT point) const noexcept {
    return {static_cast<float>(point.x - selection_.left), static_cast<float>(point.y - selection_.top)};
}

POINT OverlayWindow::desktopFromClient(POINT point) const noexcept {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return MapPointBetweenRects(point, FromWin32Rect(client), frames_.virtualDesktop);
}

POINT OverlayWindow::clientFromDesktop(POINT point) const noexcept {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return MapPointBetweenRects(point, frames_.virtualDesktop, FromWin32Rect(client));
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
    POINT client = clientFromDesktop(point);
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
    if (!commit_(hwnd_, action, frames_, selection_, annotations_, calibration_)) return false;
    succeeded_ = true; finished_ = true; DestroyWindow(hwnd_); return true;
}

bool OverlayWindow::ensureBackBuffer(HDC reference, int width, int height) {
    if (backBufferDc_ && backBufferBitmap_ && backBufferWidth_ == width && backBufferHeight_ == height) return true;
    releaseBackBuffer();
    if (width <= 0 || height <= 0) return false;
    backBufferDc_ = CreateCompatibleDC(reference);
    if (!backBufferDc_) return false;
    backBufferBitmap_ = CreateCompatibleBitmap(reference, width, height);
    if (!backBufferBitmap_) {
        releaseBackBuffer();
        return false;
    }
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

void OverlayWindow::releaseBackBuffer() noexcept {
    if (backBufferDc_ && backBufferPrevious_) SelectObject(backBufferDc_, backBufferPrevious_);
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    if (backBufferDc_) DeleteDC(backBufferDc_);
    backBufferDc_ = nullptr;
    backBufferBitmap_ = nullptr;
    backBufferPrevious_ = nullptr;
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

void OverlayWindow::updateCalibrationPreview() {
    preview_ = ColorPipeline::toneMapToSdr(previewSource_, calibration_);
    dimmedPreview_ = DimPreview(preview_, 140);
    maskedPreview_ = dimmedPreview_;
    maskedSelectionPixels_ = {};
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::updateMaskedPreview() {
    if (maskedPreview_.width != dimmedPreview_.width || maskedPreview_.height != dimmedPreview_.height ||
        maskedPreview_.pixels.size() != dimmedPreview_.pixels.size()) {
        maskedPreview_ = dimmedPreview_;
        maskedSelectionPixels_ = {};
    }
    const auto copyRows = [&](const ImageBgra8& source, RectI area) {
        if (area.empty()) return;
        const std::size_t rowBytes = static_cast<std::size_t>(area.width()) * 4;
        for (int y = area.top; y < area.bottom; ++y) {
            const std::size_t offset = (static_cast<std::size_t>(y) * preview_.width + area.left) * 4;
            memcpy(maskedPreview_.pixels.data() + offset, source.pixels.data() + offset, rowBytes);
        }
    };
    copyRows(dimmedPreview_, maskedSelectionPixels_);
    maskedSelectionPixels_ = {};
    if (selection_.empty() || preview_.width == 0 || preview_.height == 0) return;
    const RectI previewBounds{0, 0, static_cast<int>(preview_.width), static_cast<int>(preview_.height)};
    const RectI selectedPixels = ClampRect(
        MapRectBetweenRects(selection_, frames_.virtualDesktop, previewBounds), previewBounds);
    if (selectedPixels.empty()) return;
    copyRows(preview_, selectedPixels);
    maskedSelectionPixels_ = selectedPixels;
}

bool OverlayWindow::magnifierVisible() const noexcept {
    return mouseInside_ && mode_ == CaptureMode::Region && tool_ == AnnotationTool::None &&
           hoveredButton_ < 0 && pressedButton_ < 0 && preview_.width > 0 && preview_.height > 0;
}

RECT OverlayWindow::magnifierRect(POINT clientPoint, const RECT& client) const noexcept {
    return ToWin32Rect(PlaceMagnifier(clientPoint, FromWin32Rect(client), MagnifierSize, MagnifierSize));
}

void OverlayWindow::drawMagnifier(HDC dc, const RECT& client) const {
    if (!magnifierVisible()) return;

    const RECT outer = magnifierRect(mouseClient_, client);
    RECT shadow = outer;
    OffsetRect(&shadow, 2, 3);
    FillRoundRect(dc, shadow, 13, RGB(13, 13, 14), RGB(13, 13, 14));
    FillRoundRect(dc, outer, 13, RGB(31, 31, 34), RGB(112, 112, 118));

    RECT content{outer.left + MagnifierPadding, outer.top + MagnifierPadding,
                 outer.right - MagnifierPadding, outer.bottom - MagnifierPadding};
    const RectI previewBounds{0, 0, static_cast<int>(preview_.width), static_cast<int>(preview_.height)};
    // Match the same client-to-preview transform used to paint the full-screen image.
    // A round trip through desktop coordinates can lose a pixel on scaled desktops.
    const POINT mappedSourcePoint = MapPointBetweenRects(mouseClient_, FromWin32Rect(client), previewBounds);
    const int sourceX = std::clamp(static_cast<int>(mappedSourcePoint.x), 0, static_cast<int>(preview_.width) - 1);
    const int sourceY = std::clamp(static_cast<int>(mappedSourcePoint.y), 0, static_cast<int>(preview_.height) - 1);

    const int sourceWidth = std::min(MagnifierSampleSize, static_cast<int>(preview_.width));
    const int sourceHeight = std::min(MagnifierSampleSize, static_cast<int>(preview_.height));
    const int sourceLeft = std::clamp(sourceX - sourceWidth / 2, 0,
                                      static_cast<int>(preview_.width) - sourceWidth);
    const int sourceTop = std::clamp(sourceY - sourceHeight / 2, 0,
                                     static_cast<int>(preview_.height) - sourceHeight);

    // Give GDI a tightly packed tile so source-rectangle handling cannot shift the sample.
    std::array<std::uint8_t, MagnifierSampleSize * MagnifierSampleSize * 4> sourcePixels{};
    const std::size_t sourceRowBytes = static_cast<std::size_t>(sourceWidth) * 4;
    for (int row = 0; row < sourceHeight; ++row) {
        const std::size_t previewOffset =
            (static_cast<std::size_t>(sourceTop + row) * preview_.width + sourceLeft) * 4;
        memcpy(sourcePixels.data() + static_cast<std::size_t>(row) * sourceRowBytes,
               preview_.pixels.data() + previewOffset, sourceRowBytes);
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sourceWidth;
    info.bmiHeader.biHeight = -sourceHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int previousStretchMode = SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, content.left, content.top, content.right - content.left, content.bottom - content.top,
                  0, 0, sourceWidth, sourceHeight, sourcePixels.data(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
    if (previousStretchMode != 0) SetStretchBltMode(dc, previousStretchMode);

    HPEN contentOutline = CreatePen(PS_SOLID, 1, RGB(190, 190, 195));
    const auto previousPen = SelectObject(dc, contentOutline);
    const auto previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, content.left, content.top, content.right, content.bottom);
    SelectObject(dc, previousPen);
    DeleteObject(contentOutline);

    const int pixelLeft = content.left + (sourceX - sourceLeft) * (content.right - content.left) / sourceWidth;
    const int pixelTop = content.top + (sourceY - sourceTop) * (content.bottom - content.top) / sourceHeight;
    const int pixelRight = content.left + (sourceX - sourceLeft + 1) * (content.right - content.left) / sourceWidth;
    const int pixelBottom = content.top + (sourceY - sourceTop + 1) * (content.bottom - content.top) / sourceHeight;
    HPEN pixelOutline = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(dc, pixelOutline);
    Rectangle(dc, pixelLeft, pixelTop, pixelRight + 1, pixelBottom + 1);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pixelOutline);
}

void OverlayWindow::paint() {
    PAINTSTRUCT ps{}; HDC windowDc = BeginPaint(hwnd_, &ps);
    RECT client{}; GetClientRect(hwnd_, &client);
    const bool buffered = ensureBackBuffer(windowDc, client.right - client.left, client.bottom - client.top);
    HDC dc = buffered ? backBufferDc_ : windowDc;
    const int savedBufferState = buffered ? SaveDC(dc) : 0;
    if (buffered) IntersectClipRect(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = static_cast<LONG>(preview_.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(preview_.height); info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    const bool regionMode = mode_ == CaptureMode::Region;
    if (regionMode) updateMaskedPreview();
    const ImageBgra8& background = regionMode ? maskedPreview_ : preview_;
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, background.width, background.height,
                  background.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    int x{}, y{};
    if (!selection_.empty()) {
        POINT topLeft = clientFromDesktop({selection_.left, selection_.top});
        POINT bottomRight = clientFromDesktop({selection_.right, selection_.bottom});
        RECT selected{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        if (regionMode) {
            DimOutsideSelection(dc, client, selected, 0, true);
        } else {
            DimOutsideSelection(dc, client, selected, 105, false);
        }
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
        if (regionMode) DrawSelectionHandles(dc, selected);
    } else if (!regionMode) AlphaFill(dc, client, 105);
    drawMagnifier(dc, client);
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
    if (buffered) {
        if (savedBufferState != 0) RestoreDC(dc, savedBufferState);
        BitBlt(windowDc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
               dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
}

} // namespace lumashot
