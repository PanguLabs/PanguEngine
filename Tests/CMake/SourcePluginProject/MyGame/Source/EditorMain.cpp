#include "FixtureModules.h"
#include "HostCheck.h"

int main() { return pg::fixture::VerifyCatalog(pg::fixture::EditorCatalog(), pg::fixture::kEditorExtrasId); }
