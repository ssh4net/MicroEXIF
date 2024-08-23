#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// Structure to hold the EXIF tag
struct ExifTag {
	uint16_t tag;  // Tag ID
	uint16_t type; // Data type (e.g., ASCII, BYTE, SHORT, LONG, RATIONAL)
	uint32_t count; // Number of data items
	std::variant<uint32_t, std::string, std::vector<uint8_t>> value; // Value (or offset placeholder)

	// Constructors
	ExifTag(uint16_t t, uint16_t tp, uint32_t c, uint32_t v)
		: tag(t), type(tp), count(c), value(v) {}

	ExifTag(uint16_t t, uint16_t tp, std::string v)
		: tag(t), type(tp), count(v.size() + 1), value(std::move(v)) {} // +1 for null terminator

	ExifTag(uint16_t t, uint16_t tp, std::vector<uint8_t> v)
		: tag(t), type(tp), count(v.size()), value(std::move(v)) {}
};

// Class to build the EXIF data structure
class ExifBuilder {
private:
    std::vector<ExifTag> tags;             // List of EXIF tags
    std::vector<std::shared_ptr<std::vector<uint8_t>>> extraDataPtrs; // Pointers to extra data (avoids unnecessary copies)

public:
    void addTag(ExifTag&& tag) {
        tags.push_back(std::move(tag));
    }

    std::vector<uint8_t> buildExifBlob() {
        std::vector<uint8_t> exifBlob;

        // Placeholder for APP1 header (to be filled in later)
        exifBlob.insert(exifBlob.end(), { 0xFF, 0xE1, 0x00, 0x00 }); // APP1 marker, length placeholder
        exifBlob.insert(exifBlob.end(), { 'E', 'x', 'i', 'f', 0x00, 0x00 }); // "Exif" identifier and padding

        // Write TIFF Header
        writeHeader(exifBlob);

        // Number of directory entries
        writeUInt16(exifBlob, tags.size());

        // Process each tag
        size_t dataOffset = 8 + 2 + (tags.size() * 12) + 4;  // TIFF header (8), IFD count (2), entries, next IFD offset (4)
        for (auto& tag : tags) {
            writeUInt16(exifBlob, tag.tag);
            writeUInt16(exifBlob, tag.type);
            writeUInt32(exifBlob, tag.count);

            if (tagFitsInField(tag)) {
                writeTagValue(exifBlob, tag);
            }
            else {
                writeUInt32(exifBlob, dataOffset);
                appendExtraData(tag, dataOffset);
            }
        }

        // Write the next IFD offset (0 indicates no more IFDs)
        writeUInt32(exifBlob, 0);

        // Append the extra data (strings, RATIONALs, etc.)
        for (const auto& dataPtr : extraDataPtrs) {
            exifBlob.insert(exifBlob.end(), dataPtr->begin(), dataPtr->end());
        }

        // Update the APP1 segment length
        uint16_t exifLength = static_cast<uint16_t>(exifBlob.size() - 2); // Length excluding the APP1 marker (FF E1)
        exifBlob[2] = (exifLength >> 8) & 0xFF;
        exifBlob[3] = exifLength & 0xFF;

        return exifBlob;
    }

private:
    void writeHeader(std::vector<uint8_t>& buffer) {
        buffer.insert(buffer.end(), { 0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08 });  // Big-endian, TIFF header, first IFD offset
    }

    void writeUInt16(std::vector<uint8_t>& buffer, uint16_t value) {
        buffer.push_back(value >> 8);
        buffer.push_back(value & 0xFF);
    }

    void writeUInt32(std::vector<uint8_t>& buffer, uint32_t value) {
        buffer.push_back(value >> 24);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }

    void writeTagValue(std::vector<uint8_t>& buffer, const ExifTag& tag) {
        if (std::holds_alternative<uint32_t>(tag.value)) {
            writeUInt32(buffer, std::get<uint32_t>(tag.value));
        }
        else if (std::holds_alternative<std::string>(tag.value)) {
            const std::string& str = std::get<std::string>(tag.value);
            std::vector<uint8_t> paddedStr(4, 0);
            std::copy(str.begin(), str.begin() + std::min<size_t>(4, str.size()), paddedStr.begin());
            buffer.insert(buffer.end(), paddedStr.begin(), paddedStr.end());
        }
        else if (std::holds_alternative<std::vector<uint8_t>>(tag.value)) {
            const std::vector<uint8_t>& data = std::get<std::vector<uint8_t>>(tag.value);
            buffer.insert(buffer.end(), data.begin(), data.end());
            if (data.size() < 4) {
                buffer.insert(buffer.end(), 4 - data.size(), 0);  // Padding
            }
        }
    }

    bool tagFitsInField(const ExifTag& tag) {
        if (std::holds_alternative<uint32_t>(tag.value)) {
            return true;
        }
        else if (std::holds_alternative<std::string>(tag.value)) {
            return std::get<std::string>(tag.value).size() <= 4;
        }
        else if (std::holds_alternative<std::vector<uint8_t>>(tag.value)) {
            return std::get<std::vector<uint8_t>>(tag.value).size() <= 4;
        }
        return false;
    }

    void appendExtraData(const ExifTag& tag, size_t& dataOffset) {
        if (std::holds_alternative<std::string>(tag.value)) {
            auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::get<std::string>(tag.value).begin(), std::get<std::string>(tag.value).end());
            dataPtr->push_back(0);  // Null-terminate string
            extraDataPtrs.push_back(dataPtr);
            dataOffset += dataPtr->size();
        }
        else if (std::holds_alternative<std::vector<uint8_t>>(tag.value)) {
            auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::get<std::vector<uint8_t>>(tag.value));
            extraDataPtrs.push_back(dataPtr);
            dataOffset += dataPtr->size();
        }
    }
};

////////////////////////////////////////////////////////////////////////////////////
/// 

// Function to read a JPEG file into a dynamically allocated array
uint8_t* readJpegFile(const std::string& filename, size_t& fileSize) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file.");
    }

    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    uint8_t* buffer = new uint8_t[fileSize];
    if (!file.read(reinterpret_cast<char*>(buffer), fileSize)) {
        delete[] buffer;
        throw std::runtime_error("Error reading file.");
    }

    return buffer;
}

// Function to find the FFDB marker (0xFFDB)
size_t findFFDBMarker(const uint8_t* jpegData, size_t fileSize) {
    for (size_t i = 0; i < fileSize - 1; ++i) {
        if (jpegData[i] == 0xFF && jpegData[i + 1] == 0xDB) {
            return i;
        }
    }
    throw std::runtime_error("FFDB marker not found.");
}

// Function to write the new JPEG file with the injected EXIF data
void writeNewJpegWithExif(const std::string& originalFile, const std::string& newFile, const uint8_t* exifBlob, size_t exifSize) {
    size_t fileSize = 0;
    uint8_t* jpegData = readJpegFile(originalFile, fileSize);

    // Find the position of the FFDB marker
    size_t ffdBMarkerPos = findFFDBMarker(jpegData, fileSize);

    // Create and write to the new file
    std::ofstream outputFile(newFile, std::ios::binary);
    if (!outputFile.is_open()) {
        delete[] jpegData;
        throw std::runtime_error("Unable to create output file.");
    }

    // Write bytes from the start of the file to the FFDB marker position
    outputFile.write(reinterpret_cast<const char*>(jpegData), ffdBMarkerPos);

    // Write the EXIF blob
    outputFile.write(reinterpret_cast<const char*>(exifBlob), exifSize);

    // Write the rest of the original JPEG file starting from the FFDB marker
    outputFile.write(reinterpret_cast<const char*>(jpegData + ffdBMarkerPos), fileSize - ffdBMarkerPos);

    outputFile.close();
    delete[] jpegData;
}

////////////////////////////////////////////////////////////////////////////////////
int main() {
	ExifBuilder builder;

    // 0x0001 - byte, 0x0002 - ascii, 0x0003 - short, 0x0004 - long, 0x0005 - rational

	builder.addTag(ExifTag(0x010F, 0x0002, "EVT"));
	builder.addTag(ExifTag(0x0110, 0x0002, "HB-25000-SB-C"));
	uint32_t xResolution[2] = { 300, 1 };  // 300/1
	builder.addTag(ExifTag(0x011A, 0x0005, std::vector<uint8_t>(reinterpret_cast<uint8_t*>(xResolution), reinterpret_cast<uint8_t*>(xResolution) + sizeof(xResolution))));
	uint32_t yResolution[2] = { 300, 1 };  // 300/1
	builder.addTag(ExifTag(0x011B, 0x0005, std::vector<uint8_t>(reinterpret_cast<uint8_t*>(yResolution), reinterpret_cast<uint8_t*>(yResolution) + sizeof(yResolution))));
	uint32_t resolutionUnit = 2;		   // Inch
	builder.addTag(ExifTag(0x0128, 0x0003, 1, resolutionUnit));
	uint32_t YCbCrPositioning = 1;         // Centered
	builder.addTag(ExifTag(0x0213, 0x0003, 1, YCbCrPositioning));

	// Build EXIF blob
	std::vector<uint8_t> exifBlob = builder.buildExifBlob();

	// Output EXIF blob for debugging
	size_t i = 0;
	for (auto byte : exifBlob) {
		if (i % 16 == 0 && i != 0) {
			printf("\n");
		}
		else if (i % 8 == 0 && i % 16 != 0) {
			// Add extra spacing every 8 bytes
			printf(" ");
		}
		printf("%02X ", byte);
		++i;
	}
	printf("\n");

    try {
        // Inject EXIF data into a new JPEG file
        std::string originalFile = "x:/4DTEMP/24.08.22_13.45.15_10bit/0DC_14_2005513/000001.jpg";
        std::string newFile = "x:/4DTEMP/24.08.22_13.45.15_10bit/0DC_14_2005513/000001_exif.jpg";
        writeNewJpegWithExif(originalFile, newFile, exifBlob.data(), exifBlob.size());

        std::cout << "EXIF data injected and new file created: " << newFile << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

	return 0;
}