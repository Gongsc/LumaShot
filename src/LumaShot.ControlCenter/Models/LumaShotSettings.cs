using System.Text.Json.Serialization;

namespace LumaShot_ControlCenter.Models;

internal sealed class LumaShotSettings
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; set; } = 4;

    [JsonPropertyName("language")]
    public string Language { get; set; } = "auto";

    [JsonPropertyName("hotkeyModifiers")]
    public uint HotkeyModifiers { get; set; } = 0x0002 | 0x0004;

    [JsonPropertyName("hotkeyVirtualKey")]
    public uint HotkeyVirtualKey { get; set; } = 0x2C;

    [JsonPropertyName("lastCaptureMode")]
    public string LastCaptureMode { get; set; } = "region";

    [JsonPropertyName("includeCursor")]
    public bool IncludeCursor { get; set; }

    [JsonPropertyName("copyOnEnter")]
    public bool CopyOnEnter { get; set; } = true;

    [JsonPropertyName("launchAtLogin")]
    public bool LaunchAtLogin { get; set; }

    [JsonPropertyName("hdrOutputBrightnessPercent")]
    public int HdrOutputBrightnessPercent { get; set; } = 100;

    [JsonPropertyName("hdrHighlightCompressionPercent")]
    public int HdrHighlightCompressionPercent { get; set; }

    public void Normalize()
    {
        SchemaVersion = 4;
        Language = Language is "zh-CN" or "en-US" ? Language : "auto";
        LastCaptureMode = LastCaptureMode is "window" or "monitor" or "virtualDesktop"
            ? LastCaptureMode
            : "region";
        HotkeyModifiers &= 0x0001 | 0x0002 | 0x0004 | 0x0008;
        HotkeyVirtualKey = HotkeyVirtualKey is > 0 and <= 0xFE ? HotkeyVirtualKey : 0x2C;
        HdrOutputBrightnessPercent = Math.Clamp(HdrOutputBrightnessPercent, 40, 110);
        HdrHighlightCompressionPercent = Math.Clamp(HdrHighlightCompressionPercent, 0, 100);
    }
}
