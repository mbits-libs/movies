from ...parser import file_pos

simple_types = {
    "string": [None, "string_type"],
    "ascii": ["<string>", "std::string"],
    "string_view": [None, "string_view_type"],
    "path": ["<filesystem>", "std::filesystem::path"],
    "timestamp": ["<date/date.h>", "date::sys_seconds"],
    "int8_t": ["<cstdint>", "std::int8_t"],
    "int16_t": ["<cstdint>", "std::int16_t"],
    "int32_t": ["<cstdint>", "std::int32_t"],
    "int64_t": ["<cstdint>", "std::int64_t"],
    "uint8_t": ["<cstdint>", "std::uint8_t"],
    "uint16_t": ["<cstdint>", "std::uint16_t"],
    "uint32_t": ["<cstdint>", "std::uint32_t"],
    "uint64_t": ["<cstdint>", "std::uint64_t"],
    "conv_result": ["<json/serdes.hpp>", "json::conv_result"],
    "dict": ["<json/json.hpp>", "json::map"],
    "str_map": ["<map>", "std::map<std::u8string, std::u8string>"],
    "navigator": [None, "tangle::nav::navigator"],
    "uri": [None, "tangle::uri"],
}

builtin_types = [
    "void",
    "int",
    "unsigned",
    "short",
    "long long",
    "char",
    "bool",
    "alpha_2_aliases",
    "object",
    "tuple",
    "list",
]
