#include <string>

int pg_fixture_base();
int pg_fixture_tech_a();
int pg_fixture_module() { return pg_fixture_base() + pg_fixture_tech_a() + static_cast<int>(std::string().size()); }
