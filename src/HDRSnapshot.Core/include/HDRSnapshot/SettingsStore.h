#pragma once
#include "Types.h"
#include <filesystem>

namespace hdrsnapshot {

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path = DefaultPath());
    [[nodiscard]] AppSettings load() const noexcept;
    [[nodiscard]] bool save(const AppSettings& settings) const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] static std::filesystem::path DefaultPath();

private:
    std::filesystem::path path_;
};

} // namespace hdrsnapshot

