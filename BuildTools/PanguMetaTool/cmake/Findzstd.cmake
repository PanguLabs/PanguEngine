# LLVM 的包导出直接引用这个命名目标，不需要先安装另一个开发包。
if(NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static ALIAS libzstd_static)
endif()
set(zstd_LIBRARIES zstd::libzstd_static)
set(zstd_INCLUDE_DIRS "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/Repo/lib")
set(zstd_VERSION 1.5.7)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(zstd REQUIRED_VARS zstd_LIBRARIES VERSION_VAR zstd_VERSION)
