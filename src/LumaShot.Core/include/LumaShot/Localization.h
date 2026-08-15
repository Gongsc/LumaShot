#pragma once
#include "Types.h"
#include <string_view>

namespace lumashot {
enum class StringId {
    AppName, Capture, Region, Window, Monitor, VirtualDesktop, Settings, Exit,
    Copy, Save, Cancel, Pen, Rectangle, Arrow, Text, Undo, Redo, Color, LineWidth,
    Language, Automatic, Chinese, English, Hotkey, IncludeCursor, LaunchAtLogin,
    SettingsSubtitle, CaptureControls, Behavior, HdrCalibrationTitle, HdrCalibrationHint,
    HdrOutputBrightness, HdrHighlightCompression, LivePreview, ResetCalibration,
    HotkeyUnavailable, CaptureFailed, SaveFailed, ClipboardFailed, Unsupported,
    Saved, Copied, PngDescription, JxrDescription
};

[[nodiscard]] Language ResolveLanguage(Language configured) noexcept;
[[nodiscard]] std::wstring_view Localized(StringId id, Language language) noexcept;
} // namespace lumashot
