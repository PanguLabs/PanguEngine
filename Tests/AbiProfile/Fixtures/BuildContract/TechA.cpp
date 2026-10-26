#include <string>

extern int pg_fixture_tech_b();
int pg_fixture_tech_a() { return pg_fixture_tech_b() + 1; }
