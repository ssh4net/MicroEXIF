#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <variant>

struct ExifTag {
    uint16_t tag;                  // Tag ID
    uint16_t type;                 // Data type (e.g., ASCII, BYTE, SHORT, LONG, RATIONAL)
    uint32_t count;                // Number of data items
    std::variant<uint32_t, std::string, std::vector<uint8_t>> value; // Either a direct value, a string, or raw data

    ExifTag(uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
        : tag(tag), type(type), count(count), value(value) {}

    ExifTag(uint16_t tag, uint16_t type, const std::string& value)
        : tag(tag), type(type), count(value.size() + 1), value(value) {}  // +1 for null terminator

    ExifTag(uint16_t tag, uint16_t type, const std::vector<uint8_t>& value)
        : tag(tag), type(type), count(value.size()), value(value) {}
};
