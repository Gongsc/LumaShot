#pragma once
#include "ColorPipeline.h"
#include <HDRSnapshot/Types.h>
#include <filesystem>
#include <optional>

namespace hdrsnapshot {
struct SaveChoice { std::filesystem::path path; ExportFormat format; };

class ImageExporter {
public:
    [[nodiscard]] static std::optional<SaveChoice> showSaveDialog(HWND owner, bool defaultHdr, Language language);
    static void savePng(const std::filesystem::path& path, const ImageBgra8& image);
    static void saveJxr(const std::filesystem::path& path, const ImageF16& image);
    [[nodiscard]] static std::vector<std::uint8_t> encodePng(const ImageBgra8& image);
};
} // namespace hdrsnapshot

