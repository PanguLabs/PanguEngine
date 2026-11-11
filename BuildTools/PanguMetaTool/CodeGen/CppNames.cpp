#include "CodeGen/CppNames.h"
#include <string>
#include <string_view>
namespace pg::meta::tool
{
std::string QuoteCppString(std::string_view value)
{
    std::string result = "\"";
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            result += '\\';
            result += static_cast<char>(byte);
        }
        else if (byte < 32 || byte == 127)
        {
            // 固定三位八进制，不会吞掉后续十六进制或数字字符。
            result += '\\';
            result += static_cast<char>('0' + ((static_cast<unsigned>(byte) >> 6U) & 7U));
            result += static_cast<char>('0' + ((static_cast<unsigned>(byte) >> 3U) & 7U));
            result += static_cast<char>('0' + (byte & 7U));
        }
        else
        {
            result += static_cast<char>(byte);
        }
    }
    result += '"';
    return result;
}
std::string EscapeIdentifier(std::string_view value)
{
    constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string result = "T_";
    for (const unsigned char byte : value)
    {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9'))
        {
            result += static_cast<char>(byte);
        }
        else
        {
            result += '_';
            result += kHex.at(static_cast<unsigned>(byte) >> 4U);
            result += kHex.at(byte & 15U);
        }
    }
    return result;
}
std::string GlobalName(std::string_view value)
{
    return value.starts_with("::") ? std::string(value) : "::" + std::string(value);
}
} // namespace pg::meta::tool
