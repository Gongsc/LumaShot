using System.Diagnostics;
using System.Runtime.InteropServices;

namespace LumaShot_ControlCenter.Services;

internal sealed partial class NativeBridge
{
    private const uint CaptureModeMessage = 0x8000 + 74;
    private const uint ReloadSettingsMessage = 0x8000 + 75;
    private const uint BeginCalibrationMessage = 0x8000 + 76;
    private const string BackendWindowClass = "LumaShot.MessageWindow";

    public bool IsBackendAvailable => FindWindow(BackendWindowClass, null) != nint.Zero;

    public async Task<bool> EnsureBackendAsync()
    {
        if (IsBackendAvailable)
        {
            return true;
        }

        string? executable = FindBackendExecutable();
        if (executable is null)
        {
            return false;
        }

        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = Path.GetDirectoryName(executable)!,
                UseShellExecute = true,
            });
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return false;
        }

        for (int attempt = 0; attempt < 24; attempt++)
        {
            await Task.Delay(100);
            if (IsBackendAvailable)
            {
                return true;
            }
        }

        return false;
    }

    public bool Capture(int mode)
    {
        return Post(CaptureModeMessage, (nuint)mode);
    }

    public bool BeginCalibration()
    {
        return Post(BeginCalibrationMessage, 0);
    }

    public async Task<bool> ReloadSettingsAsync()
    {
        return await PostAsync(ReloadSettingsMessage, 0);
    }

    private async Task<bool> PostAsync(uint message, nuint parameter)
    {
        if (!await EnsureBackendAsync())
        {
            return false;
        }

        return Post(message, parameter);
    }

    private static bool Post(uint message, nuint parameter)
    {
        nint window = FindWindow(BackendWindowClass, null);
        return window != nint.Zero && PostMessage(window, message, parameter, 0);
    }

    private static string? FindBackendExecutable()
    {
        HashSet<string> visited = new(StringComparer.OrdinalIgnoreCase);
        foreach (string origin in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            DirectoryInfo? directory = new(origin);
            for (int depth = 0; directory is not null && depth < 9; depth++, directory = directory.Parent)
            {
                foreach (string candidate in new[]
                {
                    Path.Combine(directory.FullName, "LumaShot.exe"),
                    Path.Combine(directory.FullName, "bin", "Debug", "LumaShot.exe"),
                    Path.Combine(directory.FullName, "bin", "Release", "LumaShot.exe"),
                })
                {
                    if (visited.Add(candidate) && File.Exists(candidate))
                    {
                        return candidate;
                    }
                }
            }
        }

        return null;
    }

    [LibraryImport("user32.dll", EntryPoint = "FindWindowW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindow(string className, string? windowName);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);
}
