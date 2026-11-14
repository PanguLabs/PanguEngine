#!/usr/bin/env bash
# 兼容 macOS 自带 Bash 3.2；构建依赖与增量判断全部交给 CMake/Ninja。
set -euo pipefail

usage() {
    cat <<'EOF'
PanguEngine 本机构建（Linux / macOS / iOS）
用法：bash Scripts/build.sh <操作> [选项]
      bash Scripts/build.sh --action <操作> [选项]
无参数仅显示帮助，不执行构建。详细说明见 Scripts/README.md。

操作          平台              用途
help          全部              显示构建清单与参数（也可用 --help / --list）
configure     全部              仅配置 CMake
build         全部              配置并构建普通 ALL，或 --target 指定目标
runtime       全部              关闭测试、样例、基准，仅构建正式引擎目标
fast          Linux/macOS       配置、准备并运行快速测试集合
precommit     Linux/macOS       配置、准备并运行提交前测试集合
full          Linux/macOS       配置、准备并运行完整测试集合
samples       Linux/macOS       开启样例并构建 PanguMinimalHost
benchmarks    Linux/macOS       构建 Module / Log / Meta 基准，不自动采样
mobile-link   iOS               编译根工程并验证真实 Module 最终链接，仅支持 Release

选项
  --platform linux|macos|ios           默认当前桌面宿主；iOS 为 ARM64/iOS18.0
  --config Release|Debug               默认 Release
  --link-mode Modular|Monolithic      桌面默认 Modular，iOS 默认 Monolithic
  --target <目标名>                    仅用于 build，传入实际 CMake Target 名
  --jobs <正整数>                      可选构建并行数，省略时沿用现有 Preset/环境
  --fresh                             显式请求 cmake --fresh 重新配置
  --dry-run                           只显示命令，不检查工具链或执行构建

前置条件：CMake 4.0+、Ninja、LLVM_ROOT（Clang 23）；Linux 需要匹配的 libc++。
macOS/iOS 使用 MacPorts LLVM；iOS 还需要独立 iPhoneOS SDK，见 PANGU_IOS_SDK_PATH。
Linux 源码：/home/moozen/WindowsGit/PanguEngine；macOS/iOS：/Users/moozen/WindowsGit/PanguEngine。
产物目录：$HOME/PanguEngineBuild/{Linux,macOS,iOS}/{Bin,Lib}，按宿主本地文件系统隔离。
普通桌面构建开启测试；iOS 默认关闭测试。每个操作显式复位样例/基准开关。
操作会切换本平台 BuildRoot 配置，请勿与其他使用同一 BuildRoot 的任务并发运行。
mobile-link 不运行设备程序，不代表打包或签名；源码锁校验沿用现有正式验证入口。

示例：
  bash Scripts/build.sh fast
  bash Scripts/build.sh build --config Debug --target PanguEngineHost
  bash Scripts/build.sh full --link-mode Monolithic --dry-run
  bash Scripts/build.sh build --platform ios
  bash Scripts/build.sh mobile-link --platform ios --link-mode Modular
EOF
}

fail() {
    printf '错误：%s；使用 bash Scripts/build.sh help 查看用法。\n' "$*" >&2
    exit 2
}

case "$(uname -s)" in
    Linux) host_platform=linux ;;
    Darwin) host_platform=macos ;;
    *) fail '此入口需要 Linux/macOS 宿主，Windows 请使用 build.ps1' ;;
esac

action=help
action_set=0
platform=$host_platform
config=Release
link_mode=''
target=''
jobs=''
fresh=0
dry_run=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --help|-h|--list) action=help; action_set=1; shift ;;
        --fresh) fresh=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        --action|--platform|--config|--link-mode|--target|--jobs)
            option=$1
            [ "$#" -ge 2 ] && [ -n "$2" ] && [[ $2 != -* ]] || fail "$option 需要参数值"
            case "$option" in
                --action)
                    [ "$action_set" -eq 0 ] || fail '请只指定一个操作'
                    action=$2; action_set=1 ;;
                --platform) platform=$2 ;;
                --config) config=$2 ;;
                --link-mode) link_mode=$2 ;;
                --target) target=$2 ;;
                --jobs) jobs=$2 ;;
            esac
            shift 2 ;;
        -*) fail "未知选项 $1" ;;
        *)
            [ "$action_set" -eq 0 ] || fail '请只指定一个操作'
            action=$1; action_set=1; shift ;;
    esac
done

case "$action" in
    help|configure|build|runtime|fast|precommit|full|samples|benchmarks|mobile-link) ;;
    *) fail "未知操作 $action" ;;
esac
case "$platform" in
    linux|macos|ios) ;;
    *) fail 'Platform 必须为 linux、macos 或 ios' ;;
esac
case "$config" in Release|Debug) ;; *) fail 'Config 必须为 Release 或 Debug' ;; esac
case "$link_mode" in ''|Modular|Monolithic) ;; *) fail 'LinkMode 必须为 Modular 或 Monolithic' ;; esac
if [ -n "$jobs" ] && ! [[ $jobs =~ ^[1-9][0-9]*$ ]]; then fail 'Jobs 必须为正整数'; fi
if [ -n "$target" ] && [ "$action" != build ]; then fail 'Target 仅用于 build'; fi
if [ "$action" = help ]; then usage; exit 0; fi

case "$host_platform:$platform" in
    linux:linux|macos:macos|macos:ios) ;;
    *) fail "宿主 $host_platform 不能使用目标 $platform" ;;
esac
mobile=0
[ "$platform" != ios ] || mobile=1
test_action=0
case "$action" in fast|precommit|full) test_action=1 ;; esac
if [ "$mobile" -eq 1 ]; then
    case "$action" in fast|precommit|full|samples|benchmarks) fail "$action 仅用于桌面平台" ;; esac
fi
if [ "$action" = mobile-link ] && { [ "$mobile" -ne 1 ] || [ "$config" != Release ]; }; then
    fail 'mobile-link 仅用于 iOS 的现有 Release 链接验证夹具'
fi
if [ -z "$link_mode" ]; then
    if [ "$mobile" -eq 1 ]; then link_mode=Monolithic; else link_mode=Modular; fi
fi

source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
case "$platform" in
    linux) preset=linux; build_root=$HOME/PanguEngineBuild/Linux ;;
    macos) preset=macos; build_root=$HOME/PanguEngineBuild/macOS ;;
    ios) preset=ios-arm64; build_root=$HOME/PanguEngineBuild/iOS ;;
esac

run() {
    printf '> '
    printf '%q ' "$@"
    printf '\n'
    if [ "$dry_run" -eq 0 ]; then "$@"; fi
}

if [ "$dry_run" -eq 0 ]; then
    command -v cmake >/dev/null 2>&1 || { printf '缺少 cmake，请检查 PATH。\n' >&2; exit 127; }
    if [ "$test_action" -eq 1 ]; then
        command -v ctest >/dev/null 2>&1 || { printf '缺少 ctest，请检查 PATH。\n' >&2; exit 127; }
    fi
fi

testing=ON
if [ "$action" = runtime ] || { [ "$mobile" -eq 1 ] && [ "$action" != mobile-link ]; }; then testing=OFF; fi
samples=OFF
[ "$action" != samples ] || samples=ON
benchmarks=OFF
[ "$action" != benchmarks ] || benchmarks=ON
configure_args=(--preset "$preset" "-DCMAKE_BUILD_TYPE=$config" "-DPANGU_LINK_MODE=$link_mode"
    "-DBUILD_TESTING=$testing" "-DPANGU_BUILD_ENGINE_TESTS=$testing" "-DPANGU_BUILD_SAMPLES=$samples"
    "-DPANGU_BUILD_BENCHMARKS=$benchmarks" "-DPANGU_BUILD_META_BENCHMARKS=$benchmarks")
[ "$fresh" -eq 0 ] || configure_args+=(--fresh)

printf '源码：%s\n构建：%s；%s / %s；操作 %s\n' "$source_root" "$build_root" "$config" "$link_mode" "$action"
cd -- "$source_root"
run cmake "${configure_args[@]}"
if [ "$action" != configure ]; then
    build_preset=$preset
    [ "$test_action" -eq 0 ] || build_preset=$preset-$action
    build_args=(--build --preset "$build_preset")
    [ -z "$jobs" ] || build_args+=(--parallel "$jobs")
    if [ -n "$target" ]; then build_args+=(--target "$target")
    elif [ "$action" = samples ]; then build_args+=(--target PanguMinimalHost)
    elif [ "$action" = benchmarks ]; then
        build_args+=(--target PanguModuleBenchmarks PanguLogBenchmarks PanguMetaAccessBenchmarks)
    fi
    run cmake "${build_args[@]}"
fi
if [ "$test_action" -eq 1 ]; then run ctest --preset "$preset-$action"; fi
if [ "$action" = mobile-link ]; then
    link_args=("-DPANGU_ENGINE_SOURCE_DIR=$source_root"
        "-DPANGU_TEST_ROOT=$build_root/Tests/ModuleLinkModes/$link_mode" "-DPANGU_TARGET_PRESET=$preset"
        "-DPANGU_LINK_MODE=$link_mode")
    [ -z "$jobs" ] || link_args+=("-DPANGU_TEST_BUILD_JOBS=$jobs")
    run cmake "${link_args[@]}" -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
fi
