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

// ExifTag structure
struct ExifTag {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    std::variant<uint32_t, std::string, std::vector<uint8_t>> value;

    // Constructor for integer values
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint32_t val)
        : tag(t), type(tp), count(cnt), value(val) {}

    // Constructor for 16-bit integer values (SHORT)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint16_t val)
        : tag(t), type(tp), count(cnt), value(uint16ToVector(val)) {}

    // Constructor for 32-bit integer values (LONG, RATIONAL)
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, uint32_t num, uint32_t denom)
        : tag(t), type(tp), count(cnt), value(rationalToVector(num, denom)) {}

    // Constructor for string values
    ExifTag(uint16_t t, uint16_t tp, std::string val)
        : tag(t), type(tp), count(static_cast<uint32_t>(val.size()) + 1), value(std::move(val)) {}

    // Constructor for vector values with a count
    ExifTag(uint16_t t, uint16_t tp, uint32_t cnt, std::vector<uint8_t> val)
        : tag(t), type(tp), count(cnt), value(std::move(val)) {}

private:
    // Helper to convert a uint16_t to a vector in big-endian format
    static constexpr std::vector<uint8_t> uint16ToVector(uint16_t value) {
        return { static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF) };
    }

    // Helper to convert a uint32_t to a vector in big-endian format
    static constexpr std::vector<uint8_t> uint32ToVector(uint32_t value) {
        return {
            static_cast<uint8_t>((value >> 24) & 0xFF),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF)
        };
    }

    // Helper to convert a RATIONAL (two uint32_t) to a vector in big-endian format
    static std::vector<uint8_t> rationalToVector(uint32_t numerator, uint32_t denominator) {
        auto numVec = uint32ToVector(numerator);
        auto denomVec = uint32ToVector(denominator);
        numVec.insert(numVec.end(), denomVec.begin(), denomVec.end());
        return numVec;
    }
};

// ExifBuilder class
class ExifBuilder {
private:
    std::vector<ExifTag> tags; // List of EXIF tags
    std::vector<uint8_t> extraData; // Buffer for extra data (strings, RATIONALs, etc.)

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
        appendUInt16(exifBlob, 0x4D4D);  // Big-endian indicator
        appendUInt16(exifBlob, 0x002A);  // TIFF version
        appendUInt32(exifBlob, 0x00000008); // Offset to the first IFD

        // Number of directory entries
        appendUInt16(exifBlob, static_cast<uint16_t>(tags.size()));

        // Calculate data offset (just after IFD entries and next IFD offset)
        size_t dataOffset = 8 + 2 + (tags.size() * 12) + 4;

        // Process each tag
        for (auto& tag : tags) {
            appendUInt16(exifBlob, tag.tag);
            appendUInt16(exifBlob, tag.type);
            appendUInt32(exifBlob, tag.count);

            if (tagFitsInField(tag)) {
                writeTagValue(exifBlob, tag); // Write values directly as is
            }
            else {
                appendUInt32(exifBlob, static_cast<uint32_t>(dataOffset));
                appendExtraData(tag, dataOffset);
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
    // Helper function to append a 16-bit integer in big-endian format to a vector
    static void appendUInt16(std::vector<uint8_t>& vec, uint16_t value) {
        vec.push_back((value >> 8) & 0xFF);
        vec.push_back(value & 0xFF);
    }

    // Helper function to append a 32-bit integer in big-endian format to a vector
    static void appendUInt32(std::vector<uint8_t>& vec, uint32_t value) {
        vec.push_back((value >> 24) & 0xFF);
        vec.push_back((value >> 16) & 0xFF);
        vec.push_back((value >> 8) & 0xFF);
        vec.push_back(value & 0xFF);
    }

    void writeTagValue(std::vector<uint8_t>& buffer, const ExifTag& tag) {
        if (std::holds_alternative<uint32_t>(tag.value)) {
            appendUInt32(buffer, std::get<uint32_t>(tag.value));
        }
        else if (std::holds_alternative<std::string>(tag.value)) {
            const std::string& str = std::get<std::string>(tag.value);
            buffer.insert(buffer.end(), str.begin(), str.begin() + std::min<size_t>(4, str.size()));
            if (str.size() < 4) {
                buffer.insert(buffer.end(), 4 - str.size(), 0);  // Padding
            }
        }
        else if (std::holds_alternative<std::vector<uint8_t>>(tag.value)) {
            const auto& data = std::get<std::vector<uint8_t>>(tag.value);
            buffer.insert(buffer.end(), data.begin(), data.end());
            if (data.size() < 4) {
                buffer.insert(buffer.end(), 4 - data.size(), 0);  // Padding
            }
        }
    }

    bool tagFitsInField(const ExifTag& tag) const {
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
            const std::string& str = std::get<std::string>(tag.value);
            extraData.insert(extraData.end(), str.begin(), str.end());
            extraData.push_back(0);  // Null-terminate string
            dataOffset += str.size() + 1;
        }
        else if (std::holds_alternative<std::vector<uint8_t>>(tag.value)) {
            const auto& data = std::get<std::vector<uint8_t>>(tag.value);
            extraData.insert(extraData.end(), data.begin(), data.end());
            dataOffset += data.size();
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

	// Add Manufacturer tag
	builder.addTag(ExifTag(0x010F, 0x0002, "EVT"));

	// Add Model tag
	builder.addTag(ExifTag(0x0110, 0x0002, "HB-25000-SB-C"));

	// Add LensModel tag
	builder.addTag(ExifTag(0xA434, 0x0002, "F3526-MPT"));

	// Add ExposureTime tag
	builder.addTag(ExifTag(0x829A, 0x0005, 1, 1, 100));

	// Add FNumber tag 5.6
	builder.addTag(ExifTag(0x829D, 0x0005, 1, 56, 10));

	// Add ISOSpeedRatings tag
	builder.addTag(ExifTag(0x8827, 0x0003, 1, uint16_t(200)));

	// Add FocalLength tag
	builder.addTag(ExifTag(0x920A, 0x0005, 1, 35, 1));

	// Add FocalLengthIn35mmFormat tag
	builder.addTag(ExifTag(0xA405, 0x0003, 1, uint16_t(79)));

	// Add DeteTimeOriginal/CreateDate tag
	time_t rawtime;
	struct tm timeinfo;
	time(&rawtime);
	localtime_s(&timeinfo, &rawtime);
	char timeStr[20];
	strftime(timeStr, sizeof(timeStr), "%Y:%m:%d %H:%M:%S", &timeinfo);
	builder.addTag(ExifTag(0x9003, 0x0002, timeStr));
	builder.addTag(ExifTag(0x9004, 0x0002, timeStr));

	// Add Software tag
//	builder.addTag(ExifTag(0x0131, 0x0002, "ExifBuilder"));

	// Add Orientation tag (1 - top-left)
	// 1 = Horizontal (normal), 3 = Rotate 180, 6 = Rotate 90 CW, 8 = Rotate 270 CW
	builder.addTag(ExifTag(0x0112, 0x0003, 1, uint16_t(8)));

	// Add Copyright tag
//	builder.addTag(ExifTag(0x8298, 0x0002, "2024 CyberAgent, Tokyo, Japan"));

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