using System.Text.Json;
using LumaShot_ControlCenter.Models;

namespace LumaShot_ControlCenter.Services;

internal sealed class SettingsService
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
    };

    private readonly string _settingsPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "LumaShot",
        "settings.json");

    public LumaShotSettings Load()
    {
        try
        {
            if (!File.Exists(_settingsPath))
            {
                return new LumaShotSettings();
            }

            LumaShotSettings? settings = JsonSerializer.Deserialize<LumaShotSettings>(
                File.ReadAllText(_settingsPath),
                SerializerOptions);
            if (settings is null || settings.SchemaVersion is < 1 or > 4)
            {
                return new LumaShotSettings();
            }

            settings.Normalize();
            return settings;
        }
        catch (JsonException)
        {
            return new LumaShotSettings();
        }
        catch (IOException)
        {
            return new LumaShotSettings();
        }
        catch (UnauthorizedAccessException)
        {
            return new LumaShotSettings();
        }
    }

    public async Task SaveAsync(LumaShotSettings settings)
    {
        settings.Normalize();
        string? directory = Path.GetDirectoryName(_settingsPath);
        if (string.IsNullOrEmpty(directory))
        {
            throw new IOException("The settings directory could not be resolved.");
        }

        Directory.CreateDirectory(directory);
        string temporaryPath = _settingsPath + ".tmp";
        string json = JsonSerializer.Serialize(settings, SerializerOptions) + Environment.NewLine;
        await File.WriteAllTextAsync(temporaryPath, json).ConfigureAwait(false);
        File.Move(temporaryPath, _settingsPath, true);
    }
}
