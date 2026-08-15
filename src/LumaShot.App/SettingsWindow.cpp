#include "SettingsWindow.h"
#include "resource.h"
#include <LumaShot/Localization.h>
#include <algorithm>
#include <array>
#include <dwmapi.h>
#include <string>
#include <uxtheme.h>

namespace lumashot {
namespace {
constexpr wchar_t ClassName[] = L"LumaShot.Settings";
constexpr int LanguageCombo = 1001;
constexpr int HotkeyControl = 1002;
constexpr int CursorCheckbox = 1003;
constexpr int StartupCheckbox = 1004;
constexpr int SaveButton = 1005;
constexpr int CancelButton = 1006;
constexpr int StartCalibrationButton = 1007;
constexpr int CopyOnEnterCheckbox = 1008;
constexpr int LanguageLabel = 1101;
constexpr int HotkeyLabel = 1102;

constexpr COLORREF LightBackground = RGB(243, 243, 243);
constexpr COLORREF LightCard = RGB(255, 255, 255);
constexpr COLORREF LightBorder = RGB(225, 225, 225);
constexpr COLORREF LightText = RGB(32, 32, 32);
constexpr COLORREF LightSecondaryText = RGB(96, 96, 96);
constexpr COLORREF DarkBackground = RGB(32, 32, 32);
constexpr COLORREF DarkCard = RGB(44, 44, 44);
constexpr COLORREF DarkBorder = RGB(61, 61, 61);
constexpr COLORREF DarkText = RGB(250, 250, 250);
constexpr COLORREF DarkSecondaryText = RGB(190, 190, 190);
constexpr COLORREF LightInput = RGB(255, 255, 255);
constexpr COLORREF DarkInput = RGB(51, 51, 51);
constexpr COLORREF Accent = RGB(0, 120, 212);

bool IsSystemDarkMode() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;
}

HWND AddControl(HWND parent, DWORD exStyle, const wchar_t* cls, const wchar_t* text, DWORD style,
                int id, HINSTANCE instance) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}

void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF outline) {
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

std::wstring HotkeyText(HWND control) {
    const WORD hotkey = static_cast<WORD>(SendMessageW(control, HKM_GETHOTKEY, 0, 0));
    const BYTE key = LOBYTE(hotkey);
    const BYTE flags = HIBYTE(hotkey);
    if (key == 0) return {};
    std::wstring result;
    const auto append = [&](std::wstring_view part) {
        if (!result.empty()) result += L" + ";
        result.append(part);
    };
    if (flags & HOTKEYF_CONTROL) append(L"Ctrl");
    if (flags & HOTKEYF_SHIFT) append(L"Shift");
    if (flags & HOTKEYF_ALT) append(L"Alt");
    wchar_t keyName[64]{};
    const UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (GetKeyNameTextW(static_cast<LONG>(scanCode << 16), keyName, ARRAYSIZE(keyName)) > 0) append(keyName);
    else {
        wchar_t fallback[16]{};
        swprintf_s(fallback, L"0x%02X", key);
        append(fallback);
    }
    return result;
}
} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner, AppSettings settings)
    : instance_(instance), owner_(owner), settings_(settings),
      displayLanguage_(ResolveLanguage(settings.language)) {}

bool SettingsWindow::run() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance_;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = ClassName;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;
    cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_LUMASHOT));
    RegisterClassExW(&cls);

    dpi_ = GetDpiForSystem();
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT windowRect{0, 0, scale(720), scale(500)};
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, dpi_);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    hwnd_ = CreateWindowExW(exStyle, ClassName, Localized(StringId::Settings, displayLanguage_).data(), style,
                            work.left + (work.right - work.left - width) / 2,
                            work.top + (work.bottom - work.top - height) / 2,
                            width, height, owner_, nullptr, instance_, this);
    if (!hwnd_) return false;

    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    MSG message{};
    while (!done_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    EnableWindow(owner_, TRUE);
    SetForegroundWindow(owner_);
    return accepted_;
}

void SettingsWindow::activate() const noexcept {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    BringWindowToTop(hwnd_);
    SetActiveWindow(hwnd_);
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::ActionButtonProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                                   UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<SettingsWindow*>(data);
    switch (message) {
    case WM_MOUSEMOVE: {
        if (self->hoveredAction_ != static_cast<int>(id)) {
            const int previous = self->hoveredAction_;
            self->hoveredAction_ = static_cast<int>(id);
            if (previous >= 0) InvalidateRect(GetDlgItem(self->hwnd_, previous), nullptr, FALSE);
            InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        break;
    }
    case WM_MOUSELEAVE:
        if (self->hoveredAction_ == static_cast<int>(id)) {
            self->hoveredAction_ = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, ActionButtonProc, id);
        break;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::InputControlProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                                   UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<SettingsWindow*>(data);
    switch (message) {
    case WM_MOUSEMOVE:
        if (self->hoveredInput_ != static_cast<int>(id)) {
            const int previous = self->hoveredInput_;
            self->hoveredInput_ = static_cast<int>(id);
            if (previous >= 0) InvalidateRect(GetDlgItem(self->hwnd_, previous), nullptr, FALSE);
            InvalidateRect(window, nullptr, FALSE);
        }
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
        }
        break;
    case WM_MOUSELEAVE:
        if (self->hoveredInput_ == static_cast<int>(id)) {
            self->hoveredInput_ = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_PAINT: {
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        self->drawInputControl(window, static_cast<int>(id));
        return result;
    }
    case WM_PRINTCLIENT: {
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        self->drawInputControl(window, static_cast<int>(id), reinterpret_cast<HDC>(wparam));
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, InputControlProc, id);
        break;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT SettingsWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        const UINT windowDpi = GetDpiForWindow(hwnd_);
        if (windowDpi != dpi_) {
            dpi_ = windowDpi;
            RECT current{};
            GetWindowRect(hwnd_, &current);
            RECT desired{0, 0, scale(720), scale(500)};
            AdjustWindowRectExForDpi(&desired, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                                     static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), dpi_);
            const int width = desired.right - desired.left;
            const int height = desired.bottom - desired.top;
            const int centerX = (current.left + current.right) / 2;
            const int centerY = (current.top + current.bottom) / 2;
            SetWindowPos(hwnd_, nullptr, centerX - width / 2, centerY - height / 2, width, height,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        refreshTheme();
        createControls();
        layoutControls();
        return 0;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        layoutControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        createUiResources();
        layoutControls();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        refreshTheme();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark_ ? DarkText : LightText);
        return reinterpret_cast<LRESULT>(cardBrush_);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkColor(dc, dark_ ? DarkInput : LightInput);
        SetTextColor(dc, dark_ ? DarkText : LightText);
        return reinterpret_cast<LRESULT>(inputBrush_);
    }
    case WM_MEASUREITEM:
        if (wparam == LanguageCombo) {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            item->itemHeight = static_cast<UINT>(scale(30));
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (wparam == LanguageCombo) {
            drawComboItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        }
        if (wparam == SaveButton || wparam == CancelButton || wparam == StartCalibrationButton) {
            drawActionButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == SaveButton) {
            readControls();
            accepted_ = true;
            done_ = true;
            DestroyWindow(hwnd_);
            return 0;
        }
        if (LOWORD(wparam) == CancelButton) {
            done_ = true;
            DestroyWindow(hwnd_);
            return 0;
        }
        if (LOWORD(wparam) == StartCalibrationButton) {
            readControls();
            accepted_ = true;
            calibrationRequested_ = true;
            done_ = true;
            DestroyWindow(hwnd_);
            return 0;
        }
        break;
    case WM_CLOSE:
        done_ = true;
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        releaseUiResources();
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void SettingsWindow::createControls() {
    const auto languageLabel = Localized(StringId::Language, displayLanguage_);
    HWND languageText = AddControl(hwnd_, 0, L"STATIC", languageLabel.data(), SS_LEFT, LanguageLabel, instance_);
    HWND language = AddControl(hwnd_, 0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                               LanguageCombo, instance_);
    const std::array values{Localized(StringId::Automatic, displayLanguage_),
                            Localized(StringId::Chinese, displayLanguage_),
                            Localized(StringId::English, displayLanguage_)};
    for (const auto value : values) SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.data()));
    SendMessageW(language, CB_SETCURSEL, static_cast<WPARAM>(settings_.language), 0);

    const auto hotkeyLabel = Localized(StringId::Hotkey, displayLanguage_);
    HWND hotkeyText = AddControl(hwnd_, 0, L"STATIC", hotkeyLabel.data(), SS_LEFT, HotkeyLabel, instance_);
    HWND hotkey = AddControl(hwnd_, 0, HOTKEY_CLASSW, L"", WS_TABSTOP, HotkeyControl, instance_);
    BYTE flags{};
    if (settings_.hotkey.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (settings_.hotkey.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    if (settings_.hotkey.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    SendMessageW(hotkey, HKM_SETHOTKEY, MAKEWORD(settings_.hotkey.virtualKey, flags), 0);
    SendMessageW(hotkey, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S,
                 MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_SHIFT, 0));

    const auto cursor = Localized(StringId::IncludeCursor, displayLanguage_);
    HWND cursorBox = AddControl(hwnd_, 0, L"BUTTON", cursor.data(), BS_AUTOCHECKBOX | WS_TABSTOP,
                                CursorCheckbox, instance_);
    SendMessageW(cursorBox, BM_SETCHECK, settings_.includeCursor ? BST_CHECKED : BST_UNCHECKED, 0);
    const auto copyOnEnter = Localized(StringId::CopyOnEnter, displayLanguage_);
    HWND copyOnEnterBox = AddControl(hwnd_, 0, L"BUTTON", copyOnEnter.data(), BS_AUTOCHECKBOX | WS_TABSTOP,
                                     CopyOnEnterCheckbox, instance_);
    SendMessageW(copyOnEnterBox, BM_SETCHECK, settings_.copyOnEnter ? BST_CHECKED : BST_UNCHECKED, 0);
    const auto startup = Localized(StringId::LaunchAtLogin, displayLanguage_);
    HWND startupBox = AddControl(hwnd_, 0, L"BUTTON", startup.data(), BS_AUTOCHECKBOX | WS_TABSTOP,
                                 StartupCheckbox, instance_);
    SendMessageW(startupBox, BM_SETCHECK, settings_.launchAtLogin ? BST_CHECKED : BST_UNCHECKED, 0);

    const auto startCalibration = Localized(StringId::StartCalibration, displayLanguage_);
    HWND calibrationButton = AddControl(hwnd_, 0, L"BUTTON", startCalibration.data(),
                                        BS_OWNERDRAW | WS_TABSTOP, StartCalibrationButton, instance_);

    const auto save = Localized(StringId::Save, displayLanguage_);
    const auto cancel = Localized(StringId::Cancel, displayLanguage_);
    HWND saveButton = AddControl(hwnd_, 0, L"BUTTON", save.data(), BS_OWNERDRAW | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                 SaveButton, instance_);
    HWND cancelButton = AddControl(hwnd_, 0, L"BUTTON", cancel.data(), BS_OWNERDRAW | WS_TABSTOP,
                                   CancelButton, instance_);
    SetWindowSubclass(saveButton, ActionButtonProc, SaveButton, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(cancelButton, ActionButtonProc, CancelButton, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(calibrationButton, ActionButtonProc, StartCalibrationButton, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(language, InputControlProc, LanguageCombo, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(hotkey, InputControlProc, HotkeyControl, reinterpret_cast<DWORD_PTR>(this));
    for (HWND control : {languageText, language, hotkeyText, hotkey, cursorBox, copyOnEnterBox, startupBox,
                         calibrationButton, saveButton, cancelButton}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        SetWindowTheme(control, dark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    SetWindowTheme(language, L"", nullptr);
    SetWindowTheme(hotkey, L"", nullptr);
}

void SettingsWindow::layoutControls() {
    if (!hwnd_ || !GetDlgItem(hwnd_, LanguageCombo)) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int inputX = scale(250);
    const int inputWidth = std::max(scale(180), static_cast<int>(client.right) - inputX - scale(44));
    MoveWindow(GetDlgItem(hwnd_, LanguageLabel), scale(44), scale(132), scale(180), scale(24), TRUE);
    MoveWindow(GetDlgItem(hwnd_, LanguageCombo), inputX, scale(123), inputWidth, scale(220), TRUE);
    MoveWindow(GetDlgItem(hwnd_, HotkeyLabel), scale(44), scale(178), scale(180), scale(24), TRUE);
    MoveWindow(GetDlgItem(hwnd_, HotkeyControl), inputX, scale(169), inputWidth, scale(34), TRUE);
    MoveWindow(GetDlgItem(hwnd_, CursorCheckbox), scale(44), scale(271), client.right - scale(88), scale(25), TRUE);
    MoveWindow(GetDlgItem(hwnd_, CopyOnEnterCheckbox), scale(44), scale(297), client.right - scale(88), scale(25), TRUE);
    MoveWindow(GetDlgItem(hwnd_, StartupCheckbox), scale(44), scale(323), client.right - scale(88), scale(25), TRUE);
    MoveWindow(GetDlgItem(hwnd_, StartCalibrationButton), client.right - scale(184), scale(385),
               scale(140), scale(38), TRUE);

    constexpr int actionWidth = 96;
    constexpr int actionGap = 10;
    const int actionY = client.bottom - scale(54);
    MoveWindow(GetDlgItem(hwnd_, CancelButton), client.right - scale(44 + actionWidth), actionY,
               scale(actionWidth), scale(36), TRUE);
    MoveWindow(GetDlgItem(hwnd_, SaveButton), client.right - scale(44 + actionWidth * 2 + actionGap), actionY,
               scale(actionWidth), scale(36), TRUE);
}

void SettingsWindow::paint() {
    PAINTSTRUCT ps{};
    HDC windowDc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    HDC dc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, client.right, client.bottom);
    const auto oldBitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, backgroundBrush_);

    const COLORREF card = dark_ ? DarkCard : LightCard;
    const COLORREF border = dark_ ? DarkBorder : LightBorder;
    FillRoundedRect(dc, {scale(24), scale(88), client.right - scale(24), scale(218)},
                    scale(12), card, border);
    FillRoundedRect(dc, {scale(24), scale(230), client.right - scale(24), scale(348)},
                    scale(12), card, border);
    FillRoundedRect(dc, {scale(24), scale(360), client.right - scale(24), scale(440)},
                    scale(12), card, border);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark_ ? DarkText : LightText);
    auto oldFont = SelectObject(dc, titleFont_);
    RECT title{scale(28), scale(18), client.right - scale(28), scale(57)};
    const auto titleText = Localized(StringId::Settings, displayLanguage_);
    DrawTextW(dc, titleText.data(), static_cast<int>(titleText.size()), &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, bodyFont_);
    SetTextColor(dc, dark_ ? DarkSecondaryText : LightSecondaryText);
    RECT subtitle{scale(28), scale(53), client.right - scale(28), scale(78)};
    const auto subtitleText = Localized(StringId::SettingsSubtitle, displayLanguage_);
    DrawTextW(dc, subtitleText.data(), static_cast<int>(subtitleText.size()), &subtitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, captionFont_);
    SetTextColor(dc, dark_ ? DarkSecondaryText : LightSecondaryText);
    RECT captureCaption{scale(44), scale(101), client.right - scale(44), scale(124)};
    const auto captureText = Localized(StringId::CaptureControls, displayLanguage_);
    DrawTextW(dc, captureText.data(), static_cast<int>(captureText.size()), &captureCaption,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT behaviorCaption{scale(44), scale(242), client.right - scale(44), scale(265)};
    const auto behaviorText = Localized(StringId::Behavior, displayLanguage_);
    DrawTextW(dc, behaviorText.data(), static_cast<int>(behaviorText.size()), &behaviorCaption,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT calibrationCaption{scale(44), scale(372), client.right - scale(204), scale(395)};
    const auto calibrationText = Localized(StringId::HdrCalibrationTitle, displayLanguage_);
    DrawTextW(dc, calibrationText.data(), static_cast<int>(calibrationText.size()), &calibrationCaption,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, bodyFont_);
    RECT calibrationHint{scale(44), scale(397), client.right - scale(204), scale(426)};
    const auto hintText = Localized(StringId::HdrCalibrationHint, displayLanguage_);
    DrawTextW(dc, hintText.data(), static_cast<int>(hintText.size()), &calibrationHint,
              DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);

    BitBlt(windowDc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

void SettingsWindow::drawActionButton(const DRAWITEMSTRUCT& item) {
    RECT rect = item.rcItem;
    const bool primary = item.CtlID == SaveButton || item.CtlID == StartCalibrationButton;
    const bool hovered = hoveredAction_ == static_cast<int>(item.CtlID);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    COLORREF fill{};
    COLORREF outline{};
    COLORREF text{};
    if (primary) {
        fill = pressed ? RGB(0, 82, 148) : hovered ? RGB(16, 110, 190) : Accent;
        outline = fill;
        text = RGB(255, 255, 255);
    } else {
        fill = pressed ? (dark_ ? RGB(63, 63, 63) : RGB(218, 218, 218))
                       : hovered ? (dark_ ? RGB(58, 58, 58) : RGB(235, 235, 235))
                                 : (dark_ ? DarkCard : LightCard);
        outline = dark_ ? RGB(84, 84, 84) : RGB(205, 205, 205);
        text = dark_ ? DarkText : LightText;
    }
    FillRoundedRect(item.hDC, rect, scale(8), fill, outline);

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    const auto oldFont = SelectObject(item.hDC, bodyFont_);
    if (pressed) OffsetRect(&rect, 0, scale(1));
    DrawTextW(item.hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -scale(4), -scale(4));
        DrawFocusRect(item.hDC, &focus);
    }
    SelectObject(item.hDC, oldFont);
}

void SettingsWindow::drawComboItem(const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1)) return;
    const bool selected = (item.itemState & ODS_SELECTED) != 0 && (item.itemState & ODS_COMBOBOXEDIT) == 0;
    const COLORREF background = selected ? Accent : (dark_ ? DarkInput : LightInput);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);

    wchar_t text[128]{};
    SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text));
    RECT label = item.rcItem;
    label.left += scale(10);
    label.right -= scale(10);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, selected ? RGB(255, 255, 255) : (dark_ ? DarkText : LightText));
    const auto oldFont = SelectObject(item.hDC, bodyFont_);
    DrawTextW(item.hDC, text, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item.hDC, oldFont);
}

void SettingsWindow::drawInputControl(HWND control, int id, HDC target) {
    const bool ownsDc = target == nullptr;
    HDC dc = ownsDc ? GetDC(control) : target;
    if (!dc) return;
    RECT client{};
    GetClientRect(control, &client);
    const bool focused = GetFocus() == control;
    const bool hovered = hoveredInput_ == id;
    const COLORREF background = dark_ ? DarkInput : LightInput;
    const COLORREF outline = focused ? Accent
                             : hovered ? (dark_ ? RGB(112, 112, 112) : RGB(145, 145, 145))
                                       : (dark_ ? RGB(86, 86, 86) : RGB(190, 190, 190));
    FillRoundedRect(dc, client, scale(6), background, outline);

    wchar_t text[256]{};
    std::wstring hotkeyText;
    if (id == LanguageCombo) {
        const LRESULT index = SendMessageW(control, CB_GETCURSEL, 0, 0);
        if (index >= 0) SendMessageW(control, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    } else {
        hotkeyText = HotkeyText(control);
    }
    RECT label = client;
    label.left += scale(10);
    label.right -= scale(id == LanguageCombo ? 32 : 10);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, IsWindowEnabled(control) ? (dark_ ? DarkText : LightText) : (dark_ ? DarkSecondaryText : LightSecondaryText));
    const auto oldFont = SelectObject(dc, bodyFont_);
    const wchar_t* displayText = id == HotkeyControl ? hotkeyText.c_str() : text;
    DrawTextW(dc, displayText, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);

    if (id == LanguageCombo) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), dark_ ? DarkSecondaryText : LightSecondaryText);
        const auto oldPen = SelectObject(dc, pen);
        const int arrowX = client.right - scale(17);
        const int arrowY = (client.top + client.bottom) / 2;
        MoveToEx(dc, arrowX - scale(4), arrowY - scale(2), nullptr);
        LineTo(dc, arrowX, arrowY + scale(2));
        LineTo(dc, arrowX + scale(4), arrowY - scale(2));
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    if (ownsDc) ReleaseDC(control, dc);
}

void SettingsWindow::refreshTheme() {
    dark_ = IsSystemDarkMode();
    createUiResources();

    const BOOL dark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    EnumChildWindows(hwnd_, [](HWND child, LPARAM data) -> BOOL {
        const bool darkMode = data != 0;
        SetWindowTheme(child, darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        return TRUE;
    }, dark_ ? 1 : 0);
    if (HWND language = GetDlgItem(hwnd_, LanguageCombo)) SetWindowTheme(language, L"", nullptr);
    if (HWND hotkey = GetDlgItem(hwnd_, HotkeyControl)) SetWindowTheme(hotkey, L"", nullptr);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SettingsWindow::createUiResources() {
    releaseUiResources();
    titleFont_ = CreateFontW(-scale(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    bodyFont_ = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    captionFont_ = CreateFontW(-scale(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    backgroundBrush_ = CreateSolidBrush(dark_ ? DarkBackground : LightBackground);
    cardBrush_ = CreateSolidBrush(dark_ ? DarkCard : LightCard);
    inputBrush_ = CreateSolidBrush(dark_ ? DarkInput : LightInput);

    if (hwnd_) {
        EnumChildWindows(hwnd_, [](HWND child, LPARAM data) -> BOOL {
            auto* self = reinterpret_cast<SettingsWindow*>(data);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(self->bodyFont_), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }
}

void SettingsWindow::releaseUiResources() noexcept {
    if (titleFont_) DeleteObject(titleFont_);
    if (bodyFont_) DeleteObject(bodyFont_);
    if (captionFont_) DeleteObject(captionFont_);
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    if (cardBrush_) DeleteObject(cardBrush_);
    if (inputBrush_) DeleteObject(inputBrush_);
    titleFont_ = nullptr;
    bodyFont_ = nullptr;
    captionFont_ = nullptr;
    backgroundBrush_ = nullptr;
    cardBrush_ = nullptr;
    inputBrush_ = nullptr;
}

void SettingsWindow::readControls() {
    const LRESULT language = SendDlgItemMessageW(hwnd_, LanguageCombo, CB_GETCURSEL, 0, 0);
    settings_.language = language >= 0 && language <= 2 ? static_cast<Language>(language) : Language::Automatic;
    const WORD hotkey = static_cast<WORD>(SendDlgItemMessageW(hwnd_, HotkeyControl, HKM_GETHOTKEY, 0, 0));
    settings_.hotkey.virtualKey = LOBYTE(hotkey);
    const BYTE flags = HIBYTE(hotkey);
    settings_.hotkey.modifiers = MOD_NOREPEAT;
    if (flags & HOTKEYF_CONTROL) settings_.hotkey.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_SHIFT) settings_.hotkey.modifiers |= MOD_SHIFT;
    if (flags & HOTKEYF_ALT) settings_.hotkey.modifiers |= MOD_ALT;
    settings_.includeCursor = IsDlgButtonChecked(hwnd_, CursorCheckbox) == BST_CHECKED;
    settings_.copyOnEnter = IsDlgButtonChecked(hwnd_, CopyOnEnterCheckbox) == BST_CHECKED;
    settings_.launchAtLogin = IsDlgButtonChecked(hwnd_, StartupCheckbox) == BST_CHECKED;
}

int SettingsWindow::scale(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

} // namespace lumashot
