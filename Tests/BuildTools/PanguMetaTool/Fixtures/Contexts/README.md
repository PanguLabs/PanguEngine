这些版本化响应文件由 ContextTests 复制到隔离测试工作目录，验证嵌套响应、带空格的字符串宏和中文 include 路径。

编译数据库由测试按实际临时目录生成，分别覆盖 `arguments` 与宿主原生 `command` 字符串语法。其相邻 CMakeCache 声明 Ninja 单配置及 Debug；双对象命令分别定义 `PG_TEST_VALUE=17/29`。

`BuildInvocation` 返回不含编译器程序的 Driver 参数，最后一个参数是指定原始头。消费者须用配套宿主编译器作为 argv[0]，并检查 Driver/Frontend 诊断，不能只依赖工具包装层返回值。

输出映射分为无 LLVM 的 `BuildOutputMapping`（固定槽位和精确声明集合）及 Frontend 的 `ValidateOutputMapping`（已有文件身份和同文件系统独占占位探测）。Unicode 名称通过真实文件系统判断。Windows 当前支持默认不区分大小写的 NTFS；Linux 要求能查询目录 flags，拒绝无法在探测树安全复现的逐目录 casefold；macOS 按本机卷的命名规则探测。探测只在 owned 输出根内创建唯一临时目录，最终槽位不写入内容，探测目录随结果清理。
