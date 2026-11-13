// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
using PrimalEditor.GameProject;
using PrimalEditor.Utilities;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace PrimalEditor.GameDev;

// The game code tools without Visual Studio: MSBuild (from Visual Studio or the Build Tools, found with vswhere)
// builds the game project, the .vcxproj file is edited directly to add scripts, new scripts open in VS Code (or the
// program Windows associates with .cpp files), and the game runs as a plain process, without a debugger.
//
// It is used when Visual Studio's automation server (VisualStudio.DTE.18.0) isn't registered. The environment
// variable PRIMAL_EDITOR_BUILD chooses explicitly: "msbuild" for this, "vs" for Visual Studio.
static partial class VisualStudio
{
    private static readonly Lazy<bool> _useCommandLine = new(ChooseCommandLine);
    private static readonly Lazy<string> _msbuildPath = new(FindMSBuild);
    private static readonly Regex _buildMessageRegex = new(@":\s+(?<kind>error|warning)\s", RegexOptions.IgnoreCase);
    private static readonly Regex _projectSuffixRegex = new(@"\s+\[[^\]]+\]$");
    private static Process _gameProcess;

    private static bool UseCommandLine => _useCommandLine.Value;

    private static bool ChooseCommandLine()
    {
        var choice = Environment.GetEnvironmentVariable("PRIMAL_EDITOR_BUILD")?.Trim().ToLowerInvariant();
        bool useCommandLine = choice switch
        {
            "msbuild" => true,
            "vs" => false,
            _ => Type.GetTypeFromProgID(_progID, false) == null,
        };

        Logger.Log(MessageType.Info, useCommandLine
            ? "Game code: building with MSBuild (no Visual Studio automation)."
            : "Game code: building with Visual Studio.");
        return useCommandLine;
    }

    private static string FindMSBuild()
    {
        var programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        var vswhere = Path.Combine(programFilesX86, @"Microsoft Visual Studio\Installer\vswhere.exe");
        if (File.Exists(vswhere))
        {
            var (exitCode, output) = RunAndCapture(vswhere,
                @"-latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe");
            var path = output.Split('\n').Select(x => x.Trim()).FirstOrDefault(File.Exists);
            if (exitCode == 0 && path != null) return path;
        }

        Logger.Log(MessageType.Error, "MSBuild not found: install Visual Studio or the Visual Studio Build Tools (C++ workload).");
        return null;
    }

    private static (int ExitCode, string Output) RunAndCapture(string fileName, string arguments)
    {
        using var process = Process.Start(new ProcessStartInfo(fileName, arguments)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            CreateNoWindow = true,
        });
        var output = process.StandardOutput.ReadToEnd();
        process.WaitForExit();
        return (process.ExitCode, output);
    }

    // ---- adding files -------------------------------------------------------------------------------------------

    private static string FindProjectFile(string solution, string projectName)
    {
        var solutionFolder = Path.GetDirectoryName(solution);
        var expected = Path.Combine(solutionFolder, "GameCode", $"{projectName}.vcxproj");
        if (File.Exists(expected)) return expected;

        return Directory.EnumerateFiles(solutionFolder, $"{projectName}.vcxproj", SearchOption.AllDirectories).FirstOrDefault();
    }

    private static bool AddFilesToSolution_CommandLine(string solution, string projectName, string[] files)
    {
        try
        {
            var projectFile = FindProjectFile(solution, projectName);
            if (projectFile == null)
            {
                Logger.Log(MessageType.Error, $"Project file {projectName}.vcxproj not found next to {solution}");
                return false;
            }

            AddToProjectFile(projectFile, files);
            var filters = projectFile + ".filters";
            if (File.Exists(filters)) AddToProjectFile(filters, files);

            OpenInCodeEditor(Path.GetDirectoryName(solution), files.FirstOrDefault(x => Path.GetExtension(x) == ".cpp"));
            return true;
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
            Logger.Log(MessageType.Error, $"Failed to add files to {projectName}.vcxproj: {ex.Message}");
            return false;
        }
    }

    // Adds <ClCompile Include="..."/> for .cpp files and <ClInclude Include="..."/> for the others, with paths
    // relative to the project file, to the first item group that already has items of that kind.
    private static void AddToProjectFile(string projectFile, string[] files)
    {
        var document = XDocument.Load(projectFile);
        var ns = document.Root.Name.Namespace;
        var projectFolder = Path.GetDirectoryName(projectFile);

        foreach (var file in files)
        {
            var itemType = Path.GetExtension(file).Equals(".cpp", StringComparison.OrdinalIgnoreCase) ? "ClCompile" : "ClInclude";
            var include = Path.GetRelativePath(projectFolder, file);
            var items = document.Root.Elements(ns + "ItemGroup").Elements(ns + itemType).ToList();

            if (items.Any(x => string.Equals((string)x.Attribute("Include"), include, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }

            var group = items.FirstOrDefault()?.Parent;
            if (group == null)
            {
                group = new XElement(ns + "ItemGroup");
                var lastGroup = document.Root.Elements(ns + "ItemGroup").LastOrDefault();
                if (lastGroup != null) lastGroup.AddAfterSelf(group);
                else document.Root.Add(group);
            }

            group.Add(new XElement(ns + itemType, new XAttribute("Include", include)));
        }

        document.Save(projectFile);
    }

    private static string FindOnPath(string fileName)
    {
        var path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
        return path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(folder => Path.Combine(folder.Trim(), fileName))
            .FirstOrDefault(File.Exists);
    }

    private static void OpenInCodeEditor(string folder, string file)
    {
        if (string.IsNullOrEmpty(file)) return;
        try
        {
            var code = FindOnPath("code.cmd");
            if (code != null)
            {
                // Opens (or focuses) a VS Code window on the game project's folder, with the file in it.
                Process.Start(new ProcessStartInfo(code, $"\"{folder}\" \"{file}\"")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                });
            }
            else
            {
                Process.Start(new ProcessStartInfo(file) { UseShellExecute = true });
            }
        }
        catch (Exception ex)
        {
            Logger.Log(MessageType.Warning, $"Could not open {file}: {ex.Message}");
        }
    }

    // ---- building -----------------------------------------------------------------------------------------------

    private static void BuildSolution_CommandLine(Project project, BuildConfiguration buildConfig)
    {
        if (IsGameRunning())
        {
            Logger.Log(MessageType.Error, "The game is still running. Stop it before building.");
            return;
        }

        BuildDone = BuildSucceeded = false;
        var configName = GetConfigurationName(buildConfig);
        try
        {
            var msbuild = _msbuildPath.Value;
            if (msbuild == null) return;

            DeletePdbFiles(project, configName);
            Logger.Log(MessageType.Info, $"Building {project.Name}, {configName}, x64");

            var startInfo = new ProcessStartInfo(msbuild,
                $"\"{project.Solution}\" /p:Configuration={configName} /p:Platform=x64 /m /nologo /v:minimal")
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = project.Path,
            };
            // The game project finds the engine through $(PRIMAL_ENGINE), which must end with a backslash.
            if (!string.IsNullOrEmpty(MainWindow.PrimalPath))
            {
                startInfo.Environment["PRIMAL_ENGINE"] = Path.TrimEndingDirectorySeparator(MainWindow.PrimalPath) + Path.DirectorySeparatorChar;
            }

            using var process = new Process { StartInfo = startInfo };
            process.OutputDataReceived += (_, e) => LogBuildLine(e.Data);
            process.ErrorDataReceived += (_, e) => LogBuildLine(e.Data);
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();

            BuildSucceeded = process.ExitCode == 0;
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
            Logger.Log(MessageType.Error, $"Failed to run MSBuild: {ex.Message}");
            BuildSucceeded = false;
        }
        finally
        {
            if (BuildSucceeded) Logger.Log(MessageType.Info, $"Building {configName} configuration succeeded");
            else Logger.Log(MessageType.Error, $"Building {configName} configuration failed");
            BuildDone = true;
        }
    }

    private static void LogBuildLine(string line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;

        line = _projectSuffixRegex.Replace(line.Trim(), string.Empty);
        var match = _buildMessageRegex.Match(line);
        var type = !match.Success ? MessageType.Info
            : match.Groups["kind"].Value.Equals("error", StringComparison.OrdinalIgnoreCase) ? MessageType.Error
            : MessageType.Warning;
        Logger.Log(type, line);
    }

    // ---- running ------------------------------------------------------------------------------------------------

    private static bool IsGameRunning()
    {
        try { return _gameProcess != null && !_gameProcess.HasExited; }
        catch (InvalidOperationException) { return false; }
    }

    private static void Run_CommandLine(Project project, BuildConfiguration buildConfig, bool debug)
    {
        if (IsGameRunning() || !BuildSucceeded) return;

        var configName = GetConfigurationName(buildConfig);
        var exe = Path.Combine(project.Path, "x64", configName, $"{project.Name}.exe");
        if (!File.Exists(exe))
        {
            Logger.Log(MessageType.Error, $"{exe} not found. Build the {configName} configuration first.");
            return;
        }

        if (debug)
        {
            Logger.Log(MessageType.Warning, "No Visual Studio: the game runs without a debugger.");
        }

        try
        {
            var process = new Process
            {
                StartInfo = new ProcessStartInfo(exe)
                {
                    UseShellExecute = false,
                    RedirectStandardError = true,
                    WorkingDirectory = Path.GetDirectoryName(exe),
                },
                EnableRaisingEvents = true,
            };

            // The engine logs to stderr (and to primal.log next to the game): show it in the editor's log.
            process.ErrorDataReceived += (_, e) => LogGameLine(e.Data);
            process.Exited += (_, _) => Logger.Log(MessageType.Info, $"{project.Name} exited with code {SafeExitCode(process)}");
            process.Start();
            process.BeginErrorReadLine();
            _gameProcess = process;
            Logger.Log(MessageType.Info, $"Started {exe}");
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
            Logger.Log(MessageType.Error, $"Failed to start {exe}: {ex.Message}");
        }
    }

    private static int SafeExitCode(Process process)
    {
        try { return process.ExitCode; }
        catch (InvalidOperationException) { return -1; }
    }

    // Engine lines look like "[    1.2345] [warning] [category] text".
    private static void LogGameLine(string line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;

        var type = line.Contains("] [error") ? MessageType.Error
            : line.Contains("] [warning") ? MessageType.Warning
            : MessageType.Info;
        Logger.Log(type, $"[game] {line.Trim()}");
    }

    private static void Stop_CommandLine()
    {
        if (!IsGameRunning()) return;
        try
        {
            // Closing the window lets the game shut down (scripts' end_play(), the log written out); kill it only
            // when it doesn't.
            if (!_gameProcess.CloseMainWindow() || !_gameProcess.WaitForExit(5000))
            {
                Logger.Log(MessageType.Warning, "The game didn't close: stopping it.");
                _gameProcess.Kill(true);
            }
        }
        catch (Exception ex)
        {
            Logger.Log(MessageType.Warning, $"Could not stop the game: {ex.Message}");
        }
    }
}
