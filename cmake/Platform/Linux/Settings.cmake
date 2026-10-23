# Linux compiler, matching libc++ headers/runtime and rpath are selected and
# validated by Platform/Linux/Toolchain.cmake before project().
# Do not re-apply those flags through PanguBuildOptions: third-party C++
# targets must use the same libc++ even when they do not link that target.
