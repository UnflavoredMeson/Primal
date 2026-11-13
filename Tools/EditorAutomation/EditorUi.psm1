# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Drives the WPF editor (PrimalEditor) through Windows UI Automation: find controls by name, click them, type, press
# keys, read text. No screenshots. Runs in Windows PowerShell 5.1 (powershell.exe), which has the UI Automation
# assemblies; PowerShell 7 doesn't load them.
#
# Checked 2026-11-13: opening a project, waiting for log lines, clicking Build and reading the log panel work. The
# main menu items stay disabled for UI Automation; the scene's own menu (next to the scene name) works.
#
#   Import-Module .\Tools\EditorAutomation\EditorUi.psm1
#   $editor = Start-Editor -Configuration DebugEditor -StdErr "$env:TEMP\editor.txt"
#   Get-UiTree $editor.Window | Out-File tree.txt
#   Invoke-UiElement (Find-UiElement $editor.Window -Name 'Open')
#   Stop-Editor $editor

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms

$script:Ae = [System.Windows.Automation.AutomationElement]
$script:Tree = [System.Windows.Automation.TreeScope]

function Start-Editor {
    param(
        [string] $Configuration = 'DebugEditor',
        [string] $Repository = (Resolve-Path "$PSScriptRoot\..\.."),
        [string] $StdErr = "$env:TEMP\primal-editor-stderr.txt",
        [int] $TimeoutSeconds = 60
    )
    $exe = Join-Path $Repository "x64\$Configuration\PrimalEditor.exe"
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru -RedirectStandardError $StdErr
    $null = $process.Handle # keeps the exit code readable after the process ends
    $window = Wait-UiWindow -ProcessId $process.Id -TimeoutSeconds $TimeoutSeconds
    [pscustomobject]@{ Process = $process; Window = $window; StdErr = $StdErr }
}

# The first top-level window of the process (or one whose name matches -Name).
function Wait-UiWindow {
    param([int] $ProcessId, [string] $Name = '*', [int] $TimeoutSeconds = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $condition = New-Object System.Windows.Automation.PropertyCondition($script:Ae::ProcessIdProperty, $ProcessId)
    while ((Get-Date) -lt $deadline) {
        $windows = $script:Ae::RootElement.FindAll($script:Tree::Children, $condition)
        foreach ($w in $windows) {
            if ($w.Current.Name -clike $Name) { return $w }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "No window '$Name' of process $ProcessId after $TimeoutSeconds s"
}

# Every top-level window of the process (dialogs are separate windows in WPF).
function Get-UiWindows {
    param([int] $ProcessId)
    $condition = New-Object System.Windows.Automation.PropertyCondition($script:Ae::ProcessIdProperty, $ProcessId)
    @($script:Ae::RootElement.FindAll($script:Tree::Children, $condition))
}

# One line per element: indentation, control type, name, automation id, class.
function Get-UiTree {
    param($Element, [int] $MaxDepth = 12)
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    function Walk($e, $depth) {
        if ($depth -gt $MaxDepth -or -not $e) { return }
        $c = $e.Current
        $type = $c.ControlType.ProgrammaticName -replace '^ControlType\.', ''
        '{0}{1} "{2}" id={3} class={4}{5}' -f ('  ' * $depth), $type, $c.Name, $c.AutomationId, $c.ClassName,
            $(if (-not $c.IsEnabled) { ' (disabled)' } else { '' })
        $child = $walker.GetFirstChild($e)
        while ($child) {
            Walk $child ($depth + 1)
            $child = $walker.GetNextSibling($child)
        }
    }
    Walk $Element 0
}

# Finds the first descendant by name and/or automation id and/or control type (wildcards allowed in -Name).
function Find-UiElement {
    param($Root, [string] $Name, [string] $AutomationId, [string] $ControlType, [int] $TimeoutSeconds = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $all = $Root.FindAll($script:Tree::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($e in $all) {
            $c = $e.Current
            # Case-sensitive: the project browser has both "OPEN PROJECT" (a tab) and "Open Project" (the button).
            if ($Name -and $c.Name -cnotlike $Name) { continue }
            if ($AutomationId -and $c.AutomationId -ne $AutomationId) { continue }
            if ($ControlType -and ($c.ControlType.ProgrammaticName -replace '^ControlType\.', '') -ne $ControlType) { continue }
            return $e
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "Element not found: name='$Name' id='$AutomationId' type='$ControlType'"
}

# Clicks a button (Invoke), selects a list item (SelectionItem), toggles a check box (Toggle), or expands a menu.
function Invoke-UiElement {
    param([Parameter(Mandatory)] $Element)
    $patterns = $Element.GetSupportedPatterns()
    foreach ($p in $patterns) {
        switch ($p.ProgrammaticName) {
            'InvokePatternIdentifiers.Pattern' { $Element.GetCurrentPattern($p).Invoke(); return }
            'SelectionItemPatternIdentifiers.Pattern' { $Element.GetCurrentPattern($p).Select(); return }
            'TogglePatternIdentifiers.Pattern' { $Element.GetCurrentPattern($p).Toggle(); return }
            'ExpandCollapsePatternIdentifiers.Pattern' { $Element.GetCurrentPattern($p).Expand(); return }
        }
    }
    throw "Element '$($Element.Current.Name)' supports no click-like pattern"
}

function Set-UiText {
    param([Parameter(Mandatory)] $Element, [string] $Text)
    $pattern = $Element.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    $pattern.SetValue($Text)
}

# Sends keys to the focused window, SendKeys syntax: '^{F5}' is Ctrl+F5, '+{F5}' Shift+F5, '{F7}' F7.
function Send-UiKeys {
    param([Parameter(Mandatory)] $Window, [Parameter(Mandatory)] [string] $Keys)
    $Window.SetFocus()
    Start-Sleep -Milliseconds 200
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
}

# The names of all text elements below $Root (e.g. the log panel's lines).
function Get-UiTexts {
    param($Root)
    $condition = New-Object System.Windows.Automation.PropertyCondition($script:Ae::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Text)
    $Root.FindAll($script:Tree::Descendants, $condition) | ForEach-Object { $_.Current.Name } | Where-Object { $_ }
}

# In the project browser (the first window), opens the project with this name and waits until the main window is
# enabled. Returns the main window.
function Open-EditorProject {
    param([Parameter(Mandatory)] $Editor, [Parameter(Mandatory)] [string] $Name, [int] $TimeoutSeconds = 120)
    $browser = Wait-UiWindow -ProcessId $Editor.Process.Id -Name 'Project Browser'
    $items = $browser.FindAll($script:Tree::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition($script:Ae::ClassNameProperty, 'ListBoxItem')))
    $item = $items | Where-Object { (Get-UiTexts $_) -ccontains $Name } | Select-Object -First 1
    if (-not $item) { throw "No project '$Name' in the project browser" }
    Invoke-UiElement $item
    Invoke-UiElement (Find-UiElement $browser -Name 'Open Project' -ControlType Button)

    # The main window's title names the open project ("Primal Editor v0.0.1 [Name]"). Not the Build button: WPF
    # re-enables it only after the next mouse or keyboard input. Wait for "Game code DLL loaded" with Wait-EditorLog.
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        foreach ($w in Get-UiWindows -ProcessId $Editor.Process.Id) {
            if ($w.Current.Name -clike "*[[]$Name[]]" -and $w.Current.IsEnabled) { return $w }
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Project '$Name' didn't open within $TimeoutSeconds s"
}

# The editor's log panel, one string per message, oldest first.
function Get-EditorLog {
    param([Parameter(Mandatory)] $Window)
    $views = $Window.FindAll($script:Tree::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition($script:Ae::ClassNameProperty, 'LoggerView')))
    foreach ($view in $views) {
        $items = $view.FindAll($script:Tree::Descendants,
            (New-Object System.Windows.Automation.PropertyCondition($script:Ae::ClassNameProperty, 'ItemsControlItem')))
        foreach ($item in $items) { (Get-UiTexts $item) -join ' ' }
    }
}

# Waits until a log line matches the regex (case-sensitive) and returns it; $null after the timeout. -After skips
# the first N lines (pass the count from an earlier Get-EditorLog to wait for a new line).
function Wait-EditorLog {
    param([Parameter(Mandatory)] $Window, [Parameter(Mandatory)] [string] $Pattern, [int] $After = 0,
        [int] $TimeoutSeconds = 120)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $lines = @(Get-EditorLog $Window)
        $match = $lines | Select-Object -Skip $After | Where-Object { $_ -cmatch $Pattern } | Select-Object -First 1
        if ($match) { return $match }
        Start-Sleep -Milliseconds 500
    }
    $null
}

# Closes every window of the editor (a clean shutdown) and waits for it to exit.
function Stop-Editor {
    param([Parameter(Mandatory)] $Editor, [int] $TimeoutSeconds = 20)
    $p = $Editor.Process
    foreach ($w in Get-UiWindows -ProcessId $p.Id) {
        try { $w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() } catch { }
    }
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { $p.Kill(); return 'killed' }
    "exit $($p.ExitCode)"
}

Export-ModuleMember -Function Start-Editor, Wait-UiWindow, Get-UiWindows, Get-UiTree, Find-UiElement,
    Invoke-UiElement, Set-UiText, Send-UiKeys, Get-UiTexts, Open-EditorProject, Get-EditorLog, Wait-EditorLog, Stop-Editor
