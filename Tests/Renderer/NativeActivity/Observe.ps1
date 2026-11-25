param(
    [Parameter(Mandatory)][string]$LogPath,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$Serial
)

$ErrorActionPreference = 'Stop'
$adb = Join-Path $AndroidSdk 'platform-tools/adb.exe'
$device = if ($Serial) { @('-s', $Serial) } else { @() }
$package = 'com.pangulabs.nativeactivitycheck.gradle.agdk'
$activity = "$package/android.app.NativeActivity"
. (Join-Path $PSScriptRoot 'SurfaceFlinger.ps1')

function Invoke-Device {
    $output = @(& $adb @device @args)
    if ($LASTEXITCODE) { throw "ADB 命令失败 $args" }
    return $output
}

function Wait-ActivityLog([string]$pattern) {
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $messages = @(Invoke-Device logcat -b main -b crash -d -v threadtime --pid=$activityPid -s 'PanguApplication:V' 'PanguFirstFrame:V' 'libc:E')
        $messages | Set-Content -LiteralPath $LogPath -Encoding utf8
        if ($messages -match 'host-failed|Fatal signal') { throw "宿主失败，见 $LogPath" }
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
            return $presentation
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw "未观察到第 $generation 代窗口的实际呈现，见 $LogPath"
}

Invoke-Device shell pm path $package | Out-Null
Invoke-Device shell am force-stop $package | Out-Null
try {
    Invoke-Device shell am start -W -n $activity | Out-Null
    $activityPid = (Invoke-Device shell pidof $package | Select-Object -First 1)
    if ($activityPid -notmatch '^\d+$') { throw 'NativeActivity 进程启动失败。' }
    Wait-ActivityLog 'vulkan-present-requests=120$'
    $firstPresentation = Wait-ActivityPresentation 1

    Invoke-Device shell am start -W -a android.intent.action.MAIN -c android.intent.category.HOME | Out-Null
    Wait-ActivityLog 'activity-paused$'
    Wait-ActivityLog 'window-retired generation=1$'
    Invoke-Device shell am start -W -n $activity | Out-Null
    Wait-ActivityLog 'window-ready generation=2$'
    $null = Wait-ActivityPresentation 2 $firstPresentation
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
    Invoke-Device shell am force-stop $package | Out-Null
}
