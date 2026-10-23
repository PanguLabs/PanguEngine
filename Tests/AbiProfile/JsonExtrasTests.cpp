// json.hpp undefines this configuration macro after consuming it.
#if !defined(JSON_USE_GLOBAL_UDLS) || JSON_USE_GLOBAL_UDLS != 0
#error "Pangu::NlohmannJson must disable global JSON literal imports"
#endif

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR == 12 &&
              NLOHMANN_JSON_VERSION_PATCH == 0);

namespace
{
[[nodiscard]] bool Run()
{
    // Explicit literals are test-only; arbitrary ABI input still passes ParseUnique.
    using namespace nlohmann::literals;
    const nlohmann::json fixture = R"({"value":9,"name":"test"})"_json;
    const auto pointer = "/value"_json_pointer;
    if (fixture.at(pointer) != 9)
    {
        return false;
    }

    // Stable DOM APIs are used for ABI inputs; parse failures must not throw.
    const std::string text = R"({"kind":"schema","target":{"triple":"x86_64-pc-windows-msvc"},"count":3})";
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object() || !document.contains("target"))
    {
        return false;
    }
    if (document.at("target").at("triple") != "x86_64-pc-windows-msvc")
    {
        return false;
    }
    if (document.at("count") != 3)
    {
        return false;
    }

    return nlohmann::json::parse("{", nullptr, false).is_discarded();
}
} // namespace

int main()
{
    const bool passed = Run();
    if (!passed)
    {
        std::cerr << "JSON 3.12.0 compatibility test failed\n";
    }
    return passed ? 0 : 1;
}
