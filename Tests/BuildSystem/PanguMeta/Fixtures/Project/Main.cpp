#include <PanguLayoutValueExport.h>
#include <iostream>

PG_LAYOUT_VALUE_API int EngineValue();

int main() { std::cout << "value=" << EngineValue() << '\n'; }
