param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][ValidateSet('Modular', 'Monolithic')][string]$LinkMode
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class RendererDesktop
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr window, out Rect bounds);
    [DllImport("user32.dll")]
    private static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr window, IntPtr context);
    [DllImport("user32.dll")]
    private static extern bool PrintWindow(IntPtr window, IntPtr context, uint flags);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("gdi32.dll")]
    private static extern uint GetPixel(IntPtr context, int x, int y);
    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateCompatibleDC(IntPtr context);
    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateCompatibleBitmap(IntPtr context, int width, int height);
    [DllImport("gdi32.dll")]
    private static extern IntPtr SelectObject(IntPtr context, IntPtr item);
    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(IntPtr item);
    [DllImport("gdi32.dll")]
    private static extern bool DeleteDC(IntPtr context);

    public static void UsePhysicalPixels()
    {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (SetThreadDpiAwarenessContext(new IntPtr(-4)) == IntPtr.Zero)
            throw new InvalidOperationException("Could not use physical desktop pixels");
    }

    public static string ClientSize(IntPtr window)
    {
        Rect bounds;
        return GetClientRect(window, out bounds) ? String.Format("{0}x{1}", bounds.Right, bounds.Bottom) : "";
    }

    public static string CaptureCenter(IntPtr window)
    {
        Rect bounds;
        if (!GetClientRect(window, out bounds) || bounds.Right <= 0 || bounds.Bottom <= 0)
            return "";
        IntPtr context = GetDC(window);
        IntPtr memory = CreateCompatibleDC(context);
        IntPtr bitmap = CreateCompatibleBitmap(context, bounds.Right, bounds.Bottom);
        IntPtr original = SelectObject(memory, bitmap);
        try
        {
            const uint fullClient = 3;
            if (!PrintWindow(window, memory, fullClient))
                return "";
            uint bgr = GetPixel(memory, bounds.Right / 2, bounds.Bottom / 2);
            return String.Format("#{0:X2}{1:X2}{2:X2}", bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255);
        }
        finally
        {
            SelectObject(memory, original);
            DeleteObject(bitmap);
            DeleteDC(memory);
            ReleaseDC(window, context);
        }
    }
}

public sealed class ObservedRenderer : IDisposable
{
    private readonly StringBuilder output = new StringBuilder();
    private readonly StringBuilder errors = new StringBuilder();
    private int frames;

    public Process App { get; private set; }
    public int Frames { get { return Volatile.Read(ref frames); } }
    public string Output { get { lock (output) { return output.ToString(); } } }
    public string Errors { get { lock (errors) { return errors.ToString(); } } }

    public ObservedRenderer(string executable)
    {
        var settings = new ProcessStartInfo(executable);
        settings.WorkingDirectory = System.IO.Path.GetDirectoryName(executable);
        settings.UseShellExecute = false;
        settings.CreateNoWindow = true;
        settings.RedirectStandardOutput = true;
        settings.RedirectStandardError = true;
        settings.EnvironmentVariables.Remove("PATH");
        App = new Process { StartInfo = settings };
        App.OutputDataReceived += delegate(object sender, DataReceivedEventArgs data)
        {
            if (data.Data == null)
                return;
            const string prefix = "first-frame present-request-progress=";
            int count;
            if (data.Data.StartsWith(prefix, StringComparison.Ordinal) &&
                Int32.TryParse(data.Data.Substring(prefix.Length), out count))
            {
                Volatile.Write(ref frames, count);
                return;
            }
            lock (output) { output.AppendLine(data.Data); }
        };
        App.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs data)
        {
            if (data.Data != null)
                lock (errors) { errors.AppendLine(data.Data); }
        };
        App.Start();
        App.BeginOutputReadLine();
        App.BeginErrorReadLine();
    }

    public void Dispose()
    {
        if (!App.HasExited)
        {
            App.Kill();
            App.WaitForExit();
        }
        App.Dispose();
    }
}
'@

function Wait-For([scriptblock]$Observation, [string]$Condition) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if ($run.App.HasExited) {
            throw "FirstFrame exited before $Condition (exit=$($run.App.ExitCode))"
        }
        if (& $Observation) {
            return
        }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for $Condition (client=$([RendererDesktop]::ClientSize($run.App.MainWindowHandle)), color=$([RendererDesktop]::CaptureCenter($run.App.MainWindowHandle)), present-requests=$($run.Frames))"
}

[RendererDesktop]::UsePhysicalPixels()
$run = [ObservedRenderer]::new((Resolve-Path -LiteralPath $Executable).Path)
try {
    Wait-For { $run.App.Refresh(); $run.App.MainWindowHandle -ne [IntPtr]::Zero } 'the Win32 window'
    $window = $run.App.MainWindowHandle
    $sizeA = [RendererDesktop]::ClientSize($window)
    Wait-For { [RendererDesktop]::CaptureCenter($window) -eq '#3F99C8' } 'sRGB color A'
    Write-Output "A: client $sizeA, center #3F99C8 (sRGB RGB8)"

    # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    if (-not [RendererDesktop]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, 540, 360, 0x16)) {
        throw 'Cannot resize the Win32 window'
    }
    Wait-For { [RendererDesktop]::ClientSize($window) -ne $sizeA } 'the resized client area'
    $sizeB = [RendererDesktop]::ClientSize($window)
    [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'second-color'), '')
    Wait-For { [RendererDesktop]::CaptureCenter($window) -eq '#FF0000' } 'sRGB color B after resize'
    Write-Output "B: client $sizeB, center #FF0000 (sRGB RGB8)"

    $loaded = @{}
    foreach ($module in $run.App.Modules) {
        if ($module.ModuleName -match '^(PanguRenderer|D3D12Core|amd_ags_x64)[.]dll$') {
            $loaded[$module.ModuleName] = $module.FileName
            Write-Output "Loaded $($module.FileName)"
        }
    }
    $directory = $run.App.StartInfo.WorkingDirectory
    if ($loaded['D3D12Core.dll'] -ne (Join-Path $directory 'D3D12/D3D12Core.dll') -or
        $loaded['amd_ags_x64.dll'] -ne (Join-Path $directory 'amd_ags_x64.dll')) {
        throw 'FirstFrame did not load its deployed D3D12 Agility and AMD runtime'
    }
    if ($LinkMode -eq 'Modular' -and $loaded['PanguRenderer.dll'] -ne (Join-Path $directory 'PanguRenderer.dll')) {
        throw 'FirstFrame did not load the deployed Renderer module'
    }
    if ($LinkMode -eq 'Monolithic' -and $loaded.ContainsKey('PanguRenderer.dll')) {
        throw 'Monolithic FirstFrame loaded a Renderer DLL'
    }
    Wait-For { $run.Frames -ge 120 } '120 present requests'
    [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'stop'), '')
    if (-not $run.App.WaitForExit(15000)) { throw 'FirstFrame did not close normally' }
    $run.App.WaitForExit()
    if ($run.App.ExitCode -ne 0 -or $run.Output -notmatch 'first-frame present-requests=(\d+) skipped=(\d+) failures=0' -or
        [int]$Matches[1] -lt 120) {
        throw "FirstFrame failed acceptance (exit=$($run.App.ExitCode))"
    }
    Write-Output "Normal exit 0 with $($Matches[1]) present requests and $($Matches[2]) skipped frames"
}
finally {
    Write-Output $run.Output
    if ($run.Errors) { Write-Output $run.Errors }
    $run.Dispose()
}
