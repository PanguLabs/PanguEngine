#include <Check.h>

// 仅编译与最终链接；交叉目标不运行，不将此结果当作设备 Fatal 验收。
int main(int argc, char* /*argv*/[])
{
    PG_CHECK(argc == 1, "mobile fatal link fixture");
    return 0;
}
