#include "IconFactory.h"
#include "resource.h"

namespace lumashot {
HICON CreateAppIcon(int size) {
    const HMODULE module = GetModuleHandleW(nullptr);
    if (HICON icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(IDI_LUMASHOT_TRAY),
                                                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR))) return icon;
    if (HICON icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(IDI_LUMASHOT),
                                                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR))) return icon;
    return LoadIconW(nullptr, IDI_APPLICATION);
}
} // namespace lumashot
