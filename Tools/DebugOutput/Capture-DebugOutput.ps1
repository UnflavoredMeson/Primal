# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Runs a Windows program under the debugging API (no symbols, no breakpoints) and prints what it writes with
# OutputDebugString: the MSVC debug CRT's leak report at exit, the D3D12 debug layer's own output and
# ReportLiveDeviceObjects(). Exceptions are passed on to the program unchanged. After -Seconds, WM_CLOSE is posted to
# the program's windows (a clean shutdown); -Seconds 0 waits until it ends by itself.
#
#   pwsh -File Tools\DebugOutput\Capture-DebugOutput.ps1 -Exe x64\Debug\EngineTest_RENDERER.exe -Seconds 6
param(
    [Parameter(Mandatory)] [string] $Exe,
    [string] $Arguments = '',
    [int] $Seconds = 6
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class DebugOutputCapture
{
    const uint DEBUG_ONLY_THIS_PROCESS = 0x2;
    const uint CREATE_PROCESS_DEBUG_EVENT = 3, EXIT_PROCESS_DEBUG_EVENT = 5, LOAD_DLL_DEBUG_EVENT = 6;
    const uint EXCEPTION_DEBUG_EVENT = 1, OUTPUT_DEBUG_STRING_EVENT = 8;
    const uint DBG_CONTINUE = 0x00010002, DBG_EXCEPTION_NOT_HANDLED = 0x80010001;
    const uint EXCEPTION_BREAKPOINT = 0x80000003;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO { public int cb; public IntPtr r1, desktop, title; public int x, y, w, h, cx, cy, fill, flags; public short show, r2; public IntPtr r3, stdin, stdout, stderr; }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr process, thread; public uint pid, tid; }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool WaitForDebugEventEx(IntPtr evt, uint ms);
    [DllImport("kernel32.dll")] static extern bool ContinueDebugEvent(uint pid, uint tid, uint status);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr p, out uint code);

    delegate bool EnumProc(IntPtr hwnd, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);

    public static List<string> Run(string exe, string args, string dir, int closeAfterSeconds, out uint exitCode)
    {
        var lines = new List<string>();
        var si = new STARTUPINFO { cb = Marshal.SizeOf<STARTUPINFO>() };
        var cmd = new StringBuilder("\"" + exe + "\" " + args);
        PROCESS_INFORMATION pi;
        if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, DEBUG_ONLY_THIS_PROCESS, IntPtr.Zero, dir, ref si, out pi))
            throw new Exception("CreateProcess failed: " + Marshal.GetLastWin32Error());

        bool exited = false;
        if (closeAfterSeconds > 0)
        {
            new Thread(() =>
            {
                Thread.Sleep(closeAfterSeconds * 1000);
                for (int i = 0; i < 60 && !Volatile.Read(ref exited); ++i)
                {
                    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pi.pid && IsWindowVisible(h)) PostMessage(h, 0x0010, IntPtr.Zero, IntPtr.Zero); return true; }, IntPtr.Zero);
                    Thread.Sleep(500);
                }
            }) { IsBackground = true }.Start();
        }

        // DEBUG_EVENT: code, pid, tid, then the union at offset 16 (x64).
        IntPtr evt = Marshal.AllocHGlobal(512);
        bool initialBreakpointSeen = false;
        exitCode = 0;
        var pending = new StringBuilder();
        while (!exited)
        {
            if (!WaitForDebugEventEx(evt, 0xFFFFFFFF)) break;
            uint code = (uint)Marshal.ReadInt32(evt, 0), pid = (uint)Marshal.ReadInt32(evt, 4), tid = (uint)Marshal.ReadInt32(evt, 8);
            IntPtr u = evt + 16;
            uint status = DBG_CONTINUE;
            switch (code)
            {
                case CREATE_PROCESS_DEBUG_EVENT:
                case LOAD_DLL_DEBUG_EVENT:
                    IntPtr file = Marshal.ReadIntPtr(u, 0); // hFile: the debugger must close it
                    if (file != IntPtr.Zero) CloseHandle(file);
                    break;
                case OUTPUT_DEBUG_STRING_EVENT:
                {
                    IntPtr data = Marshal.ReadIntPtr(u, 0);
                    bool unicode = Marshal.ReadInt16(u, 8) != 0;
                    int bytes = (ushort)Marshal.ReadInt16(u, 10); // nDebugStringLength: in bytes, also for UTF-16
                    var buffer = new byte[bytes];
                    IntPtr read;
                    if (bytes > 0 && ReadProcessMemory(pi.process, data, buffer, (IntPtr)bytes, out read))
                    {
                        string text = (unicode ? Encoding.Unicode : Encoding.UTF8).GetString(buffer, 0, (int)read).TrimEnd('\0');
                        pending.Append(text);
                        string all = pending.ToString();
                        int nl;
                        while ((nl = all.IndexOf('\n')) >= 0) { lines.Add(all.Substring(0, nl).TrimEnd('\r')); all = all.Substring(nl + 1); }
                        pending.Clear().Append(all);
                    }
                    break;
                }
                case EXCEPTION_DEBUG_EVENT:
                {
                    uint exceptionCode = (uint)Marshal.ReadInt32(u, 0);
                    if (exceptionCode == EXCEPTION_BREAKPOINT && !initialBreakpointSeen) { initialBreakpointSeen = true; }
                    else
                    {
                        status = DBG_EXCEPTION_NOT_HANDLED;
                        // 0x406D1388: the old "set thread name" exception that drivers raise; not worth a line.
                        if (exceptionCode != 0x406D1388)
                            lines.Add(string.Format("[capture] exception 0x{0:X8} (passed to the program)", exceptionCode));
                    }
                    break;
                }
                case EXIT_PROCESS_DEBUG_EVENT:
                    exitCode = (uint)Marshal.ReadInt32(u, 0);
                    Volatile.Write(ref exited, true);
                    break;
            }
            ContinueDebugEvent(pid, tid, status);
        }
        if (pending.Length > 0) lines.Add(pending.ToString());
        Marshal.FreeHGlobal(evt);
        CloseHandle(pi.thread);
        CloseHandle(pi.process);
        return lines;
    }
}
'@

$path = (Resolve-Path $Exe).Path
$exitCode = [uint32]0
$lines = [DebugOutputCapture]::Run($path, $Arguments, (Split-Path $path), $Seconds, [ref]$exitCode)
$lines
"[capture] exit code $([int]$exitCode)"
