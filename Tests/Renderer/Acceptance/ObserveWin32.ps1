param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][ValidateSet('Modular', 'Monolithic')][string]$LinkMode,
    [ValidateSet('FirstFrame', 'FirstTriangle', 'IndexedMesh')][string]$Target = 'FirstFrame'
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
        return Capture(window, new int[] { 50, 50 });
    }

    public static string CaptureTriangle(IntPtr window)
    {
        return Capture(window, new int[] { 25, 40, 25, 60, 75, 40, 75, 60 });
    }

    public static string CaptureIndexedMesh(IntPtr window)
    {
        return Capture(window, new int[] { 125, 125, 275, 275, 20, 20 }, 400);
    }

    private static string Capture(IntPtr window, int[] positions, int denominator = 100)
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
            var colors = new string[positions.Length / 2];
            for (int index = 0; index < colors.Length; ++index)
            {
                int x = bounds.Right * positions[2 * index] / denominator;
                int y = bounds.Bottom * positions[2 * index + 1] / denominator;
                uint bgr = GetPixel(memory, x, y);
                if (bgr == UInt32.MaxValue)
                    return "";
                colors[index] = String.Format("#{0:X2}{1:X2}{2:X2}", bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255);
            }
            return String.Join(",", colors);
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

    public ObservedRenderer(string executable, string frameLabel)
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
            string prefix = frameLabel + " present-request-progress=";
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
            throw "$Target exited before $Condition (exit=$($run.App.ExitCode))"
        }
        if (& $Observation) {
            return
        }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    $handle = $run.App.MainWindowHandle
    $sample = if ($isIndexed) { [RendererDesktop]::CaptureIndexedMesh($handle) }
              elseif ($Target -eq 'FirstTriangle') { [RendererDesktop]::CaptureTriangle($handle) }
              else { [RendererDesktop]::CaptureCenter($handle) }
    throw "Timed out waiting for $Condition (client=$([RendererDesktop]::ClientSize($handle)), pixels=$sample, present-requests=$($run.Frames))"
}

function Read-TrianglePixels([string]$Phase, [string[]]$Expected) {
    $captureState = @{ Pixels = '' }
    Wait-For {
        $captureState.Pixels = if ($isIndexed) { [RendererDesktop]::CaptureIndexedMesh($window) }
                               else { [RendererDesktop]::CaptureTriangle($window) }
        $actual = $captureState.Pixels.Split(',')
        if ($actual.Length -ne $Expected.Length) { return $false }
        for ($pixel = 0; $pixel -lt $Expected.Length; ++$pixel) {
            for ($channel = 1; $channel -lt 7; $channel += 2) {
                $value = [Convert]::ToInt32($actual[$pixel].Substring($channel, 2), 16)
                $expectedValue = [Convert]::ToInt32($Expected[$pixel].Substring($channel, 2), 16)
                if ([Math]::Abs($value - $expectedValue) -gt 4) { return $false }
            }
        }
        return $true
    } "$Phase triangle pixels"
    return $captureState.Pixels
}

function Test-ShaderRejection([string]$Case, [string]$Shader, [scriptblock]$Change) {
    $directory = [IO.Path]::GetDirectoryName($Executable)
    $copy = Join-Path ([IO.Path]::GetDirectoryName($directory)) $Case
    Copy-Item -LiteralPath $directory -Destination $copy -Recurse
    $shaderPath = Join-Path $copy "CompiledShaders/DIRECT3D12/$Shader"
    & $Change $shaderPath
    $app = [ObservedRenderer]::new((Join-Path $copy ([IO.Path]::GetFileName($Executable))), $frameLabel)
    try {
        if (-not $app.App.WaitForExit(15000)) { throw "$Case did not exit after shader rejection" }
        $app.App.WaitForExit()
        if ($app.App.ExitCode -ne 1 -or $app.Frames -ne 0 -or
            $app.Errors -notmatch 'first-triangle failure stage=4 cause=11' -or
            $app.Errors -notmatch 'Pangu Application: error domain=70672e72656e64 code=1035' -or
            $app.Output -notmatch 'Shutting down log system[.]' -or
            ($app.Output + $app.Errors) -match 'Failed to open shader binary|Assertion failed') {
            throw "$Case did not report kSubmit/kShaderUnavailable (exit=$($app.App.ExitCode))`n$($app.Output)$($app.Errors)"
        }
        Write-Output "$Case kSubmit/kShaderUnavailable, present-requests=0, exit=1"
    }
    finally {
        [IO.File]::WriteAllText((Join-Path $copy 'deployment.log'), $app.Output + $app.Errors)
        $app.Dispose()
    }
}

$Executable = (Resolve-Path -LiteralPath $Executable).Path
$isTriangle = $Target -eq 'FirstTriangle'
$isIndexed = $Target -eq 'IndexedMesh'
$frameLabel = if ($isTriangle) { 'first-triangle' } elseif ($isIndexed) { 'indexed-mesh' } else { 'first-frame' }
[RendererDesktop]::UsePhysicalPixels()
if ($isTriangle) {
    Test-ShaderRejection 'MissingFragment' 'Graph.frag' { param($path) Remove-Item -LiteralPath $path }
    Test-ShaderRejection 'MissingRootSignature' 'default.rootsig' { param($path) Remove-Item -LiteralPath $path }
    Test-ShaderRejection 'EmptyFragment' 'Graph.frag' { param($path) [IO.File]::WriteAllBytes($path, [byte[]]@()) }
    Test-ShaderRejection 'TruncatedFragment' 'Graph.frag' {
        param($path)
        $stream = [IO.File]::OpenWrite($path)
        try {
            # FSL v1.63 的 36 字节头和一条 24 字节变体记录之后只留一个载荷字节。
            $stream.SetLength(61)
        }
        finally { $stream.Dispose() }
    }
}

$run = [ObservedRenderer]::new($Executable, $frameLabel)
try {
    Wait-For { $run.App.Refresh(); $run.App.MainWindowHandle -ne [IntPtr]::Zero } 'the Win32 window'
    $window = $run.App.MainWindowHandle
    $sizeA = [RendererDesktop]::ClientSize($window)
    if ($isIndexed) {
        $quadColors = '#00BC00', '#BC0000', '#000000'
        Write-Output (Read-TrianglePixels 'initial quad' $quadColors)
    }
    elseif ($isTriangle) {
        $initialColors = '#6C6CDA', '#A0A095', '#000000', '#000000'
        $updatedColors = '#000000', '#000000', '#6CDA6C', '#A095A0'
        $initial = Read-TrianglePixels 'initial' $initialColors
        Write-Output "Initial client=$sizeA hwnd=$window, sRGB pixels=$initial"
        [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'second-triangle'), '')
        $updated = Read-TrianglePixels 'same-size update' $updatedColors
        $run.App.Refresh()
        if ($run.App.MainWindowHandle -ne $window -or [RendererDesktop]::ClientSize($window) -ne $sizeA) {
            throw 'The triangle update changed the window or client size'
        }
        Write-Output "Updated client=$sizeA hwnd=$window, sRGB pixels=$updated"
    }
    else {
        Wait-For { [RendererDesktop]::CaptureCenter($window) -eq '#3F99C8' } 'sRGB color A'
        Write-Output "A: client $sizeA, center #3F99C8 (sRGB RGB8)"
    }

    # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    if (-not [RendererDesktop]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, 540, 360, 0x16)) {
        throw 'Cannot resize the Win32 window'
    }
    Wait-For { [RendererDesktop]::ClientSize($window) -ne $sizeA } 'the resized client area'
    $sizeB = [RendererDesktop]::ClientSize($window)
    if ($isIndexed) {
        $beforeResizePresent = $run.Frames
        Wait-For { $run.Frames -ge ($beforeResizePresent + 2) } 'indexed presents after resize'
        Write-Output (Read-TrianglePixels 'resized quad' $quadColors)
        [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'reopen'), '')
        Wait-For { $run.Output.Contains('indexed-mesh reopened') } 'indexed present on the reopened device'
        Write-Output (Read-TrianglePixels 'reopened quad' $quadColors)
    }
    elseif ($isTriangle) {
        $beforeResizePresent = $run.Frames
        Wait-For { $run.Frames -ge ($beforeResizePresent + 2) } 'present requests after resize'
        $resized = Read-TrianglePixels 'resized' $updatedColors
        Write-Output "Resized client=$sizeB hwnd=$window, sRGB pixels=$resized"
    }
    else {
        [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'second-color'), '')
        Wait-For { [RendererDesktop]::CaptureCenter($window) -eq '#FF0000' } 'sRGB color B after resize'
        Write-Output "B: client $sizeB, center #FF0000 (sRGB RGB8)"
    }

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
        throw "$Target did not load its deployed D3D12 Agility and AMD runtime"
    }
    if ($LinkMode -eq 'Modular' -and $loaded['PanguRenderer.dll'] -ne (Join-Path $directory 'PanguRenderer.dll')) {
        throw "$Target did not load the deployed Renderer module"
    }
    if ($LinkMode -eq 'Monolithic' -and $loaded.ContainsKey('PanguRenderer.dll')) {
        throw "Monolithic $Target loaded a Renderer DLL"
    }
    Wait-For { $run.Frames -ge 120 } '120 present requests'
    [IO.File]::WriteAllText((Join-Path $run.App.StartInfo.WorkingDirectory 'stop'), '')
    if (-not $run.App.WaitForExit(15000)) { throw "$Target did not close normally" }
    $run.App.WaitForExit()
    if ($run.App.ExitCode -ne 0 -or $run.Output -notmatch "$frameLabel present-requests=(\d+) skipped=(\d+) failures=0" -or
        [int]$Matches[1] -lt 120) {
        throw "$Target failed acceptance (exit=$($run.App.ExitCode))"
    }
    Write-Output "Normal exit 0 with $($Matches[1]) present requests and $($Matches[2]) skipped frames"
    if ($isTriangle) {
        $swapchains = [regex]::Matches($run.Output, 'Adding D3D12 swapchain @ (\d+x\d+)')
        if ($swapchains.Count -ne 2 -or $swapchains[0].Groups[1].Value -ne $sizeA -or
            $swapchains[1].Groups[1].Value -ne $sizeB) {
            throw 'The triangle run must create a swap chain only for the initial window and the resize'
        }
    }
}
finally {
    Write-Output $run.Output
    if ($run.Errors) { Write-Output $run.Errors }
    $run.Dispose()
}
