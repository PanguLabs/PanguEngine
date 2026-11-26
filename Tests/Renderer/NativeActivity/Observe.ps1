param(
    [Parameter(Mandatory)][string]$LogPath,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$Serial,
    [switch]$ClearFrame,
    [switch]$IndexedMesh,
    [switch]$MeshTransform
)

$ErrorActionPreference = 'Stop'
$adb = Join-Path $AndroidSdk 'platform-tools/adb.exe'
$device = if ($Serial) { @('-s', $Serial) } else { @() }
$package = 'com.pangulabs.nativeactivitycheck.gradle.agdk'
$activity = "$package/android.app.NativeActivity"
$frameTag = if ($IndexedMesh -or $MeshTransform) { 'PanguIndexedMesh' } elseif ($ClearFrame) { 'PanguFirstFrame' } else { 'PanguFirstTriangle' }
. (Join-Path $PSScriptRoot 'SurfaceFlinger.ps1')

function Invoke-Device {
    $output = @(& $adb @device @args)
    if ($LASTEXITCODE) { throw "ADB 命令失败 $args" }
    return $output
}

function Wait-ActivityLog([string]$pattern) {
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $messages = @(Invoke-Device logcat -b main -b crash -d -v threadtime --pid=$activityPid -s 'PanguApplication:V' "${frameTag}:V" 'libc:E')
        $messages | Set-Content -LiteralPath $LogPath -Encoding utf8
        if ($messages -match 'host-failed|vulkan-failed|Fatal signal') { throw "宿主失败，见 $LogPath" }
        if ($messages -match $pattern) { return }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    throw "等待 $pattern 超时，见 $LogPath"
}

function Wait-ActivityPresentation([int]$generation, $previous = $null) {
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $presentation = Get-ActivityPresentation $activity
        if ($presentation -and $presentation.PresentTimes.Count -gt 0 -and
            (!$previous -or ($presentation.PresentTimes[-1] -gt $previous.PresentTimes[-1] -and
                ($previous.Layer -notmatch '#\d+$' -or $presentation.Layer -ne $previous.Layer)))) {
            @($presentation.Layer) + $presentation.Latency |
                Set-Content -LiteralPath "$LogPath.surface-$generation" -Encoding utf8
            if ($ClearFrame -or (Save-GeometryScreenshot "$LogPath.window-$generation.png" $generation)) {
                return $presentation
            }
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "未验证第 $generation 代窗口的像素与实际呈现，见 $LogPath"
}

function Save-GeometryScreenshot([string]$path, [int]$generation) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $adb
    $info.Arguments = ($device + @('exec-out', 'screencap', '-p')) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        $image = [IO.File]::Create($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path))
        try { $process.StandardOutput.BaseStream.CopyTo($image) }
        finally { $image.Dispose() }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "屏幕截图失败，见 $path" }
    } finally { $process.Dispose() }

    # 索引网格的期望值使用 sRGB，解码时应用设备截图内嵌的 ICC 配置。
    $bitmap = [System.Drawing.Bitmap]::new($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path),
                                          ($IndexedMesh.IsPresent -or $MeshTransform.IsPresent))
    try {
        $expected = if ($generation -eq 1) { @('6C6CDA', 'A0A095', '000000', '000000') }
                    else { @('000000', '000000', '6CDA6C', 'A095A0') }
        # Android 合成截图的高饱和色偏差较大，仍分别验证插值主色、混色和空白区域。
        $tolerances = if ($generation -eq 1) { @(32, 8, 4, 4) } else { @(4, 4, 32, 8) }
        $positions = @(@(25, 40), @(25, 60), @(75, 40), @(75, 60))
        if ($MeshTransform) {
            $positions = @(@(32.5, 57.5), @(82.5, 57.5))
            $expected = if ($generation -eq 1) { @('BC0000', '000000') } else { @('000000', 'BC0000') }
            $tolerances = if ($generation -eq 1) { @(32, 4) } else { @(4, 32) }
        } elseif ($IndexedMesh) {
            $positions = @(@(31.25, 31.25), @(68.75, 68.75), @(5, 50))
            $expected = @('00BC00', 'BC0000', '000000')
            $tolerances = @(32, 32, 4)
        }
        $colors = @(foreach ($position in $positions) {
            $pixel = $bitmap.GetPixel([Math]::Floor($bitmap.Width * $position[0] / 100),
                                     [Math]::Floor($bitmap.Height * $position[1] / 100))
            '{0:X2}{1:X2}{2:X2}' -f $pixel.R, $pixel.G, $pixel.B
        })
        "$($bitmap.Width)x$($bitmap.Height) $($colors -join ' ')" | Set-Content -LiteralPath "$path.pixels" -Encoding utf8
        for ($index = 0; $index -lt $expected.Length; ++$index) {
            foreach ($offset in 0, 2, 4) {
                $actual = [Convert]::ToInt32($colors[$index].Substring($offset, 2), 16)
                $target = [Convert]::ToInt32($expected[$index].Substring($offset, 2), 16)
                if ([Math]::Abs($actual - $target) -gt $tolerances[$index]) { return $false }
            }
        }
        return $true
    } finally { $bitmap.Dispose() }
}

Add-Type -AssemblyName System.Drawing
Invoke-Device shell pm path $package | Out-Null
Invoke-Device shell am force-stop $package | Out-Null
try {
    if ($MeshTransform) { Invoke-Device shell setprop debug.pangu.mesh_pose L }
    Invoke-Device shell am start -W -n $activity | Out-Null
    $activityPid = (Invoke-Device shell pidof $package | Select-Object -First 1)
    if ($activityPid -notmatch '^\d+$') { throw 'NativeActivity 进程启动失败。' }
    Wait-ActivityLog 'window-ready generation=1$'
    $firstPresentation = Wait-ActivityPresentation 1
    Wait-ActivityLog 'vulkan-present-requests=120$'

    Invoke-Device shell am start -W -a android.intent.action.MAIN -c android.intent.category.HOME | Out-Null
    Wait-ActivityLog 'activity-paused$'
    Wait-ActivityLog 'window-retired generation=1$'
    if ($MeshTransform) { Invoke-Device shell setprop debug.pangu.mesh_pose R }
    Invoke-Device shell am start -W -n $activity | Out-Null
    Wait-ActivityLog 'window-ready generation=2$'
    $null = Wait-ActivityPresentation 2 $firstPresentation
    if ($MeshTransform) { Invoke-Device shell setprop debug.pangu.mesh_pose D }
    Wait-ActivityLog 'host-closed$'

    $firstWindow = Select-String -LiteralPath $LogPath -Pattern "^\S+\s+\S+\s+$activityPid\s+(\d+)\s+I\s+PanguApplication:\s+window-ready generation=1$" |
        Select-Object -First 1
    if ($null -eq $firstWindow) { throw "找不到初始窗口事件，见 $LogPath" }
    $activityThread = $firstWindow.Matches[0].Groups[1].Value
    $lifecycle = (Get-Content -LiteralPath $LogPath | Where-Object { $_ -match "^\S+\s+\S+\s+$activityPid\s+$activityThread\s+" }) -join [char]10
    if ($lifecycle -notmatch '(?s)window-ready generation=1.*vulkan-present-requests=120.*window-retired generation=1.*window-ready generation=2.*vulkan-finished.*window-retired generation=2.*activity-destroyed.*host-closed') {
        throw "NativeActivity 窗口生命周期或呈现顺序不符，见 $LogPath"
    }
    "NativeActivity and Vulkan presentation verified in process $activityPid on thread $activityThread"
} finally {
    if ($MeshTransform) { Invoke-Device shell setprop debug.pangu.mesh_pose idle }
    Invoke-Device shell am force-stop $package | Out-Null
}
