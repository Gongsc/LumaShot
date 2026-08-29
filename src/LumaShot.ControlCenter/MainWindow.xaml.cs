using Microsoft.UI;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace LumaShot_ControlCenter;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        AppWindow.SetIcon("Assets/AppIcon.ico");
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;

        nint windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        double scale = GetDpiForWindow(windowHandle) / 96.0;
        AppWindow.Resize(new SizeInt32(
            (int)Math.Round(1180 * scale),
            (int)Math.Round(800 * scale)));
        RootFrame.Navigate(typeof(MainPage));
    }

    [System.Runtime.InteropServices.LibraryImport("user32.dll")]
    private static partial uint GetDpiForWindow(nint window);
}
