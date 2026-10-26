#include <string>

int pg_fixture_base() { return static_cast<int>(std::string("fixture").size()) + 35; }
