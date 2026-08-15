#include <LumaShot/SettingsStore.h>
#include <ShlObj.h>
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace lumashot {
namespace {

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::optional<std::string> StringValue(const std::string& json, const char* name) {
    const std::regex expression(std::string{"\""} + name + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    return std::regex_search(json, match, expression) ? std::optional<std::string>{match[1].str()} : std::nullopt;
}

std::optional<unsigned long> UIntValue(const std::string& json, const char* name) {
    const std::regex expression(std::string{"\""} + name + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    try { return std::stoul(match[1].str()); } catch (...) { return std::nullopt; }
}

std::optional<bool> BoolValue(const std::string& json, const char* name) {
    const std::regex expression(std::string{"\""} + name + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    return std::regex_search(json, match, expression) ? std::optional<bool>{match[1].str() == "true"} : std::nullopt;
}

Language ParseLanguage(const std::string& value) {
    if (value == "zh-CN") return Language::SimplifiedChinese;
    if (value == "en-US") return Language::English;
    return Language::Automatic;
}

CaptureMode ParseCaptureMode(const std::string& value) {
    if (value == "window") return CaptureMode::Window;
    if (value == "monitor") return CaptureMode::Monitor;
    if (value == "virtualDesktop") return CaptureMode::VirtualDesktop;
    return CaptureMode::Region;
}

const char* ToString(Language value) {
    switch (value) { case Language::SimplifiedChinese: return "zh-CN"; case Language::English: return "en-US"; default: return "auto"; }
}

const char* ToString(CaptureMode value) {
    switch (value) { case CaptureMode::Window: return "window"; case CaptureMode::Monitor: return "monitor"; case CaptureMode::VirtualDesktop: return "virtualDesktop"; default: return "region"; }
}

} // namespace

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

std::filesystem::path SettingsStore::DefaultPath() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw))) return L"settings.json";
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result / L"LumaShot" / L"settings.json";
}

AppSettings SettingsStore::load() const noexcept {
    AppSettings result;
    try {
        const auto json = ReadAll(path_);
        if (json.empty()) return result;
        const auto schema = UIntValue(json, "schemaVersion");
        if (!schema || *schema < 1 || *schema > AppSettings::CurrentSchemaVersion) return AppSettings{};
        result.schemaVersion = AppSettings::CurrentSchemaVersion;
        if (const auto value = StringValue(json, "language")) result.language = ParseLanguage(*value);
        if (const auto value = UIntValue(json, "hotkeyModifiers")) result.hotkey.modifiers = static_cast<UINT>(*value) | MOD_NOREPEAT;
        if (const auto value = UIntValue(json, "hotkeyVirtualKey")) result.hotkey.virtualKey = static_cast<UINT>(*value);
        if (const auto value = StringValue(json, "lastCaptureMode")) result.lastCaptureMode = ParseCaptureMode(*value);
        if (const auto value = BoolValue(json, "includeCursor")) result.includeCursor = *value;
        if (const auto value = BoolValue(json, "copyOnEnter")) result.copyOnEnter = *value;
        if (const auto value = BoolValue(json, "launchAtLogin")) result.launchAtLogin = *value;
        // Schema 3 changes calibration from a global Reinhard exposure control
        // to an SDR-white-normalized highlight shoulder. Old values are not
        // perceptually equivalent, so migrate them to the new neutral defaults.
        if (*schema >= 3) {
            if (const auto value = UIntValue(json, "hdrOutputBrightnessPercent")) {
                result.hdrCalibration.outputBrightnessPercent = std::clamp(static_cast<int>(*value),
                    HdrCalibration::MinimumOutputBrightness, HdrCalibration::MaximumOutputBrightness);
            }
            if (const auto value = UIntValue(json, "hdrHighlightCompressionPercent")) {
                result.hdrCalibration.highlightCompressionPercent = std::clamp(static_cast<int>(*value),
                    HdrCalibration::MinimumHighlightCompression, HdrCalibration::MaximumHighlightCompression);
            }
        }
    } catch (...) {
        return AppSettings{};
    }
    return result;
}

bool SettingsStore::save(const AppSettings& settings) const noexcept {
    try {
        std::filesystem::create_directories(path_.parent_path());
        const auto temporary = path_.wstring() + L".tmp";
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << "{\n"
               << "  \"schemaVersion\": " << AppSettings::CurrentSchemaVersion << ",\n"
               << "  \"language\": \"" << ToString(settings.language) << "\",\n"
               << "  \"hotkeyModifiers\": " << (settings.hotkey.modifiers & ~MOD_NOREPEAT) << ",\n"
               << "  \"hotkeyVirtualKey\": " << settings.hotkey.virtualKey << ",\n"
               << "  \"lastCaptureMode\": \"" << ToString(settings.lastCaptureMode) << "\",\n"
               << "  \"includeCursor\": " << (settings.includeCursor ? "true" : "false") << ",\n"
               << "  \"copyOnEnter\": " << (settings.copyOnEnter ? "true" : "false") << ",\n"
               << "  \"launchAtLogin\": " << (settings.launchAtLogin ? "true" : "false") << ",\n"
               << "  \"hdrOutputBrightnessPercent\": " << std::clamp(settings.hdrCalibration.outputBrightnessPercent,
                    HdrCalibration::MinimumOutputBrightness, HdrCalibration::MaximumOutputBrightness) << ",\n"
               << "  \"hdrHighlightCompressionPercent\": " << std::clamp(settings.hdrCalibration.highlightCompressionPercent,
                    HdrCalibration::MinimumHighlightCompression, HdrCalibration::MaximumHighlightCompression) << "\n}"
               << std::endl;
        output.close();
        if (!output) return false;
        return MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

} // namespace lumashot
