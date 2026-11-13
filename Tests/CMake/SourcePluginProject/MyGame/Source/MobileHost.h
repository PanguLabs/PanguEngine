#pragma once

#ifdef __ANDROID__
// 应用传入自身沙箱中可写的绝对日志目录；此声明也供最终链接 caller 使用。
extern "C" __attribute__((visibility("default"))) int PanguMobileHostCheck(const char* logDirectory);
#endif
