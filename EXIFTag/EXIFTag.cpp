#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>

// Endianness enum
enum class Endianness {
    BigEndian,
    LittleEndian
};

// Template function to write values in the specified endianness
template<typename T>
void write_value(std::vector<uint8_t>& buffer, T value, Endianness endianness) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    if (endianness == Endianness::BigEndian) {
        std::reverse(bytes, bytes + sizeof(T));  // Reverse bytes for big-endian
    }
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));  // Insert bytes into buffer
}

// Function to determine the count based on the type and value
uint32_t determine_count(uint16_t type, const void* value) {
    switch (type) {
    case 1: // BYTE
    case 2: // ASCII
        return strlen(static_cast<const char*>(value)) + 1;  // Including NULL terminator for ASCII
    case 3: // SHORT
        return 1;  // Typically one short value
    case 4: // LONG
        return 1;  // Typically one long value
    case 5: // RATIONAL
        return 1;  // Typically one rational value (numerator and denominator)
    default:
        return 0;  // Unsupported type
    }
}

// Function to generate an EXIF tag
std::vector<uint8_t> generate_exif_tag(uint16_t tag, uint16_t type, const void* value, Endianness endianness) {
    std::vector<uint8_t> exif_tag;
    uint32_t count = determine_count(type, value);
    write_value(exif_tag, tag, endianness);          // Tag
    write_value(exif_tag, type, endianness);         // Type
    write_value(exif_tag, count, endianness);        // Count

    // If the value fits within 4 bytes, place it directly in the value offset field
    if (type == 1 || type == 2 || type == 3 || type == 4) {  // BYTE, ASCII, SHORT, LONG
        uint32_t value_offset = 0;
        if (type == 3 && count == 1) {  // SHORT
            value_offset = *static_cast<const uint16_t*>(value);
        }
        else if (type == 4 && count == 1) {  // LONG
            value_offset = *static_cast<const uint32_t*>(value);
        }
        else if (type == 1 || type == 2) {  // BYTE or ASCII
            const uint8_t* bytes = static_cast<const uint8_t*>(value);
            for (size_t i = 0; i < count && i < 4; ++i) {
                value_offset |= static_cast<uint32_t>(bytes[i]) << (8 * (3 - i));
            }
        }
        write_value(exif_tag, value_offset, endianness);
    }
    else {
        // If the value is larger, just use the pointer as the offset (for simplicity, assuming value is stored elsewhere)
        write_value(exif_tag, reinterpret_cast<uintptr_t>(value), endianness);
    }

    return exif_tag;
}

// Example usage
int main() {
    // Example 1: Create an EXIF tag for Manufacturer
    uint16_t tag = 0x010F;       // Manufacturer tag
    uint16_t type = 0x0002;      // ASCII string type
    const char* manufacturer = "EVT";

    // Generate EXIF tag in big-endian format
    std::vector<uint8_t> exif_tag_be = generate_exif_tag(tag, type, manufacturer, Endianness::BigEndian);

    // Output the result in big-endian format
    std::cout << "manufacturer EXIF tag:\t";
    for (uint8_t byte : exif_tag_be) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;

    tag = 0x0110;	   // Model tag
    const char* model = "HB-25000-SB-C";

    exif_tag_be = generate_exif_tag(tag, type, model, Endianness::BigEndian);

    std::cout << "model EXIF tag:\t\t";
    for (uint8_t byte : exif_tag_be) {
		printf("%02X ", byte);
	}
    std::cout << std::endl;

    // Example 2: Create an EXIF tag for XResolution
    tag = 0x011A;       // XResolution tag
    type = 0x0004;      // LONG type
    uint32_t x_resolution = 300;

    // Generate EXIF tag in big-endian format
    exif_tag_be = generate_exif_tag(tag, type, &x_resolution, Endianness::BigEndian);

    // Output the result in big-endian format
    std::cout << "x_resolution EXIF tag:\t";
    for (uint8_t byte : exif_tag_be) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;

    return 0;
}
