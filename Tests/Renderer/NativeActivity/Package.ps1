param(
    [Parameter(Mandatory)][string]$BuildRoot,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$AndroidNdk = $env:ANDROID_NDK_HOME,
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$BuildToolsVersion = '37.0.0',
    [string]$CompilePlatform = 'android-37.0'
)

$ErrorActionPreference = 'Stop'

$build = (Resolve-Path -LiteralPath $BuildRoot).Path
$cache = Join-Path $build 'CMakeCache.txt'
$preview = Select-String -LiteralPath $cache -Pattern '^PANGU_ENABLE_VULKAN_PREVIEW:[^=]+=(.*)$'
if ($preview -and $preview.Matches[0].Groups[1].Value -notmatch '^(0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|.*-NOTFOUND)?$') {
    throw 'Vulkan 预览 APK 请使用 Android Studio 测试工程的 agdkDebug 变体。'
}
$tools = Join-Path $AndroidSdk "build-tools/$BuildToolsVersion"
$androidJar = Join-Path $AndroidSdk "platforms/$CompilePlatform/android.jar"
$ndk = Join-Path $AndroidNdk 'toolchains/llvm/prebuilt/windows-x86_64'
$readObject = Join-Path $ndk 'bin/llvm-readobj.exe'
$output = Join-Path $build 'Tests/NativeActivity'
$modeLine = Select-String -LiteralPath $cache -Pattern '^PANGU_LINK_MODE:[^=]+=(Monolithic|Modular)$'
$mode = $modeLine.Matches[0].Groups[1].Value
$archiveDirectory = Join-Path $output $mode
New-Item -ItemType Directory -Force -Path $archiveDirectory | Out-Null

$application = Join-Path $build 'Bin/PanguNativeActivityCheck/libPanguNativeActivityCheck.so'
$runtime = Join-Path $ndk 'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
$modulePaths = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
$pending = [Collections.Generic.Queue[string]]::new()
$pending.Enqueue((Resolve-Path -LiteralPath $application).Path)
while ($pending.Count -gt 0) {
    $library = $pending.Dequeue()
    $name = Split-Path -Leaf $library
    if ($modulePaths.ContainsKey($name)) { continue }
    $modulePaths.Add($name, $library)
    $dependencies = @(& $readObject --needed-libs $library)
    if ($LASTEXITCODE) { throw "无法检查 Android 库依赖 $library" }
    foreach ($dependency in $dependencies) {
        if ($dependency.Trim() -match '^libPangu[A-Za-z0-9]+\.so$') {
            $dependencyPath = Join-Path $build ('Bin/' + $dependency.Trim())
            $pending.Enqueue((Resolve-Path -LiteralPath $dependencyPath).Path)
        }
    }
}
$modulePaths.Add('libc++_shared.so', (Resolve-Path -LiteralPath $runtime).Path)

$manifest = Join-Path $PSScriptRoot 'AndroidManifest.xml'
$unsigned = Join-Path $archiveDirectory 'unsigned.apk'
$aligned = Join-Path $archiveDirectory 'aligned.apk'
$apk = Join-Path $archiveDirectory 'PanguNativeActivityCheck.apk'
& (Join-Path $tools 'aapt2.exe') link -I $androidJar --manifest $manifest -o $unsigned
if ($LASTEXITCODE) { throw 'Android Manifest 链接失败。' }

Add-Type -AssemblyName System.IO.Compression
$stream = [IO.File]::Open($unsigned, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite)
try {
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Update, $true)
    try {
        foreach ($name in ($modulePaths.Keys | Sort-Object)) {
            $entry = $zip.CreateEntry("lib/arm64-v8a/$name", [IO.Compression.CompressionLevel]::NoCompression)
            $source = [IO.File]::OpenRead($modulePaths[$name])
            try {
                $destination = $entry.Open()
                try { $source.CopyTo($destination) } finally { $destination.Dispose() }
            } finally { $source.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }

& (Join-Path $tools 'zipalign.exe') -f -P 16 4 $unsigned $aligned
if ($LASTEXITCODE) { throw 'APK 对齐失败。' }
$originalJavaHome = $env:JAVA_HOME
try {
    $env:JAVA_HOME = $JavaHome
    $signer = Join-Path $tools 'apksigner.bat'
    $debugKeystore = Join-Path $env:USERPROFILE '.android/debug.keystore'
    & $signer sign --ks $debugKeystore --ks-pass 'pass:android' --key-pass 'pass:android' --out $apk $aligned
    if ($LASTEXITCODE) { throw 'APK 调试签名失败。' }
    & $signer verify $apk
    if ($LASTEXITCODE) { throw 'APK 签名验证失败。' }
} finally { $env:JAVA_HOME = $originalJavaHome }

"$mode APK $apk"
'Native libraries ' + (($modulePaths.Keys | Sort-Object) -join ', ')
