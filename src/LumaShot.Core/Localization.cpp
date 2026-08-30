#include <LumaShot/Localization.h>
#include <Windows.h>
#include <array>
#include <vector>

namespace lumashot {
namespace {
struct Entry { StringId id; std::wstring_view zh; std::wstring_view en; };
constexpr std::array entries{
    Entry{StringId::AppName, L"LumaShot", L"LumaShot"}, Entry{StringId::Capture, L"开始截图", L"Capture"},
    Entry{StringId::Region, L"矩形区域", L"Region"}, Entry{StringId::Window, L"窗口", L"Window"},
    Entry{StringId::Monitor, L"当前屏幕", L"Current display"}, Entry{StringId::VirtualDesktop, L"全部屏幕", L"All displays"},
    Entry{StringId::Settings, L"设置", L"Settings"}, Entry{StringId::Exit, L"退出", L"Exit"},
    Entry{StringId::Copy, L"复制", L"Copy"}, Entry{StringId::Save, L"保存", L"Save"}, Entry{StringId::Cancel, L"取消", L"Cancel"},
    Entry{StringId::Pen, L"画笔", L"Pen"}, Entry{StringId::Rectangle, L"矩形", L"Rectangle"}, Entry{StringId::Arrow, L"箭头", L"Arrow"},
    Entry{StringId::Text, L"文字", L"Text"}, Entry{StringId::AddText, L"添加文字", L"Add text"},
    Entry{StringId::Font, L"字体", L"Font"},
    Entry{StringId::FontSize, L"字号", L"Size"},
    Entry{StringId::TextInputPlaceholder, L"在此输入文字", L"Type text here"},
    Entry{StringId::TextInputHint, L"Enter 确认  ·  Esc 取消", L"Enter to apply  ·  Esc to cancel"},
    Entry{StringId::Undo, L"撤销", L"Undo"}, Entry{StringId::Redo, L"重做", L"Redo"},
    Entry{StringId::Color, L"颜色", L"Color"}, Entry{StringId::LineWidth, L"粗细", L"Width"},
    Entry{StringId::Selected, L"已选择", L"Selected"},
    Entry{StringId::Language, L"语言", L"Language"}, Entry{StringId::Automatic, L"跟随系统", L"System default"},
    Entry{StringId::Chinese, L"简体中文", L"Simplified Chinese"}, Entry{StringId::English, L"英文", L"English"},
    Entry{StringId::Hotkey, L"快捷键", L"Hotkey"}, Entry{StringId::IncludeCursor, L"包含鼠标指针", L"Include mouse pointer"},
    Entry{StringId::CopyOnEnter, L"按回车复制截图", L"Press Enter to copy screenshot"}, Entry{StringId::LaunchAtLogin, L"开机启动", L"Start at sign-in"},
    Entry{StringId::SettingsSubtitle, L"个性化您的截图体验", L"Personalize your capture experience"},
    Entry{StringId::CaptureControls, L"截图与快捷键", L"Capture controls"}, Entry{StringId::Behavior, L"常规行为", L"Behavior"},
    Entry{StringId::HdrCalibrationTitle, L"HDR 校准", L"HDR calibration"},
    Entry{StringId::HdrCalibrationHint, L"在全屏截图中预览并调整 HDR 到 SDR 的转换", L"Preview and tune HDR-to-SDR conversion on a full-screen capture"},
    Entry{StringId::HdrOutputBrightness, L"输出亮度", L"Output brightness"},
    Entry{StringId::HdrHighlightCompression, L"高光压缩", L"Highlight compression"},
    Entry{StringId::StartCalibration, L"开始校准", L"Start calibration"},
    Entry{StringId::CalibrationInstructions, L"拖动参数并观察全屏画面的实时变化", L"Drag the controls and watch the full-screen preview update"},
    Entry{StringId::ResetCalibration, L"恢复默认", L"Reset"},
    Entry{StringId::Apply, L"应用", L"Apply"},
    Entry{StringId::HdrCalibrationUnavailable, L"当前桌面未检测到已启用 HDR 的屏幕。", L"No HDR-enabled display was detected on the current desktop."},
    Entry{StringId::HotkeyUnavailable, L"快捷键已被其他程序占用，请在设置中修改。", L"The hotkey is already in use. Change it in Settings."},
    Entry{StringId::CaptureFailed, L"截图失败，请重试。", L"Capture failed. Please try again."},
    Entry{StringId::SaveFailed, L"保存失败，截图仍保留，可再次尝试。", L"Save failed. The capture is still available."},
    Entry{StringId::ClipboardFailed, L"无法写入剪贴板，截图仍保留。", L"Could not write to the clipboard. The capture is still available."},
    Entry{StringId::Unsupported, L"此设备不支持 Windows 图形捕获。", L"Windows Graphics Capture is not supported on this device."},
    Entry{StringId::Saved, L"截图已保存", L"Screenshot saved"}, Entry{StringId::Copied, L"截图已复制", L"Screenshot copied"},
    Entry{StringId::PngDescription, L"PNG 图像（SDR）", L"PNG image (SDR)"}, Entry{StringId::JxrDescription, L"JPEG XR 图像（HDR）", L"JPEG XR image (HDR)"}
};
}

Language ResolveLanguage(Language configured) noexcept {
    if (configured != Language::Automatic) return configured;
    ULONG languageCount{};
    ULONG bufferLength{};
    (void)GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount, nullptr, &bufferLength);
    if (bufferLength > 0) {
        std::vector<wchar_t> languages(bufferLength);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount,
                                        languages.data(), &bufferLength)) {
            for (const wchar_t* language = languages.data(); *language;
                 language += wcslen(language) + 1) {
                if (_wcsnicmp(language, L"zh", 2) == 0 &&
                    (language[2] == L'\0' || language[2] == L'-'))
                    return Language::SimplifiedChinese;
                if (_wcsnicmp(language, L"en", 2) == 0 &&
                    (language[2] == L'\0' || language[2] == L'-'))
                    return Language::English;
            }
        }
    }
    return Language::English;
}

std::wstring_view Localized(StringId id, Language language) noexcept {
    const bool chinese = ResolveLanguage(language) == Language::SimplifiedChinese;
    for (const auto& entry : entries) if (entry.id == id) return chinese ? entry.zh : entry.en;
    return {};
}
} // namespace lumashot
