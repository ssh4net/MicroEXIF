/*
MIT License

Copyright (c) 2025 Erium Vladlen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <variant>

////////////////////////////////////////////////////////////////////////////////////
// ExifTag structure:
// 
// - tag: The tag ID (e.g., 0x010F for Manufacturer)
// 
// - type:
//   0x0001 - BYTE (8-bit unsigned integer)
//   0x0002 - ASCII (8-bit byte containing one 7-bit ASCII code)
//   0x0003 - SHORT (16-bit unsigned integer)
//   0x0004 - LONG (32-bit unsigned integer)
//   0x0005 - RATIONAL (Two LONGs: numerator and denominator)
//   0x0007 - UNDEFINED (8-bit byte that can take any value depending on the field definition)
//   0x0009 - SLONG (32-bit signed integer)
//   0x000A - SRATIONAL (Two SLONGs: numerator and denominator)
//
struct ExifTag {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    std::vector<uint8_t> value;

    // Constructor for 8-bit integer values (BYTE)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint8_t val)
        : tag(t), type(tp), count(cnt), value({ val }) {}

    // Constructor for 16-bit integer values (SHORT)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint16_t val)
        : tag(t), type(tp), count(cnt), value(2) {
        std::memcpy(value.data(), &val, 2);
    }

    // Constructor for 32-bit integer values (LONG)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint32_t val)
        : tag(t), type(tp), count(cnt), value(4) {
        std::memcpy(value.data(), &val, 4);
    }

    // Constructor for RATIONAL (Two 32-bit integers: numerator and denominator)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint32_t num, uint32_t denom)
        : tag(t), type(tp), count(cnt), value(8) {
        std::memcpy(value.data(), &num, 4);
        std::memcpy(value.data() + 4, &denom, 4);
    }

    // Constructor for SLONG (32-bit signed integer)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, int32_t val)
		: tag(t), type(tp), count(cnt), value(4) {
		std::memcpy(value.data(), &val, 4);
	}

    // Constructor for SRATIONAL (Two 32-bit signed integers: numerator and denominator)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, int32_t num, int32_t denom)
        : tag(t), type(tp), count(cnt), value(8) {
        std::memcpy(value.data(), &num, 4);
        std::memcpy(value.data() + 4, &denom, 4);
	}

    // Constructor for string values, copying the string into the vector
    ExifTag(uint16_t t, uint16_t tp, const std::string& val)
        : tag(t), type(tp), count(static_cast<uint32_t>(val.size() + 1)), value(val.begin(), val.end()) {
        value.push_back('\0'); // Null-terminate the string
    }
};

// ExifBuilder class
class ExifBuilder {
private:
    std::vector<ExifTag> tags;          // List of EXIF tags
    std::vector<uint8_t> extraData;     // Buffer for extra data (strings, RATIONALs, etc.)

public:
    void addTag(ExifTag&& tag) {
        tags.push_back(std::move(tag));
    }

    std::vector<uint8_t> buildExifBlob() {
        std::vector<uint8_t> exifBlob;
        
        bool bigendian = true;

        // Placeholder for APP1 header (to be filled in later)
        exifBlob.insert(exifBlob.end(), { 0xFF, 0xE1, 0x00, 0x00 });            // APP1 marker, length placeholder
        exifBlob.insert(exifBlob.end(), { 'E', 'x', 'i', 'f', 0x00, 0x00 });    // "Exif" identifier and padding

        // Write TIFF Header
        appendUInt16(exifBlob, bigendian ? 0x4D4D : 0x4949);    // Big-endian indicator
        appendUInt16(exifBlob, 0x002A, bigendian);              // TIFF version
        appendUInt32(exifBlob, 0x00000008, bigendian);          // Offset to the first IFD

        // Number of directory entries
        appendUInt16(exifBlob, static_cast<uint16_t>(tags.size()), bigendian);

        // Calculate data offset (just after IFD entries and next IFD offset)
        size_t dataOffset = 8 + 2 + (tags.size() * 12) + 4;

        // Process each tag
        for (auto& tag : tags) {
            appendUInt16(exifBlob, tag.tag, bigendian);
            appendUInt16(exifBlob, tag.type, bigendian);
            appendUInt32(exifBlob, tag.count, bigendian);

            if (tagFitsInField(tag)) {
                writeTagValue(exifBlob, tag, bigendian); // Write values directly as is
            }
            else {
                appendUInt32(exifBlob, static_cast<uint32_t>(dataOffset), bigendian);
                appendExtraData(tag, dataOffset, bigendian);
            }
        }

        // Write the next IFD offset (0 indicates no more IFDs)
        appendUInt32(exifBlob, 0);

        // Append the extra data (strings, RATIONALs, etc.)
        exifBlob.insert(exifBlob.end(), extraData.begin(), extraData.end());

        // Update the APP1 segment length
        uint16_t exifLength = static_cast<uint16_t>(exifBlob.size() - 2); // Length excluding the APP1 marker (FF E1)
        exifBlob[2] = (exifLength >> 8) & 0xFF;
        exifBlob[3] = exifLength & 0xFF;

        return exifBlob;
    }

private:
    // Corrected function to append a 16-bit integer in big-endian format to a vector
    static void appendUInt16(std::vector<uint8_t>& vec, uint16_t value, bool bigendian = true) {
		if (bigendian) {
			vec.push_back((value >> 8) & 0xFF);
			vec.push_back(value & 0xFF);
        }
        else {
            vec.push_back(value & 0xFF);
            vec.push_back((value >> 8) & 0xFF);
        }
    }

    // Corrected function to append a 32-bit integer in big-endian format to a vector
    static void appendUInt32(std::vector<uint8_t>& vec, uint32_t value, bool bigendian = true) {
        if (bigendian) {
            vec.push_back((value >> 24) & 0xFF);
            vec.push_back((value >> 16) & 0xFF);
            vec.push_back((value >> 8) & 0xFF);
            vec.push_back(value & 0xFF);
        } else {
			vec.push_back(value & 0xFF);
			vec.push_back((value >> 8) & 0xFF);
			vec.push_back((value >> 16) & 0xFF);
			vec.push_back((value >> 24) & 0xFF);
		}
    }

    void writeTagValue(std::vector<uint8_t>& buffer, const ExifTag& tag, bool bigendian = true) {
        // byte order alwas from left to the right.
        // in case of SHORT, added a padding 0 byte to the right.
        // in case of less 4-bytes STRING, added a padding 0 byte to the right,
        // otherwise use an offset to the extra data.
        // big endian similar to the standard writing, little endian inverted (intel/x86/x64).
        size_t bufSize = buffer.size();
        switch (tag.type) {
        case 0x0001: // BYTE
            buffer.resize(bufSize + 4, 0);
            buffer[bufSize] = tag.value[0];
            break;
        case 0x0003: // SHORT
            buffer.resize(bufSize + 4, 0);
            buffer[bufSize + (bigendian ? 1 : 0)] = tag.value[0];
            buffer[bufSize + (bigendian ? 0 : 1)] = tag.value[1];
            break;
        case 0x0004: // LONG
        case 0x0009: // SLONG
            buffer.resize(bufSize + 4, 0);
            buffer[bufSize + (bigendian ? 3 : 0)] = tag.value[0];
            buffer[bufSize + (bigendian ? 2 : 1)] = tag.value[1];
            buffer[bufSize + (bigendian ? 1 : 2)] = tag.value[2];
            buffer[bufSize + (bigendian ? 0 : 3)] = tag.value[3];
            break;
        case 0x0002: // ASCII
            buffer.resize(bufSize + tag.value.size(), 0);
            std::copy(tag.value.begin(), tag.value.end(), buffer.begin() + bufSize);
            break;
        }
    }

    bool tagFitsInField(const ExifTag& tag) const {
        if (tag.type == 0x0001 || tag.type == 0x0003 || tag.type == 0x0004 || tag.type == 0x0009 ) {
            return true;
        }
        else if (tag.type == 0x0002) {
            return tag.value.size() <= 4;
        }
        // tag.type == 0x0005 (RATIONAL) is always stored in extra data
        return false;
    }

    void appendExtraData(const ExifTag& tag, size_t& dataOffset, bool bigendian) {
		const auto& data = tag.value;
		if (bigendian || tag.type == 0x0002) {
			extraData.insert(extraData.end(), data.begin(), data.end());
			dataOffset += data.size();
		}
		else {
			for (size_t i = data.size(); i > 0; --i) {
				extraData.push_back(data[i - 1]);
			}
			dataOffset += data.size();
		}
        // add a padding 0 byte.
        if (data.size() % 2 != 0) {
            extraData.push_back(0);
            ++dataOffset;
        }
    }
};
