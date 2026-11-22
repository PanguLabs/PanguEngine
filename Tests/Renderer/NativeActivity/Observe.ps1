param(
    [Parameter(Mandatory)][string]$LogPath,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$Serial
)

$ErrorActionPreference = 'Stop'
$adb = Join-Path $AndroidSdk 'platform-tools/adb.exe'
$device = if ($Serial) { @('-s', $Serial) } else { @() }
$package = 'com.pangulabs.nativeactivitycheck'
$activity = "$package/android.app.NativeActivity"

function Invoke-Device {
    $output = @(& $adb @device @args)
    if ($LASTEXITCODE) { throw "ADB 命令失败 $args" }
    return $output
}

function Wait-ActivityLog([string]$pattern) {
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $messages = @(Invoke-Device logcat -d -v threadtime --pid=$activityPid -s 'PanguApplication:V')
        $messages | Set-Content -LiteralPath $LogPath -Encoding utf8
        if ($messages -match 'host-failed') { throw "宿主失败，见 $LogPath" }
        if ($messages -match $pattern) { return }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    throw "等待 $pattern 超时，见 $LogPath"
}

Invoke-Device shell pm path $package | Out-Null
Invoke-Device shell am force-stop $package | Out-Null
Invoke-Device shell am start -W -n $activity
$activityPid = (Invoke-Device shell pidof $package | Select-Object -First 1).Trim()
if ($activityPid -notmatch '^\d+$') { throw 'NativeActivity 进程启动失败。' }
Wait-ActivityLog 'presentation-unavailable'
Wait-ActivityLog 'window-ready generation=1'

Invoke-Device shell am start -W -a android.intent.action.MAIN -c android.intent.category.HOME | Out-Null
Wait-ActivityLog 'activity-paused'
Wait-ActivityLog 'window-retired generation=1'
Invoke-Device shell am start -W -n $activity
Wait-ActivityLog 'window-ready generation=2'

# Android 正常销毁旧 Activity 后启动新实例，日志只验收旧宿主的关闭。
$flagActivityNewTask = 0x10000000
$flagActivityClearTask = 0x00008000
Invoke-Device shell am start -W -f ($flagActivityNewTask -bor $flagActivityClearTask) -n $activity | Out-Null
Wait-ActivityLog 'host-closed'
$firstWindow = Select-String -LiteralPath $LogPath -Pattern "^\S+\s+\S+\s+$activityPid\s+(\d+)\s+I\s+PanguApplication:\s+window-ready generation=1$" |
    Select-Object -First 1
if ($null -eq $firstWindow) { throw "找不到初始窗口事件，见 $LogPath" }
$activityThread = $firstWindow.Matches[0].Groups[1].Value
$lifecycle = (Get-Content -LiteralPath $LogPath | Where-Object { $_ -match "^\S+\s+\S+\s+$activityPid\s+$activityThread\s+" }) -join [char]10
if ($lifecycle -notmatch '(?s)window-ready generation=1.*window-retired generation=1.*window-ready generation=2.*window-retired generation=2.*activity-destroyed.*host-closed') {
    throw "NativeActivity 生命周期顺序不符，见 $LogPath"
}
Invoke-Device shell am force-stop $package | Out-Null
"NativeActivity lifecycle verified in process $activityPid on thread $activityThread"
