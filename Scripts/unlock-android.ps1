<#
.SYNOPSIS
通过已配对的无线 ADB 唤醒 Android 真机，用 scrcpy 的 UHID 键盘解锁并保留投屏窗口。
.DESCRIPTION
使用 Android SDK 的 platform-tools\adb.exe。SDK 位置依次取自 ANDROID_HOME、ANDROID_SDK_ROOT 和当前用户的默认 SDK 目录。
从 PATH 启动 scrcpy。锁屏时还需能通过 PATH 调用 cua-driver。
未指定 -Pin 时，从项目根目录的 .local\credentials\android-device.xml 读取 Windows PSCredential。
凭据用户名应为 adb 设备序列号；文件缺失或设备不匹配时，交互式输入 PIN。
.EXAMPLE
.\Scripts\unlock-android.ps1
.EXAMPLE
.\Scripts\unlock-android.ps1 -Serial '192.168.1.20:37123'
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9._:%\[\]-]+$')]
    [string] $Serial,
    [Security.SecureString] $Pin
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Invoke-DeviceShell([string[]] $Command) {
    $output = & $adb -s $Serial shell @Command
    if ($LASTEXITCODE -ne 0) { throw "设备命令失败：$($Command -join ' ')" }
    return $output
}

function Test-Locked {
    $policy = Invoke-DeviceShell @('dumpsys', 'window', 'policy')
    if ($policy -match '^\s*showing=true\s*$') { return $true }
    if ($policy -match '^\s*showing=false\s*$') { return $false }
    throw '无法读取系统锁屏状态。'
}

function Test-CredentialScreen {
    $ui = Invoke-DeviceShell @('dumpsys', 'activity', 'service', 'com.android.systemui/.SystemUIService')
    return [bool]($ui -match '^\s*mBouncerShowing=true\s*$')
}

function Invoke-WindowCommand([string] $Command, [hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress
    $output = $json | cua-driver.exe call $Command
    if ($LASTEXITCODE -ne 0) { throw "scrcpy 窗口操作失败：$Command" }
    $response = $output -join "`n"
    if ($response -match '"isError"\s*:\s*true|background_unavailable') {
        throw "scrcpy 窗口未接受操作：$Command"
    }
    if ($Command -eq 'list_windows') { return ($response | ConvertFrom-Json) }
}

$sdkRoot = $env:ANDROID_HOME
if (!$sdkRoot) { $sdkRoot = $env:ANDROID_SDK_ROOT }
if (!$sdkRoot) { $sdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$adb = Join-Path $sdkRoot 'platform-tools\adb.exe'
if (!(Test-Path -LiteralPath $adb -PathType Leaf)) {
    throw "找不到 Android SDK 的 adb：$adb。请检查 ANDROID_HOME 并确认已安装 SDK Platform-Tools。"
}

if (!$Serial) {
    $Serial = & $adb -e get-serialno
    if ($LASTEXITCODE -ne 0 -or !$Serial -or $Serial -eq 'unknown') {
        throw '请连接一台无线 ADB 设备，或用 -Serial 指定 adb devices -l 中的设备。'
    }
}
$deviceState = & $adb -s $Serial get-state
if ($LASTEXITCODE -ne 0 -or $deviceState -ne 'device') { throw "设备未就绪：$Serial" }

# scrcpy 与本脚本共用同一份 adb，避免不同版本争用服务。
$previousAdb = $env:ADB
try {
    $env:ADB = $adb
    $client = Start-Process -FilePath scrcpy.exe -WindowStyle Hidden -PassThru -ArgumentList @(
        "--serial=$Serial", '--window-title=Android-scrcpy', '--keyboard=uhid', '--mouse=uhid',
        '--no-power-on', '--no-audio', '--no-clipboard-autosync'
    )
} finally {
    $env:ADB = $previousAdb
}
if (Test-Locked) {
    Get-Command cua-driver.exe -CommandType Application | Out-Null
    if (!$Pin) {
        $credentialPath = Join-Path (Split-Path -Parent $PSScriptRoot) '.local\credentials\android-device.xml'
        if (Test-Path -LiteralPath $credentialPath -PathType Leaf) {
            $credential = Import-Clixml -LiteralPath $credentialPath
            if ($credential -is [PSCredential] -and $credential.UserName -ceq $Serial) {
                $Pin = $credential.Password
            }
        }
    }
    if (!$Pin) { $Pin = Read-Host '请在此终端输入手机锁屏 PIN，按回车后将唤醒手机' -AsSecureString }
    if (!$Pin -or $Pin.Length -eq 0) { throw 'PIN 为空。' }

    # 息屏时 UHID 键盘可以先就绪，scrcpy 窗口要等亮屏后才会出现。
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ($client.HasExited) { throw 'scrcpy 已退出，请检查无线连接。' }
        $inputs = Invoke-DeviceShell @('dumpsys', 'input')
        if ($inputs -match '^\s*Device \d+: scrcpy\s*$') { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw '设备尚未识别 scrcpy 的 UHID 键盘。' }
        Start-Sleep -Milliseconds 250
    } while ($true)
    Invoke-DeviceShell @('cmd', 'power', 'wakeup') | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($client.HasExited) { throw 'scrcpy 已退出，请检查无线连接。' }
        $windows = Invoke-WindowCommand 'list_windows' @{pid = $client.Id}
        $target = @($windows.windows | Where-Object {
            $_.pid -eq $client.Id -and $_.title -eq 'Android-scrcpy' -and $_.is_on_screen
        })
        if ($target.Count -eq 1) { break }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($target.Count -ne 1) { throw '未找到本次启动的 scrcpy 窗口。' }
    $window = @{pid = $client.Id; window_id = $target[0].window_id}
    Invoke-WindowCommand 'get_window_state' ($window + @{include_screenshot = $false}) | Out-Null
    # 如果等待窗口期间再次息屏，先重新点亮再调出 PIN 界面。
    Invoke-DeviceShell @('cmd', 'power', 'wakeup') | Out-Null
    Invoke-DeviceShell @('wm', 'dismiss-keyguard') | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (!(Test-CredentialScreen)) {
        if (!(Test-Locked)) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw '未检测到系统凭据输入界面。' }
        Start-Sleep -Milliseconds 250
    }
    if (Test-Locked) {
        $buffer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Pin)
        try {
            # 先验证整串 PIN，再逐键投递；不将完整明文放入子进程参数。
            for ($i = 0; $i -lt $Pin.Length; $i++) {
                $code = [Runtime.InteropServices.Marshal]::ReadInt16($buffer, 2 * $i)
                if ($code -lt 48 -or $code -gt 57) { throw '手机 PIN 只接受数字。' }
            }
            for ($i = 0; $i -lt $Pin.Length; $i++) {
                $key = [string][char][Runtime.InteropServices.Marshal]::ReadInt16($buffer, 2 * $i)
                Invoke-WindowCommand 'press_key' ($window + @{key = $key}) | Out-Null
            }
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($buffer)
        }
        Start-Sleep -Milliseconds 450
        if (Test-CredentialScreen) {
            Invoke-WindowCommand 'press_key' ($window + @{key = 'return'}) | Out-Null
        }
    }
} else {
    Invoke-DeviceShell @('cmd', 'power', 'wakeup') | Out-Null
}

$deadline = [DateTime]::UtcNow.AddSeconds(5)
while (Test-Locked) {
    if ([DateTime]::UtcNow -ge $deadline) { throw '手机仍处于锁屏状态，请在投屏窗口中确认。' }
    Start-Sleep -Milliseconds 300
}
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $client.Refresh()
    if ($client.HasExited) { throw 'scrcpy 已退出。' }
    if ($client.MainWindowTitle -eq 'Android-scrcpy') { break }
    Start-Sleep -Milliseconds 250
} while ([DateTime]::UtcNow -lt $deadline)
if ($client.MainWindowTitle -ne 'Android-scrcpy') { throw '无法确认 scrcpy 投屏窗口已打开。' }
Write-Output "设备已解锁，scrcpy 窗口已打开（PID $($client.Id)）。"
