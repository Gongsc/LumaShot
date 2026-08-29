using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using LumaShot_ControlCenter.Models;
using LumaShot_ControlCenter.Services;

namespace LumaShot_ControlCenter.ViewModels;

public partial class MainPageViewModel : ObservableObject
{
    private readonly NativeBridge _nativeBridge = new();
    private readonly SettingsService _settingsService = new();
    private readonly SemaphoreSlim _saveLock = new(1, 1);
    private readonly LumaShotSettings _settings;
    private bool _isLoading = true;

    public ObservableCollection<CaptureModeItem> CaptureModes { get; } =
    [
        new("region", "矩形区域", "自由框选", 0),
        new("window", "窗口", "选择应用窗口", 1),
        new("monitor", "当前屏幕", "捕捉所在显示器", 2),
        new("virtualDesktop", "全部屏幕", "覆盖整个桌面", 3),
    ];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsRegionSelected))]
    [NotifyPropertyChangedFor(nameof(IsWindowSelected))]
    [NotifyPropertyChangedFor(nameof(IsMonitorSelected))]
    [NotifyPropertyChangedFor(nameof(IsVirtualDesktopSelected))]
    public partial string SelectedCaptureModeId { get; set; } = "region";

    [ObservableProperty]
    public partial bool IncludeCursor { get; set; }

    [ObservableProperty]
    public partial bool CopyOnEnter { get; set; }

    [ObservableProperty]
    public partial bool LaunchAtLogin { get; set; }

    [ObservableProperty]
    public partial int LanguageIndex { get; set; }

    [ObservableProperty]
    public partial string HotkeyDisplay { get; set; } = "Ctrl + Shift + Print Screen";

    [ObservableProperty]
    public partial string BackendStatus { get; set; } = "正在连接截图服务…";

    [ObservableProperty]
    public partial bool IsBackendReady { get; set; }

    public bool IsRegionSelected => SelectedCaptureModeId == "region";
    public bool IsWindowSelected => SelectedCaptureModeId == "window";
    public bool IsMonitorSelected => SelectedCaptureModeId == "monitor";
    public bool IsVirtualDesktopSelected => SelectedCaptureModeId == "virtualDesktop";

    public MainPageViewModel()
    {
        _settings = _settingsService.Load();
        SelectedCaptureModeId = _settings.LastCaptureMode;
        IncludeCursor = _settings.IncludeCursor;
        CopyOnEnter = _settings.CopyOnEnter;
        LaunchAtLogin = _settings.LaunchAtLogin;
        LanguageIndex = _settings.Language switch
        {
            "zh-CN" => 1,
            "en-US" => 2,
            _ => 0,
        };
        HotkeyDisplay = FormatHotkey(_settings.HotkeyModifiers, _settings.HotkeyVirtualKey);
        _isLoading = false;
    }

    public async Task InitializeAsync()
    {
        IsBackendReady = await _nativeBridge.EnsureBackendAsync();
        BackendStatus = IsBackendReady ? "正在托盘中运行" : "截图服务不可用";
        if (IsBackendReady)
        {
            await _nativeBridge.ReloadSettingsAsync();
        }
    }

    public void SelectMode(string mode)
    {
        if (CaptureModes.Any(item => item.Id == mode))
        {
            SelectedCaptureModeId = mode;
        }
    }

    public void UpdateHotkey(uint modifiers, uint virtualKey, string display)
    {
        _settings.HotkeyModifiers = modifiers;
        _settings.HotkeyVirtualKey = virtualKey;
        HotkeyDisplay = display;
        QueueSave();
    }

    public async Task<bool> CaptureSelectedAsync()
    {
        await SaveAndReloadAsync();
        CaptureModeItem selected = CaptureModes.First(item => item.Id == SelectedCaptureModeId);
        if (_nativeBridge.Capture(selected.NativeValue))
        {
            return true;
        }

        IsBackendReady = false;
        BackendStatus = "截图服务不可用";
        return false;
    }

    public async Task<bool> BeginCalibrationAsync()
    {
        await SaveAndReloadAsync();
        if (_nativeBridge.BeginCalibration())
        {
            return true;
        }

        IsBackendReady = false;
        BackendStatus = "截图服务不可用";
        return false;
    }

    partial void OnSelectedCaptureModeIdChanged(string value)
    {
        _settings.LastCaptureMode = value;
        QueueSave();
    }

    partial void OnIncludeCursorChanged(bool value)
    {
        _settings.IncludeCursor = value;
        QueueSave();
    }

    partial void OnCopyOnEnterChanged(bool value)
    {
        _settings.CopyOnEnter = value;
        QueueSave();
    }

    partial void OnLaunchAtLoginChanged(bool value)
    {
        _settings.LaunchAtLogin = value;
        QueueSave();
    }

    partial void OnLanguageIndexChanged(int value)
    {
        _settings.Language = value switch
        {
            1 => "zh-CN",
            2 => "en-US",
            _ => "auto",
        };
        QueueSave();
    }

    private void QueueSave()
    {
        if (!_isLoading)
        {
            _ = SaveAndReloadAsync();
        }
    }

    private async Task SaveAndReloadAsync()
    {
        await _saveLock.WaitAsync();
        try
        {
            await _settingsService.SaveAsync(_settings);
            IsBackendReady = await _nativeBridge.ReloadSettingsAsync();
            BackendStatus = IsBackendReady ? "正在托盘中运行" : "设置已保存，截图服务未运行";
        }
        catch (IOException)
        {
            BackendStatus = "无法保存设置";
        }
        catch (UnauthorizedAccessException)
        {
            BackendStatus = "没有权限保存设置";
        }
        finally
        {
            _saveLock.Release();
        }
    }

    private static string FormatHotkey(uint modifiers, uint virtualKey)
    {
        List<string> parts = [];
        if ((modifiers & 0x0002) != 0) parts.Add("Ctrl");
        if ((modifiers & 0x0004) != 0) parts.Add("Shift");
        if ((modifiers & 0x0001) != 0) parts.Add("Alt");
        if ((modifiers & 0x0008) != 0) parts.Add("Win");
        parts.Add(FormatVirtualKey(virtualKey));
        return string.Join(" + ", parts);
    }

    private static string FormatVirtualKey(uint virtualKey)
    {
        if (virtualKey is >= 0x30 and <= 0x39 or >= 0x41 and <= 0x5A)
        {
            return ((char)virtualKey).ToString();
        }

        return virtualKey switch
        {
            0x08 => "Backspace",
            0x09 => "Tab",
            0x0D => "Enter",
            0x1B => "Escape",
            0x20 => "Space",
            0x21 => "Page Up",
            0x22 => "Page Down",
            0x23 => "End",
            0x24 => "Home",
            0x25 => "Left",
            0x26 => "Up",
            0x27 => "Right",
            0x28 => "Down",
            0x2C => "Print Screen",
            >= 0x70 and <= 0x87 => $"F{virtualKey - 0x6F}",
            _ => $"0x{virtualKey:X2}",
        };
    }
}
