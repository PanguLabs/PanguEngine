#include "PluginJson.h"
#include "PathEncoding.h"
#include "PluginDiscovery.h"

#include <string>
#include <string_view>
#include <vector>

namespace pg::build::plugin
{
namespace
{
void AppendQuoted(std::string& output, std::string_view text)
{
    constexpr char kDigits[] = "0123456789abcdef";
    output += '"';
    for (const unsigned char value : text)
    {
        switch (value)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (value < 0x20)
            {
                output += "\\u00";
                output += kDigits[value >> 4];
                output += kDigits[value & 0x0f];
            }
            else
            {
                output += static_cast<char>(value);
            }
        }
    }
    output += '"';
}
} // namespace

std::string PluginsToJson(const std::vector<PluginEntry>& plugins)
{
    std::string output = "{\"plugins\":[";
    for (const auto& entry : plugins)
    {
        if (output.back() != '[')
        {
            output += ',';
        }
        output += "{\"id\":";
        AppendQuoted(output, entry.Metadata.Id);
        output += ",\"origin\":";
        AppendQuoted(output, entry.Origin == PluginOrigin::kEngine ? "engine" : "project");
        output += ",\"name\":";
        AppendQuoted(output, entry.Metadata.Name);
        output += ",\"version\":";
        AppendQuoted(output, entry.Metadata.Version);
        output += ",\"source_dir\":";
        AppendQuoted(output, PathToUtf8(entry.SourceDir, true));
        output += ",\"manifest_path\":";
        AppendQuoted(output, PathToUtf8(entry.ManifestPath, true));
        output += ",\"configure_inputs\":[";
        for (const auto& input : entry.ConfigureInputs)
        {
            if (output.back() != '[')
            {
                output += ',';
            }
            AppendQuoted(output, PathToUtf8(input, true));
        }
        output += ']';
        output += '}';
    }
    output += "]}";
    return output;
}
} // namespace pg::build::plugin
