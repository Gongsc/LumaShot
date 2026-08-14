#include "ClipboardService.h"
#include "ImageExporter.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace hdrsnapshot {
namespace {

std::vector<std::uint8_t> MakeBitmapBytes(const void* header, std::size_t headerSize, const ImageBgra8& image, bool bottomUp) {
    std::vector<std::uint8_t> bytes(headerSize + image.pixels.size());
    memcpy(bytes.data(), header, headerSize);
    const std::size_t row = static_cast<std::size_t>(image.width) * 4;
    for (UINT y = 0; y < image.height; ++y) {
        const UINT sourceY = bottomUp ? image.height - 1 - y : y;
        memcpy(bytes.data() + headerSize + static_cast<std::size_t>(y) * row,
               image.pixels.data() + static_cast<std::size_t>(sourceY) * row, row);
    }
    return bytes;
}

HGLOBAL MakeBlock(const std::vector<std::uint8_t>& bytes) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return nullptr;
    auto* target = static_cast<std::uint8_t*>(GlobalLock(memory));
    if (!target) { GlobalFree(memory); return nullptr; }
    memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(memory);
    return memory;
}

} // namespace

ClipboardBitmapPayload ClipboardService::buildBitmapPayload(const ImageBgra8& image) {
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5); v5.bV5Width = static_cast<LONG>(image.width); v5.bV5Height = -static_cast<LONG>(image.height);
    v5.bV5Planes = 1; v5.bV5BitCount = 32; v5.bV5Compression = BI_BITFIELDS; v5.bV5SizeImage = image.width * image.height * 4;
    v5.bV5RedMask = 0x00ff0000; v5.bV5GreenMask = 0x0000ff00; v5.bV5BlueMask = 0x000000ff; v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = LCS_sRGB; v5.bV5Intent = LCS_GM_IMAGES;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof(dib); dib.biWidth = static_cast<LONG>(image.width); dib.biHeight = static_cast<LONG>(image.height);
    dib.biPlanes = 1; dib.biBitCount = 32; dib.biCompression = BI_RGB; dib.biSizeImage = image.width * image.height * 4;
    return {MakeBitmapBytes(&v5, sizeof(v5), image, false), MakeBitmapBytes(&dib, sizeof(dib), image, true)};
}

bool ClipboardService::copy(HWND owner, const ImageBgra8& image) noexcept {
    try {
        const auto png = ImageExporter::encodePng(image);
        const auto bitmap = buildBitmapPayload(image);
        HGLOBAL v5Block = MakeBlock(bitmap.dibV5);
        HGLOBAL dibBlock = MakeBlock(bitmap.dib);
        HGLOBAL pngBlock = MakeBlock(png);
        if (!v5Block || !dibBlock || !pngBlock) {
            if (v5Block) GlobalFree(v5Block); if (dibBlock) GlobalFree(dibBlock); if (pngBlock) GlobalFree(pngBlock);
            return false;
        }
        bool opened = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (OpenClipboard(owner)) { opened = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(15 * (attempt + 1)));
        }
        if (!opened) { GlobalFree(v5Block); GlobalFree(dibBlock); GlobalFree(pngBlock); return false; }
        if (!EmptyClipboard()) { CloseClipboard(); GlobalFree(v5Block); GlobalFree(dibBlock); GlobalFree(pngBlock); return false; }
        const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        const bool v5Set = SetClipboardData(CF_DIBV5, v5Block) != nullptr;
        const bool dibSet = SetClipboardData(CF_DIB, dibBlock) != nullptr;
        const bool pngSet = SetClipboardData(pngFormat, pngBlock) != nullptr;
        if (v5Set) v5Block = nullptr;
        if (dibSet) dibBlock = nullptr;
        if (pngSet) pngBlock = nullptr;
        CloseClipboard();
        if (v5Block) GlobalFree(v5Block); if (dibBlock) GlobalFree(dibBlock); if (pngBlock) GlobalFree(pngBlock);
        return v5Set && dibSet && pngSet;
    } catch (...) { return false; }
}

} // namespace hdrsnapshot
