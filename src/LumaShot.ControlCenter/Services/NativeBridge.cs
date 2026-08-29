using System.Diagnostics;
using System.Runtime.InteropServices;

namespace LumaShot_ControlCenter.Services;

internal sealed partial class NativeBridge
{
    private const string BackendResourceName = "LumaShot.Native.exe";
    private const uint CaptureModeMessage = 0x8000 + 74;
    private const uint ReloadSettingsMessage = 0x8000 + 75;
    private const uint BeginCalibrationMessage = 0x8000 + 76;
    private const string BackendWindowClass = "LumaShot.MessageWindow";

    public bool IsBackendAvailable => FindWindow(BackendWindowClass, null) != nint.Zero;

    internal static bool VerifyBundledBackend()
    {
        return ExtractBundledBackend() is string path && File.Exists(path);
    }

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
            ProcessStartInfo startInfo = new()
            {
                FileName = executable,
                WorkingDirectory = Path.GetDirectoryName(executable)!,
                UseShellExecute = false,
            };
            if (Environment.ProcessPath is string controlCenterExecutable)
            {
                startInfo.Environment["LUMASHOT_CONTROL_CENTER"] = controlCenterExecutable;
            }

            Process.Start(startInfo);
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
        if (ExtractBundledBackend() is string bundledBackend)
        {
            return bundledBackend;
        }

        HashSet<string> visited = new(StringComparer.OrdinalIgnoreCase);
        foreach (string origin in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            DirectoryInfo? directory = new(origin);
            for (int depth = 0; directory is not null && depth < 9; depth++, directory = directory.Parent)
            {
                foreach (string candidate in new[]
                {
                    Path.Combine(directory.FullName, "LumaShot.Native.exe"),
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

    private static string? ExtractBundledBackend()
    {
        try
        {
            using Stream? resource = typeof(NativeBridge).Assembly.GetManifestResourceStream(BackendResourceName);
            if (resource is null)
            {
                return null;
            }

            string version = typeof(NativeBridge).Assembly.GetName().Version?.ToString() ?? "current";
            string runtimeRoot = Environment.GetEnvironmentVariable("LUMASHOT_RUNTIME_ROOT")
                ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LumaShot", "runtime");
            string runtimeDirectory = Path.Combine(runtimeRoot, version);
            string backendPath = Path.Combine(runtimeDirectory, BackendResourceName);

            Directory.CreateDirectory(runtimeDirectory);
            if (File.Exists(backendPath) && new FileInfo(backendPath).Length == resource.Length)
            {
                return backendPath;
            }

            string temporaryPath = Path.Combine(runtimeDirectory, $"{BackendResourceName}.{Environment.ProcessId}.{Guid.NewGuid():N}.tmp");
            try
            {
                using (FileStream output = new(temporaryPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    resource.CopyTo(output);
                    output.Flush(flushToDisk: true);
                }

                File.Move(temporaryPath, backendPath, overwrite: true);
            }
            finally
            {
                if (File.Exists(temporaryPath))
                {
                    File.Delete(temporaryPath);
                }
            }

            return backendPath;
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
    }

    [LibraryImport("user32.dll", EntryPoint = "FindWindowW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindow(string className, string? windowName);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);
}
