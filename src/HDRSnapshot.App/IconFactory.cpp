#include "IconFactory.h"
#include <algorithm>
#include <cstdint>

namespace hdrsnapshot {
HICON CreateAppIcon(int size) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header); header.bV5Width = size; header.bV5Height = -size;
    header.bV5Planes = 1; header.bV5BitCount = 32; header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00; header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
    void* raw{};
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &raw, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !raw) return LoadIconW(nullptr, IDI_APPLICATION);
    auto* pixels = static_cast<std::uint32_t*>(raw);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const float t = static_cast<float>(x + y) / std::max(1, size * 2 - 2);
        const auto r = static_cast<std::uint8_t>(35 + t * 145);
        const auto g = static_cast<std::uint8_t>(195 - t * 95);
        const auto b = static_cast<std::uint8_t>(245 - t * 20);
        const int edge = std::max(2, size / 8), arm = std::max(5, size / 3);
        const bool corner = ((x < edge && (y < arm || y >= size - arm)) || (x >= size - edge && (y < arm || y >= size - arm)) ||
                             (y < edge && (x < arm || x >= size - arm)) || (y >= size - edge && (x < arm || x >= size - arm)));
        pixels[static_cast<std::size_t>(y) * size + x] = corner ? 0xffffffffu : (0xff000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b);
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask); DeleteObject(color);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}
} // namespace hdrsnapshot

