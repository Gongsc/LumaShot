using System.ComponentModel;
using System.Runtime.InteropServices;
using LumaShot_ControlCenter.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;

namespace LumaShot_ControlCenter;

public sealed partial class MainPage : Page
{
    private const uint ModAlt = 0x0001;
    private const uint ModControl = 0x0002;
    private const uint ModShift = 0x0004;
    private const uint ModWindows = 0x0008;

    public MainPageViewModel ViewModel { get; } = new();

    public MainPage()
    {
        InitializeComponent();
        ViewModel.PropertyChanged += ViewModel_PropertyChanged;
        PageRoot.ActualThemeChanged += PageRoot_ActualThemeChanged;
    }

    private async void Page_Loaded(object sender, RoutedEventArgs e)
    {
        UpdateModeSelection();
        UpdateStatusDot();
        await ViewModel.InitializeAsync();
    }

    private void ModeCard_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string mode })
        {
            ViewModel.SelectMode(mode);
        }
    }

    private void HotkeyBox_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key is VirtualKey.Control or VirtualKey.Shift or VirtualKey.Menu or
            VirtualKey.LeftWindows or VirtualKey.RightWindows)
        {
            return;
        }

        uint modifiers = 0;
        if (IsKeyPressed(0x11)) modifiers |= ModControl;
        if (IsKeyPressed(0x10)) modifiers |= ModShift;
        if (IsKeyPressed(0x12)) modifiers |= ModAlt;
        if (IsKeyPressed(0x5B) || IsKeyPressed(0x5C)) modifiers |= ModWindows;
        if (modifiers == 0 || modifiers == ModShift)
        {
            modifiers = ModControl | ModShift;
        }

        uint virtualKey = (uint)e.Key;
        ViewModel.UpdateHotkey(modifiers, virtualKey, FormatHotkey(modifiers, e.Key));
        e.Handled = true;
    }

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ViewModel.SelectedCaptureModeId))
        {
            UpdateModeSelection();
        }
        else if (e.PropertyName == nameof(ViewModel.IsBackendReady))
        {
            UpdateStatusDot();
        }
    }

    private void PageRoot_ActualThemeChanged(FrameworkElement sender, object args)
    {
        UpdateModeSelection();
        UpdateStatusDot();
    }

    private void UpdateModeSelection()
    {
        UpdateModeButton(RegionModeButton, ViewModel.IsRegionSelected);
        UpdateModeButton(WindowModeButton, ViewModel.IsWindowSelected);
        UpdateModeButton(MonitorModeButton, ViewModel.IsMonitorSelected);
        UpdateModeButton(DesktopModeButton, ViewModel.IsVirtualDesktopSelected);
    }

    private static void UpdateModeButton(Button button, bool selected)
    {
        string backgroundKey = selected
            ? "CaptureModeSelectedBackgroundBrush"
            : "CardBackgroundFillColorDefaultBrush";
        string borderKey = selected
            ? "CaptureModeSelectedBorderBrush"
            : "CardStrokeColorDefaultBrush";
        button.Background = (Brush)Application.Current.Resources[backgroundKey];
        button.BorderBrush = (Brush)Application.Current.Resources[borderKey];
        button.BorderThickness = selected ? new Thickness(2) : new Thickness(1);
    }

    private void UpdateStatusDot()
    {
        string key = ViewModel.IsBackendReady ? "StatusReadyBrush" : "StatusUnavailableBrush";
        BackendStatusDot.Fill = (Brush)Application.Current.Resources[key];
    }

    private static bool IsKeyPressed(int virtualKey)
    {
        return (GetKeyState(virtualKey) & 0x8000) != 0;
    }

    private static string FormatHotkey(uint modifiers, VirtualKey key)
    {
        List<string> parts = [];
        if ((modifiers & ModControl) != 0) parts.Add("Ctrl");
        if ((modifiers & ModShift) != 0) parts.Add("Shift");
        if ((modifiers & ModAlt) != 0) parts.Add("Alt");
        if ((modifiers & ModWindows) != 0) parts.Add("Win");
        parts.Add(key == VirtualKey.Snapshot ? "Print Screen" : key.ToString());
        return string.Join(" + ", parts);
    }

    [LibraryImport("user32.dll")]
    private static partial short GetKeyState(int virtualKey);
}
