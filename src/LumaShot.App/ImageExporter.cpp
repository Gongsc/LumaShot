#include "ImageExporter.h"
#include <LumaShot/Localization.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace lumashot {
namespace {

void CheckHr(HRESULT value, const char* message) {
    if (SUCCEEDED(value)) return;
    std::ostringstream text;
    text << message << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(value) << ')';
    throw std::runtime_error(text.str());
}

std::filesystem::path TemporaryPathFor(const std::filesystem::path& target) {
    GUID id{};
    CheckHr(CoCreateGuid(&id), "Could not create a temporary output name");
    wchar_t text[40]{};
    if (StringFromGUID2(id, text, ARRAYSIZE(text)) == 0)
        throw std::runtime_error("Could not format a temporary output name");
    return target.parent_path() / (target.filename().wstring() + L"." + text + L".tmp");
}

class TemporaryOutput final {
public:
    explicit TemporaryOutput(const std::filesystem::path& target) : path_(TemporaryPathFor(target)) {}
    ~TemporaryOutput() { if (!committed_) DeleteFileW(path_.c_str()); }
    TemporaryOutput(const TemporaryOutput&) = delete;
    TemporaryOutput& operator=(const TemporaryOutput&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    void commitTo(const std::filesystem::path& target) {
        // A same-directory rename keeps the final path unchanged until encoding has
        // succeeded. MOVEFILE_REPLACE_EXISTING also works on destinations that do
        // not support ReplaceFileW (for example some temporary/test volumes).
        if (!MoveFileExW(path_.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            CheckHr(HRESULT_FROM_WIN32(GetLastError()), "Could not commit output file");
        committed_ = true;
    }

private:
    std::filesystem::path path_;
    bool committed_{};
};

ComPtr<IWICImagingFactory> Factory() {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) throw std::runtime_error("WIC is unavailable");
    return factory;
}

std::wstring SuggestedName() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::wostringstream output;
    output << L"LumaShot_" << std::put_time(&local, L"%Y%m%d_%H%M%S");
    return output.str();
}

void Encode(IStream* stream, REFGUID container, REFGUID pixelFormat, UINT width, UINT height,
            UINT stride, UINT byteCount, BYTE* pixels, bool lossless, bool srgb) {
    auto factory = Factory();
    ComPtr<IWICBitmapEncoder> encoder;
    CheckHr(factory->CreateEncoder(container, nullptr, &encoder), "Could not create image encoder");
    CheckHr(encoder->Initialize(stream, WICBitmapEncoderNoCache), "Could not initialize image encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    CheckHr(encoder->CreateNewFrame(&frame, &properties), "Could not create image frame");
    if (lossless && properties) {
        PROPBAG2 options[2]{};
        options[0].pstrName = const_cast<wchar_t*>(L"Lossless");
        options[1].pstrName = const_cast<wchar_t*>(L"InterleavedAlpha");
        VARIANT values[2]{};
        for (auto& value : values) { VariantInit(&value); value.vt = VT_BOOL; value.boolVal = VARIANT_TRUE; }
        properties->Write(2, options, values);
        for (auto& value : values) VariantClear(&value);
    }
    CheckHr(frame->Initialize(properties.Get()), "Could not initialize image frame");
    CheckHr(frame->SetSize(width, height), "Could not set image size");
    CheckHr(frame->SetResolution(96.0, 96.0), "Could not set image resolution");
    GUID actual = pixelFormat;
    CheckHr(frame->SetPixelFormat(&actual), "Could not set pixel format");
    if (actual != pixelFormat) throw std::runtime_error("Requested pixel format is unsupported");
    if (srgb) {
        ComPtr<IWICMetadataQueryWriter> metadata;
        if (SUCCEEDED(frame->GetMetadataQueryWriter(&metadata))) {
            PROPVARIANT value{};
            PropVariantInit(&value);
            value.vt = VT_UI1;
            value.bVal = 0;
            (void)metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &value);
            PropVariantClear(&value);
        }
    }
    CheckHr(frame->WritePixels(height, stride, byteCount, pixels), "Could not write image pixels");
    CheckHr(frame->Commit(), "Could not commit image frame");
    CheckHr(encoder->Commit(), "Could not commit image encoder");
}

void Save(const std::filesystem::path& path, REFGUID container, REFGUID pixelFormat, UINT width, UINT height,
          UINT stride, UINT byteCount, BYTE* pixels, bool lossless, bool srgb) {
    TemporaryOutput output(path);
    auto factory = Factory();
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(output.path().c_str(), GENERIC_WRITE)))
        throw std::runtime_error("Could not open temporary output file");
    Encode(stream.Get(), container, pixelFormat, width, height, stride, byteCount, pixels, lossless, srgb);
    stream.Reset();
    output.commitTo(path);
}

} // namespace

std::optional<SaveChoice> ImageExporter::showSaveDialog(HWND owner, bool defaultHdr, Language language) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("File dialog is unavailable");
    const std::wstring pngName(Localized(StringId::PngDescription, language));
    const std::wstring jxrName(Localized(StringId::JxrDescription, language));
    COMDLG_FILTERSPEC filters[]{{pngName.c_str(), L"*.png"}, {jxrName.c_str(), L"*.jxr"}};
    CheckHr(dialog->SetFileTypes(2, filters), "Could not configure file types");
    CheckHr(dialog->SetFileTypeIndex(defaultHdr ? 2 : 1), "Could not select a file type");
    CheckHr(dialog->SetDefaultExtension(defaultHdr ? L"jxr" : L"png"), "Could not set the default extension");
    FILEOPENDIALOGOPTIONS options{};
    CheckHr(dialog->GetOptions(&options), "Could not read file dialog options");
    CheckHr(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_STRICTFILETYPES | FOS_OVERWRITEPROMPT),
            "Could not configure file dialog options");
    const auto name = SuggestedName();
    CheckHr(dialog->SetFileName(name.c_str()), "Could not set the suggested file name");
    const HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
    if (FAILED(shown)) throw std::runtime_error("Could not show file dialog");
    UINT index{};
    CheckHr(dialog->GetFileTypeIndex(&index), "Could not read selected file type");
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("Could not read selected file");
    PWSTR raw{};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) throw std::runtime_error("Could not resolve selected file");
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    const auto format = index == 2 ? ExportFormat::JpegXrHdr : ExportFormat::PngSdr;
    return SaveChoice{path, format};
}

void ImageExporter::savePng(const std::filesystem::path& path, const ImageBgra8& image) {
    Save(path, GUID_ContainerFormatPng, GUID_WICPixelFormat32bppBGRA, image.width, image.height, image.width * 4,
         static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()), false, true);
}

void ImageExporter::saveJxr(const std::filesystem::path& path, const ImageF16& image) {
    // Windows' JPEG XR codec advertises RGBA Half support, but some servicing
    // levels reject it at WritePixels. Prefer the compact half-float path and
    // transparently retry with the codec's lossless 32-bit-float path.
    try {
        Save(path, GUID_ContainerFormatWmp, GUID_WICPixelFormat64bppRGBAHalf, image.width, image.height, image.width * 8,
             static_cast<UINT>(image.rgba.size() * sizeof(std::uint16_t)), reinterpret_cast<BYTE*>(const_cast<std::uint16_t*>(image.rgba.data())), true, false);
        return;
    } catch (...) {
    }
    std::vector<float> rgba32(image.rgba.size());
    for (std::size_t index = 0; index < image.rgba.size(); ++index) rgba32[index] = ColorPipeline::halfToFloat(image.rgba[index]);
    Save(path, GUID_ContainerFormatWmp, GUID_WICPixelFormat128bppRGBAFloat, image.width, image.height, image.width * 16,
         static_cast<UINT>(rgba32.size() * sizeof(float)), reinterpret_cast<BYTE*>(rgba32.data()), true, false);
}

std::vector<std::uint8_t> ImageExporter::encodePng(const ImageBgra8& image) {
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) throw std::runtime_error("Could not create memory stream");
    Encode(stream.Get(), GUID_ContainerFormatPng, GUID_WICPixelFormat32bppBGRA, image.width, image.height, image.width * 4,
           static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()), false, true);
    HGLOBAL memory{};
    if (FAILED(GetHGlobalFromStream(stream.Get(), &memory))) throw std::runtime_error("Could not access encoded PNG");
    STATSTG stats{};
    if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || stats.cbSize.HighPart != 0) throw std::runtime_error("Could not determine encoded PNG size");
    const SIZE_T size = stats.cbSize.LowPart;
    const void* data = GlobalLock(memory);
    if (!data) throw std::runtime_error("Could not lock encoded PNG");
    std::vector<std::uint8_t> result(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
    GlobalUnlock(memory);
    return result;
}

} // namespace lumashot
