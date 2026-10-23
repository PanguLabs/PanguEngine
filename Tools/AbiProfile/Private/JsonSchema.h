#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <regex>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pg::abi
{
using Json = nlohmann::json;

[[nodiscard]] inline const Json* Find(const Json& object, std::string_view key)
{
    if (!object.is_object())
    {
        return nullptr;
    }
    const auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : &it.value();
}

[[nodiscard]] inline std::string_view Text(const Json& value) { return value.get_ref<const Json::string_t&>(); }

struct ParseDiagnostic
{
    std::optional<std::size_t> ByteOffset;
};

// Used only after a syntax failure, to obtain the public SAX parser's byte position.
class JsonErrorPosition final : public nlohmann::json_sax<Json>
{
public:
    std::optional<std::size_t> ByteOffset;

    bool null() override { return true; }
    bool boolean(bool /*value*/) override { return true; }
    bool number_integer(Json::number_integer_t /*value*/) override { return true; }
    bool number_unsigned(Json::number_unsigned_t /*value*/) override { return true; }
    bool number_float(Json::number_float_t /*value*/, const Json::string_t& /*token*/) override { return true; }
    bool string(Json::string_t& /*value*/) override { return true; }
    bool binary(Json::binary_t& /*value*/) override { return true; }
    bool start_object(std::size_t /*elements*/) override { return true; }
    bool key(Json::string_t& /*value*/) override { return true; }
    bool end_object() override { return true; }
    bool start_array(std::size_t /*elements*/) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t position, const std::string& /*token*/, const Json::exception& /*error*/) override
    {
        ByteOffset = position;
        return false;
    }
};

// nlohmann::json normally replaces duplicate object keys. Do not silently accept them.
[[nodiscard]] inline std::optional<Json> ParseUnique(std::string_view text, ParseDiagnostic* diagnostic = nullptr)
{
    if (diagnostic != nullptr)
    {
        diagnostic->ByteOffset.reset();
    }
    // The parser treats a raw NUL as EOF even with iterator bounds. JSON permits
    // escaped U+0000 inside strings, but never an actual NUL byte in the input.
    const auto nul = text.find('\0');
    if (text.empty() || nul != std::string_view::npos)
    {
        if (diagnostic != nullptr && nul != std::string_view::npos)
        {
            diagnostic->ByteOffset = nul + 1;
        }
        return std::nullopt;
    }
    bool duplicate = false;
    std::vector<std::unordered_set<std::string>> keys;
    const auto callback = [&](int /*depth*/, Json::parse_event_t event, Json& value)
    {
        if (event == Json::parse_event_t::object_start)
        {
            keys.emplace_back();
        }
        else if (event == Json::parse_event_t::object_end)
        {
            keys.pop_back();
        }
        else if (event == Json::parse_event_t::key &&
                 (keys.empty() || !keys.back().emplace(value.get_ref<const std::string&>()).second))
        {
            duplicate = true;
        }
        return true;
    };
    Json value = Json::parse(text.begin(), text.end(), callback, false);
    if (duplicate || value.is_discarded())
    {
        if (diagnostic != nullptr && value.is_discarded())
        {
            JsonErrorPosition position;
            (void)Json::sax_parse(text.begin(), text.end(), &position); // Failure already known; capture its position.
            diagnostic->ByteOffset = position.ByteOffset;
        }
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline bool HasUniqueKeys(std::string_view text) { return ParseUnique(text).has_value(); }

[[nodiscard]] inline bool MatchType(std::string_view name, const Json& value)
{
    if (name == "object")
    {
        return value.is_object();
    }
    if (name == "array")
    {
        return value.is_array();
    }
    if (name == "string")
    {
        return value.is_string();
    }
    if (name == "integer")
    {
        return value.is_number_integer() || value.is_number_unsigned();
    }
    if (name == "boolean")
    {
        return value.is_boolean();
    }
    if (name == "null")
    {
        return value.is_null();
    }
    if (name == "number")
    {
        return value.is_number();
    }
    return false;
}

// Evaluate only the vocabulary present in the pinned ABI v1 Schema.
// No exception-catching paths: the Schema is checked against a pinned digest.
struct ValidationPath
{
    std::array<char, 256> Bytes{};
    std::size_t Size{};
    bool Truncated{};

    void Append(char value) noexcept
    {
        if (Size == Bytes.size())
        {
            Truncated = true;
            return;
        }
        Bytes.at(Size++) = value;
    }

    [[nodiscard]] ValidationPath Child(std::string_view token) const noexcept
    {
        auto child = *this;
        child.Append('/');
        for (const char value : token)
        {
            if (value == '~' || value == '/')
            {
                child.Append('~');
                child.Append(value == '~' ? '0' : '1');
            }
            else
            {
                child.Append(value);
            }
        }
        return child;
    }

    [[nodiscard]] std::string_view View() const noexcept { return {Bytes.data(), Size}; }
};

struct ValidationDiagnostic
{
    ValidationPath Path;
    std::source_location Origin = std::source_location::current();
};

[[nodiscard]] inline bool Reject(ValidationDiagnostic* diagnostic, const ValidationPath& path,
                                 std::source_location origin = std::source_location::current()) noexcept
{
    if (diagnostic != nullptr)
    {
        diagnostic->Path = path;
        diagnostic->Origin = origin;
    }
    return false;
}

// Recursion follows the finite pinned Schema; recursive $ref is not supported.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline bool Validate(const Json& schema, const Json& input, ValidationDiagnostic* diagnostic = nullptr,
                                   const ValidationPath& path = {})
{
    if (schema.is_boolean())
    {
        return schema.get<bool>() || Reject(diagnostic, path);
    }
    if (!schema.is_object())
    {
        return Reject(diagnostic, path);
    }

    if (const auto* expected = Find(schema, "type");
        expected != nullptr && (!expected->is_string() || !MatchType(Text(*expected), input)))
    {
        return Reject(diagnostic, path);
    }
    if (const auto* expected = Find(schema, "const"); expected != nullptr && input != *expected)
    {
        return Reject(diagnostic, path);
    }
    if (const auto* choices = Find(schema, "enum");
        choices != nullptr && (!choices->is_array() || std::ranges::none_of(*choices, [&](const auto& candidate)
                                                                            { return input == candidate; })))
    {
        return Reject(diagnostic, path);
    }

    if (input.is_string())
    {
        if (const auto* len = Find(schema, "minLength"); len != nullptr && Text(input).size() < len->get<std::size_t>())
        {
            return Reject(diagnostic, path);
        }
        if (const auto* pattern = Find(schema, "pattern"); pattern != nullptr)
        {
            const std::regex expression(std::string(Text(*pattern)));
            if (!std::regex_search(Text(input).begin(), Text(input).end(), expression))
            {
                return Reject(diagnostic, path);
            }
        }
    }
    if (input.is_object())
    {
        if (const auto* required = Find(schema, "required"); required != nullptr)
        {
            for (const auto& key : *required)
            {
                if (!input.contains(key.get_ref<const std::string&>()))
                {
                    return Reject(diagnostic, path.Child(Text(key)));
                }
            }
        }
        if (const auto* properties = Find(schema, "properties"); properties != nullptr)
        {
            for (auto it = input.begin(); it != input.end(); ++it)
            {
                if (const auto* rule = Find(*properties, it.key()); rule != nullptr)
                {
                    if (!Validate(*rule, it.value(), diagnostic, path.Child(it.key())))
                    {
                        return false;
                    }
                }
                else if (const auto* extra = Find(schema, "additionalProperties");
                         extra != nullptr && !Validate(*extra, it.value(), diagnostic, path.Child(it.key())))
                {
                    return false;
                }
            }
        }
    }
    if (input.is_array())
    {
        if (const auto* minItems = Find(schema, "minItems");
            minItems != nullptr && input.size() < minItems->get<std::size_t>())
        {
            return Reject(diagnostic, path);
        }
        if (const auto* maxItems = Find(schema, "maxItems");
            maxItems != nullptr && input.size() > maxItems->get<std::size_t>())
        {
            return Reject(diagnostic, path);
        }
        if (const auto* prefix = Find(schema, "prefixItems"); prefix != nullptr)
        {
            for (std::size_t i = 0; i < input.size() && i < prefix->size(); ++i)
            {
                std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> index{};
                const auto converted = std::to_chars(index.data(), std::to_address(index.end()), i);
                if (converted.ec != std::errc{})
                {
                    return Reject(diagnostic, path);
                }
                const auto token = std::string_view(index.data(), converted.ptr);
                if (!Validate(prefix->at(i), input.at(i), diagnostic, path.Child(token)))
                {
                    return false;
                }
            }
        }
        if (const auto* contains = Find(schema, "contains"); contains != nullptr)
        {
            std::size_t found = 0;
            for (const auto& item : input)
            {
                found += Validate(*contains, item) ? 1U : 0U;
            }
            const auto* min = Find(schema, "minContains");
            const auto* max = Find(schema, "maxContains");
            if (found < (min != nullptr ? min->get<std::size_t>() : 1U) ||
                (max != nullptr && found > max->get<std::size_t>()))
            {
                return Reject(diagnostic, path);
            }
        }
    }

    if (const auto* all = Find(schema, "allOf"); all != nullptr)
    {
        for (const auto& rule : *all)
        {
            if (!Validate(rule, input, diagnostic, path))
            {
                return false;
            }
        }
    }
    if (const auto* one = Find(schema, "oneOf"); one != nullptr)
    {
        unsigned matches = 0;
        for (const auto& rule : *one)
        {
            matches += Validate(rule, input) ? 1U : 0U;
        }
        if (matches != 1)
        {
            return Reject(diagnostic, path);
        }
    }
    if (const auto* condition = Find(schema, "if"); condition != nullptr)
    {
        const auto* branch = Find(schema, Validate(*condition, input) ? "then" : "else");
        if (branch != nullptr && !Validate(*branch, input, diagnostic, path))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool SchemaUsesSupportedKeywords(const Json& schema) // NOLINT(misc-no-recursion)
{
    if (schema.is_boolean())
    {
        return true;
    }
    if (!schema.is_object())
    {
        return false;
    }
    static const std::unordered_set<std::string_view> kSupported{
        "$schema",  "$id",      "title",       "description", "type",        "const",
        "enum",     "pattern",  "minLength",   "required",    "properties",  "additionalProperties",
        "minItems", "maxItems", "prefixItems", "contains",    "minContains", "maxContains",
        "allOf",    "oneOf",    "if",          "then",        "else",
    };
    for (auto it = schema.begin(); it != schema.end(); ++it)
    {
        if (!kSupported.contains(it.key()))
        {
            return false;
        }
        if (it.key() == "properties")
        {
            for (const auto& member : it.value().items())
            {
                if (!SchemaUsesSupportedKeywords(member.value()))
                {
                    return false;
                }
            }
        }
        else if (it.key() == "allOf" || it.key() == "oneOf" || it.key() == "prefixItems")
        {
            for (const auto& child : it.value())
            {
                if (!SchemaUsesSupportedKeywords(child))
                {
                    return false;
                }
            }
        }
        else if ((it.key() == "additionalProperties" || it.key() == "contains" || it.key() == "if" ||
                  it.key() == "then" || it.key() == "else") &&
                 !SchemaUsesSupportedKeywords(it.value()))
        {
            return false;
        }
    }
    return true;
}

// JSON v1 contains ASCII-only keys/strings and the single integer schema=1.
// std::map keys in nlohmann::json are sorted; compact dump produces frozen JCS bytes
// for this restricted vocabulary. Not a general RFC 8785 implementation.
[[nodiscard]] inline std::string CanonicalJson(const Json& value) { return value.dump(); }

// This checks declaration syntax only, not real compiler/SDK provenance.
[[nodiscard]] inline std::optional<Json> ParseValidatedProfile(std::string_view raw, const Json& schema)
{
    if (!SchemaUsesSupportedKeywords(schema))
    {
        return std::nullopt;
    }
    auto profile = ParseUnique(raw);
    if (!profile || !Validate(schema, *profile))
    {
        return std::nullopt;
    }
    return profile;
}
} // namespace pg::abi
