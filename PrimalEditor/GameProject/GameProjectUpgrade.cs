// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
using PrimalEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;

namespace PrimalEditor.GameProject;

/// <summary>
/// Brings the Visual C++ project of a game made with an older editor up to what the current project
/// templates generate, and reports scripts written for the old script API.
/// </summary>
static class GameProjectUpgrade
{
    // Keep these in step with Resources\ProjectTemplates\*\MSVCProject and NewProject.CreateMSVCSolution().
    public const string PlatformToolset = "v145";
    public const string LanguageStandard = "stdcpp23";
    public const string WindowsSdk = "10.0";
    public const string IncludePath = @"$(PRIMAL_ENGINE)Engine\EngineAPI\";
    public const string LibraryPath = @"$(PRIMAL_ENGINE)$(Platform)\$(Configuration)\";
    public const string VulkanImport =
        @"<Import Project=""$(PRIMAL_ENGINE)Engine\Vulkan.props"" Condition=""Exists('$(PRIMAL_ENGINE)Engine\Vulkan.props')"" />";

    private const string _engineMacro = "$(PRIMAL_ENGINE)";

    private static readonly Regex _compileSettings = new(@"<ClCompile>(?<body>.*?)(?<indent>\r?\n[ \t]*)</ClCompile>", RegexOptions.Singleline);
    private static readonly Regex _pinnedSdk = new(@"(<WindowsTargetPlatformVersion>)(10\.0\.[0-9.]+)(</WindowsTargetPlatformVersion>)");
    private static readonly Regex _cppTargets = new(@"(?<indent>[ \t]*)<Import\s+Project=""\$\(VCTargetsPath\)\\Microsoft\.Cpp\.targets""");
    private static readonly Regex _oldScriptApi = new(@"\bPrimal::Script::|\bBeginPlay\s*\(");

    /// <summary>
    /// Returns the text of a game's .vcxproj with everything outdated replaced, and adds one line to
    /// <paramref name="changes"/> for each kind of change. No changes means the project is current.
    /// </summary>
    public static string UpgradeProjectFile(string text, List<string> changes)
    {
        text = SetElement(text, "PlatformToolset", PlatformToolset, x => x != PlatformToolset, "Platform toolset", changes);
        text = SetElement(text, "LanguageStandard", LanguageStandard, x => x != LanguageStandard, "C++ language standard", changes);

        // Compiler settings without a language standard use the compiler's default (C++14).
        var addedStandard = false;
        text = _compileSettings.Replace(text, m =>
        {
            if (m.Groups["body"].Value.Contains("<LanguageStandard>")) return m.Value;
            addedStandard = true;
            var indent = m.Groups["indent"].Value;
            return $"<ClCompile>{m.Groups["body"].Value}{indent}  <LanguageStandard>{LanguageStandard}</LanguageStandard>{indent}</ClCompile>";
        });
        if (addedStandard) changes.Add($"C++ language standard: not set → {LanguageStandard}");

        string pinnedSdk = null;
        text = _pinnedSdk.Replace(text, m =>
        {
            pinnedSdk = m.Groups[2].Value;
            return m.Groups[1].Value + WindowsSdk + m.Groups[3].Value;
        });
        if (pinnedSdk != null) changes.Add($"Windows SDK: {pinnedSdk} → {WindowsSdk} (the newest one installed)");

        bool isHardcoded(string x) => !x.TrimStart().StartsWith(_engineMacro, StringComparison.OrdinalIgnoreCase);
        text = SetElement(text, "Primal_IncludePath", IncludePath, isHardcoded, "Engine include folder", changes);
        text = SetElement(text, "Primal_LibraryPath", LibraryPath, isHardcoded, "Engine library folder", changes);

        if (!text.Contains(@"Engine\Vulkan.props", StringComparison.OrdinalIgnoreCase))
        {
            var newLine = text.Contains("\r\n") ? "\r\n" : "\n";
            var added = false;
            text = _cppTargets.Replace(text, m =>
            {
                added = true;
                return m.Groups["indent"].Value + VulkanImport + newLine + m.Value;
            }, 1);
            if (added) changes.Add(@"Import of Engine\Vulkan.props added (needed when the engine is built with Vulkan)");
        }

        return text;
    }

    /// <summary>
    /// Returns the source files under <paramref name="gameCodePath"/> that use the old script API.
    /// </summary>
    public static List<string> FindOldApiScripts(string gameCodePath)
    {
        var files = new List<string>();
        if (!Directory.Exists(gameCodePath)) return files;

        foreach (var file in Directory.EnumerateFiles(gameCodePath, "*.*", SearchOption.AllDirectories))
        {
            var extension = Path.GetExtension(file).ToLowerInvariant();
            if (extension != ".h" && extension != ".cpp") continue;
            if (_oldScriptApi.IsMatch(File.ReadAllText(file))) files.Add(file);
        }

        return files;
    }

    /// <summary>
    /// Checks the game project in <paramref name="projectPath"/> before it is loaded. Offers to update an
    /// outdated .vcxproj (the old file is kept next to it) and logs a warning for each old-API script.
    /// </summary>
    public static void CheckOnLoad(string projectPath, string projectName)
    {
        try
        {
            var gameCodePath = $@"{projectPath}GameCode\";
            var projectFile = $"{gameCodePath}{projectName}.vcxproj";
            if (File.Exists(projectFile)) UpdateProjectFile(projectFile, projectName);

            foreach (var file in FindOldApiScripts(gameCodePath))
            {
                Logger.Log(MessageType.Warning,
                    $"{Path.GetRelativePath(projectPath, file)} uses the old script API and won't compile. " +
                    "Derive from primal::script::EntityScript and override begin_play() and update(float dt).");
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
            Logger.Log(MessageType.Error, $"Failed to check the game project of {projectName}");
        }
    }

    private static void UpdateProjectFile(string projectFile, string projectName)
    {
        var bytes = File.ReadAllBytes(projectFile);
        var hasBom = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
        var changes = new List<string>();
        var upgraded = UpgradeProjectFile(File.ReadAllText(projectFile), changes);
        if (changes.Count == 0) return;

        var fileName = Path.GetFileName(projectFile);
        var backup = $"{projectFile}.{DateTime.Now:yyyyMMdd-HHmmss}.bak";
        var message = $"{fileName} was made by an older version of the editor and may not build with this engine.\n\n" +
            string.Join("\n", changes.Select(x => $"  •  {x}")) +
            $"\n\nUpdate it now? The current file is kept as {Path.GetFileName(backup)}.";

        if (MessageBox.Show(message, $"Update {projectName}", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
        {
            Logger.Log(MessageType.Warning, $"{fileName} is outdated and was not updated: {string.Join("; ", changes)}");
            return;
        }

        File.Copy(projectFile, backup);
        File.WriteAllText(projectFile, upgraded, new UTF8Encoding(hasBom));
        Logger.Log(MessageType.Info, $"{fileName} updated ({string.Join("; ", changes)}). The old file is {Path.GetFileName(backup)}.");
    }

    private static string SetElement(string text, string element, string value, Func<string, bool> isOutdated, string label, List<string> changes)
    {
        var oldValues = new List<string>();
        text = Regex.Replace(text, $"(<{element}>)([^<]*)(</{element}>)", m =>
        {
            if (!isOutdated(m.Groups[2].Value)) return m.Value;
            oldValues.Add(m.Groups[2].Value.Trim());
            return m.Groups[1].Value + value + m.Groups[3].Value;
        });

        if (oldValues.Count > 0) changes.Add($"{label}: {string.Join(", ", oldValues.Distinct())} → {value}");
        return text;
    }
}
