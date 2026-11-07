#include <PanguMissingQueryExport.h>

// 故意只有无关符号，不导出固定 Query，也不导入其他 Module 的 Query。
extern "C" PG_MISSING_QUERY_API int PanguMissingQueryMarker() noexcept { return 1; }
