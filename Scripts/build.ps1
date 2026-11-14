# Windows PowerShell 5.1+；UTF-8 BOM 保证中文帮助可正确显示。
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Stop-Usage([string] $Message) {
    [Console]::Error.WriteLine("错误：$Message；使用 .\Scripts\build.ps1 help 查看用法。")
    exit 2
}

function Show-Usage {
    @'
PanguEngine 本机构建（Windows / Android）
用法：.\Scripts\build.ps1 <操作> [选项]
      .\Scripts\build.ps1 -Action <操作> [选项]
无参数仅显示帮助，不执行构建。详细说明见 Scripts/README.md。

操作          平台              用途
help          全部              显示构建清单与参数（也可用 -Help / -List）
configure     全部              仅配置 CMake
build         全部              配置并构建普通 ALL，或 -Target 指定目标
runtime       全部              关闭测试、样例、基准，仅构建正式引擎目标
fast          Windows           配置、准备并运行快速测试集合
precommit     Windows           配置、准备并运行提交前测试集合
full          Windows           配置、准备并运行完整测试集合
samples       Windows           开启样例并构建 PanguMinimalHost
benchmarks    Windows           构建 Module / Log / Meta 基准，不自动采样
mobile-link   Android           编译根工程并验证真实 Module 最终链接，仅支持 Release

选项
  -Platform windows|android           默认 windows；Android 为 ARM64/API29
  -Config Release|Debug               默认 Release
  -LinkMode Modular|Monolithic        Windows 默认 Modular，Android 默认 Monolithic
  -Target <目标名>                    仅用于 build，传入实际 CMake Target 名
  -Jobs <正整数>                      可选构建并行数，省略时沿用现有 Preset/环境
  -Fresh                              显式请求 cmake --fresh 重新配置
  -DryRun                             只显示命令，不检查工具链或执行构建

前置条件：CMake 4.0+、Ninja；Windows 设置 LLVM_ROOT（Clang 23），并提供 MSVC STL/SDK。
Android 设置 ANDROID_HOME 与 ANDROID_NDK_HOME，使用官方 NDK Clang。
产物目录：Windows 为 Build/Windows/{Bin,Lib}；Android 为 Build/Android/{Bin,Lib}。
普通桌面构建开启测试；Android 默认关闭测试。每个操作显式复位样例/基准开关。
操作会切换本平台 BuildRoot 配置，请勿与其他使用同一 BuildRoot 的任务并发运行。
mobile-link 不运行设备程序，不代表打包或签名；源码锁校验沿用现有正式验证入口。

示例：
  .\Scripts\build.ps1 fast
  .\Scripts\build.ps1 build -Config Debug -Target PanguEngineHost
  .\Scripts\build.ps1 full -LinkMode Monolithic -DryRun
  .\Scripts\build.ps1 build -Platform android
  .\Scripts\build.ps1 mobile-link -Platform android -LinkMode Modular
'@ | Write-Host
}

$action = 'help'
$actionSet = $false
$platform = 'windows'
$config = 'Release'
$linkMode = ''
$target = ''
$jobs = ''
$fresh = $false
$dryRun = $false

for ($index = 0; $index -lt $args.Count; ++$index) {
    $argument = [string] $args[$index]
    switch ($argument.ToLowerInvariant()) {
        { $_ -in '-help', '--help', '-h', '-?', '-list' } { $action = 'help'; $actionSet = $true }
        '-fresh' { $fresh = $true }
        '-dryrun' { $dryRun = $true }
        { $_ -in '-action', '-platform', '-config', '-linkmode', '-target', '-jobs' } {
            $option = $_
            ++$index
            if ($index -ge $args.Count -or [string]::IsNullOrWhiteSpace([string] $args[$index]) -or
                ([string] $args[$index]).StartsWith('-')) {
                Stop-Usage "$argument 需要参数值"
            }
            $value = [string] $args[$index]
            switch ($option) {
                '-action' {
                    if ($actionSet) { Stop-Usage '请只指定一个操作' }
                    $action = $value.ToLowerInvariant()
                    $actionSet = $true
                }
                '-platform' { $platform = $value.ToLowerInvariant() }
                '-config' { $config = $value }
                '-linkmode' { $linkMode = $value }
                '-target' { $target = $value }
                '-jobs' { $jobs = $value }
            }
        }
        default {
            if ($argument.StartsWith('-')) { Stop-Usage "未知选项 $argument" }
            if ($actionSet) { Stop-Usage '请只指定一个操作' }
            $action = $argument.ToLowerInvariant()
            $actionSet = $true
        }
    }
}

if ($action -notin 'help', 'configure', 'build', 'runtime', 'fast', 'precommit', 'full',
    'samples', 'benchmarks', 'mobile-link') { Stop-Usage "未知操作 $action" }
if ($platform -notin 'windows', 'android') { Stop-Usage '此入口仅支持 Windows 宿主的 windows/android' }
if ($config -notin 'Release', 'Debug') { Stop-Usage 'Config 必须为 Release 或 Debug' }
$config = if ($config -eq 'Debug') { 'Debug' } else { 'Release' }
if ($linkMode -and $linkMode -notin 'Modular', 'Monolithic') {
    Stop-Usage 'LinkMode 必须为 Modular 或 Monolithic'
}
if ($jobs -and $jobs -notmatch '^[1-9][0-9]*$') { Stop-Usage 'Jobs 必须为正整数' }
if ($target -and $action -ne 'build') { Stop-Usage 'Target 仅用于 build' }
if ($action -eq 'help') { Show-Usage; exit 0 }
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    Stop-Usage '此入口需要 Windows 宿主，Linux/macOS 请使用 build.sh'
}

$mobile = $platform -eq 'android'
$testAction = $action -in 'fast', 'precommit', 'full'
if ($mobile -and ($testAction -or $action -in 'samples', 'benchmarks')) {
    Stop-Usage "$action 仅用于桌面平台"
}
if ($action -eq 'mobile-link' -and (-not $mobile -or $config -ne 'Release')) {
    Stop-Usage 'mobile-link 仅用于 Android 的现有 Release 链接验证夹具'
}
if (-not $linkMode) { $linkMode = if ($mobile) { 'Monolithic' } else { 'Modular' } }
$linkMode = if ($linkMode -eq 'Monolithic') { 'Monolithic' } else { 'Modular' }
$preset = if ($mobile) { 'android-arm64' } else { 'windows' }
$sourceRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $sourceRoot $(if ($mobile) { 'Build/Android' } else { 'Build/Windows' })

function Invoke-BuildCommand([string] $Tool, [string[]] $CommandArguments) {
    $display = foreach ($part in $CommandArguments) {
        if ($part -match '^[a-zA-Z0-9_./:=+\\-]+$') { $part }
        else { "'" + $part.Replace("'", "''") + "'" }
    }
    Write-Host ("> $Tool " + ($display -join ' '))
    if (-not $dryRun) {
        & $Tool @CommandArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

try {
    if (-not $dryRun) {
        $requiredTools = @('cmake')
        if ($testAction) { $requiredTools += 'ctest' }
        foreach ($tool in $requiredTools) {
            if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
                [Console]::Error.WriteLine("缺少命令 $tool，请检查 PATH。")
                exit 127
            }
        }
    }

    $testing = if ($action -eq 'runtime' -or ($mobile -and $action -ne 'mobile-link')) { 'OFF' } else { 'ON' }
    $samples = if ($action -eq 'samples') { 'ON' } else { 'OFF' }
    $benchmarks = if ($action -eq 'benchmarks') { 'ON' } else { 'OFF' }
    $configureArguments = @('--preset', $preset, "-DCMAKE_BUILD_TYPE=$config", "-DPANGU_LINK_MODE=$linkMode",
        "-DBUILD_TESTING=$testing", "-DPANGU_BUILD_ENGINE_TESTS=$testing", "-DPANGU_BUILD_SAMPLES=$samples",
        "-DPANGU_BUILD_BENCHMARKS=$benchmarks", "-DPANGU_BUILD_META_BENCHMARKS=$benchmarks")
    if ($fresh) { $configureArguments += '--fresh' }

    Write-Host "源码：$sourceRoot"
    Write-Host "构建：$buildRoot；$config / $linkMode；操作 $action"
    Push-Location -LiteralPath $sourceRoot
    try {
        Invoke-BuildCommand cmake $configureArguments
        if ($action -ne 'configure') {
            $buildPreset = if ($testAction) { "$preset-$action" } else { $preset }
            $buildArguments = @('--build', '--preset', $buildPreset)
            if ($jobs) { $buildArguments += @('--parallel', $jobs) }
            if ($target) { $buildArguments += @('--target', $target) }
            elseif ($action -eq 'samples') { $buildArguments += @('--target', 'PanguMinimalHost') }
            elseif ($action -eq 'benchmarks') {
                $buildArguments += @('--target', 'PanguModuleBenchmarks', 'PanguLogBenchmarks', 'PanguMetaAccessBenchmarks')
            }
            Invoke-BuildCommand cmake $buildArguments
        }
        if ($testAction) { Invoke-BuildCommand ctest @('--preset', "$preset-$action") }
        if ($action -eq 'mobile-link') {
            $linkArguments = @("-DPANGU_ENGINE_SOURCE_DIR=$sourceRoot",
                "-DPANGU_TEST_ROOT=$buildRoot/Tests/ModuleLinkModes/$linkMode", "-DPANGU_TARGET_PRESET=$preset",
                "-DPANGU_LINK_MODE=$linkMode")
            if ($jobs) { $linkArguments += "-DPANGU_TEST_BUILD_JOBS=$jobs" }
            $linkArguments += @('-P', 'Tests/CMake/SourcePluginProject/CrossCompileTests.cmake')
            Invoke-BuildCommand cmake $linkArguments
        }
    }
    finally { Pop-Location }
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
