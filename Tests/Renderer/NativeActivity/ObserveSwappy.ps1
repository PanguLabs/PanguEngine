param(
    [Parameter(Mandatory)][string]$LogDirectory,
    [ValidateRange(1, 10)][int]$Runs = 3,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$Serial,
    [switch]$ClearFrame
)

$ErrorActionPreference = 'Stop'
$adb = Join-Path $AndroidSdk 'platform-tools/adb.exe'
$device = if ($Serial) { @('-s', $Serial) } else { @() }
$package = 'com.pangulabs.nativeactivitycheck.gradle.agdk'
$activity = "$package/android.app.NativeActivity"
$frameTag = if ($ClearFrame) { 'PanguFirstFrame' } else { 'PanguFirstTriangle' }
. (Join-Path $PSScriptRoot 'SurfaceFlinger.ps1')
$swappyEmbeddedDex = 'SwappyDisplayManager.*from dex bytes'
$swappyJavaWarning = 'SwappyDisplayManager.*(couldn.t find|UnsatisfiedLinkError)'
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null

function Invoke-Device {
    $output = @(& $adb @device @args)
    if ($LASTEXITCODE) { throw "ADB 命令失败 $args" }
    return $output
}

function Wait-VulkanLog([string]$pattern) {
    $deadline = (Get-Date).AddSeconds(16)
    do {
        $messages = @(Invoke-Device logcat -b main -b system -b crash -b events -d -v threadtime --pid=$activityPid)
        $messages | Set-Content -LiteralPath $runLog -Encoding utf8
        if ($messages -match 'host-failed|vulkan-failed|Fatal signal') {
            throw "Vulkan 呈现失败，见 $runLog"
        }
        if ($messages -match "$swappyEmbeddedDex|$swappyJavaWarning") {
            throw "Swappy Java 加载异常，见 $runLog"
        }
        if ($messages -match $pattern) { return }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "等待 $pattern 超时，见 $runLog"
}

$results = @(for ($run = 1; $run -le $Runs; $run++) {
    $runLog = Join-Path $LogDirectory "swappy-$run.log"
    Invoke-Device shell am force-stop $package | Out-Null
    Invoke-Device shell am start -W -n $activity | Out-Null
    $activityPid = (Invoke-Device shell pidof $package | Select-Object -First 1).Trim()
    if ($activityPid -notmatch '^\d+$') { throw 'Vulkan 测试进程启动失败。' }
    Wait-VulkanLog "${frameTag}:\s+vulkan-present-requests=120$"
    Start-Sleep -Milliseconds 1400

    $timestampLog = Join-Path $LogDirectory "swappy-$run-surfaceflinger.log"
    $presentation = Get-ActivityPresentation $activity
    if (!$presentation) { throw '未找到唯一的 Vulkan 测试 SurfaceFlinger 图层。' }
    @($presentation.Layer) + $presentation.Latency | Set-Content -LiteralPath $timestampLog -Encoding utf8
    $refreshMs = $presentation.RefreshMs
    $presentTimes = $presentation.PresentTimes
    if ($presentTimes.Count -lt 101) { throw "实际呈现时间戳不足，见 $timestampLog" }
    $intervals = @(for ($index = 1; $index -lt $presentTimes.Count; $index++) {
        ($presentTimes[$index] - $presentTimes[$index - 1]) / 1e6
    })
    if ($intervals | Where-Object { $_ -le 0 }) { throw "呈现时间戳顺序不符，见 $timestampLog" }
    $sorted = @($intervals | Sort-Object)
    $targetMs = [Math]::Max(1000 / 60, $refreshMs)
    $within = @($intervals | Where-Object { [Math]::Abs($_ - $targetMs) -le 1 })
    $median = $sorted[[int][Math]::Ceiling($sorted.Count / 2) - 1]
    $p95 = $sorted[[int][Math]::Ceiling($sorted.Count * 0.95) - 1]

    Wait-VulkanLog "${frameTag}:\s+vulkan-finished "
    Wait-VulkanLog 'PanguApplication:\s+host-closed$'
    if (-not (Select-String -LiteralPath $runLog -Pattern 'PanguApplication:\s+activity-destroyed$' -Quiet)) {
        throw "宿主关闭前未处理 Activity 销毁，见 $runLog"
    }
    Wait-VulkanLog '(?:am|wm)_on_destroy_called\s*:.*\bandroid\.app\.NativeActivity\b'
    if (-not (Select-String -LiteralPath $runLog -Pattern 'SwappyDisplayManager: Starting looper thread' -Quiet)) {
        throw "未验证到 Swappy 显示管理器启动，见 $runLog"
    }
    [pscustomobject]@{
        Run = $run
        Frames = $presentTimes.Count
        TargetMs = [Math]::Round($targetMs, 3)
        RefreshMs = [Math]::Round($refreshMs, 3)
        MedianMs = [Math]::Round($median, 3)
        P95Ms = [Math]::Round($p95, 3)
        MinMs = [Math]::Round($sorted[0], 3)
        MaxMs = [Math]::Round($sorted[-1], 3)
        Within1ms = $within.Count
        Intervals = $intervals.Count
        SwappyEmbeddedDex = (Select-String -LiteralPath $runLog -Pattern $swappyEmbeddedDex -Quiet)
        SwappyJavaWarning = (Select-String -LiteralPath $runLog -Pattern $swappyJavaWarning -Quiet)
    }
})

$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'swappy-cadence.json') -Encoding utf8
$results | Format-Table -AutoSize
