#include <iostream>
#include <vector>
#include <cstring>
#include <ctime>
#include <fstream>

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

struct ExifTag {
    uint16_t tag;       // Tag identifier
    uint16_t type;      // Data type (BYTE, ASCII, SHORT, LONG, RATIONAL, etc.)
    uint32_t count;     // Number of values
    uint32_t valueOffset;  // Value or offset to data
};

// 
ExifTag createIFDEntry(uint16_t tag, uint16_t type, uint32_t count, void* value, std::vector<uint8_t>& exifBlob) {
	ExifTag entry;
	entry.tag = tag;
	entry.type = type;
	entry.count = count;

	// Calculate size of the data
	uint32_t dataSize = 0;
	switch (type) {
	case 1: // BYTE
	case 2: // ASCII
		dataSize = count; break;
	case 3: // SHORT
		dataSize = count * 2; break;
	case 4: // LONG
		dataSize = count * 4; break;
	case 5: // RATIONAL
		dataSize = count * 8; break;
	default:
		throw std::runtime_error("Unsupported type");
	}

	// If data fits within 4 bytes, store it directly in the valueOffset field
	if (dataSize <= 4) {
		memcpy(&entry.valueOffset, value, dataSize);
	}
	else {
		// Otherwise, store the data at the end of the EXIF blob and set the offset
		entry.valueOffset = exifBlob.size();
		uint8_t* dataPtr = static_cast<uint8_t*>(value);
		exifBlob.insert(exifBlob.end(), dataPtr, dataPtr + dataSize);
	}

	return entry;
}


struct ExifHeader {
    uint16_t marker;          // 0xFFE1 (APP1)
    uint16_t length;          // Length of the segment (to be set)
    char exifString[6];       // "Exif\0\0"
    uint16_t byteOrder;       // 0x4949 (little-endian) or 0x4D4D (big-endian)
    uint16_t version;         // TIFF version (always 0x002A)
    uint16_t zeroPad;         // Always 0x0000
    uint32_t ifdOffset;       // Offset to the first IFD
};

struct ExifBlob {
    ExifHeader header;
    std::vector<ExifTag> tags;
    std::vector<uint8_t> dataBlob; // Contains both IFD entries and tag data
};

std::vector<uint8_t> createExifData(ExifBlob&& exifBlob) {
    // Calculate the total length in advance
    size_t totalSize = 2 + 2 + 6 + 2 + 2 + 2 + 4 // Header parts
        + sizeof(uint16_t) + (exifBlob.tags.size() * sizeof(ExifTag)) + exifBlob.dataBlob.size();

    // Allocate the exact amount of space in the vector
    std::vector<uint8_t> exifData;
    exifData.reserve(totalSize);

    // Insert the EXIF header directly
    uint16_t marker = 0xE1FF; // APP1 marker
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&marker), reinterpret_cast<uint8_t*>(&marker) + sizeof(marker));

    uint16_t length = static_cast<uint16_t>(totalSize - 2); // Subtract 2 for the marker size
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&length), reinterpret_cast<uint8_t*>(&length) + sizeof(length));

    const char exifString[6] = { 'E', 'x', 'i', 'f', '\0', '\0' };
    exifData.insert(exifData.end(), exifString, exifString + 6);

    uint16_t byteOrder = 0x4D4D; // Big-endian
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&byteOrder), reinterpret_cast<uint8_t*>(&byteOrder) + sizeof(byteOrder));

    uint16_t version = 0x002A;   // TIFF version
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&version), reinterpret_cast<uint8_t*>(&version) + sizeof(version));

    uint16_t zeroPad = 0x0000;   // Zero padding
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&zeroPad), reinterpret_cast<uint8_t*>(&zeroPad) + sizeof(zeroPad));

    uint32_t ifdOffset = 0x00000008; // Offset to the first IFD
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&ifdOffset), reinterpret_cast<uint8_t*>(&ifdOffset) + sizeof(ifdOffset));

    // Insert the number of tags
    uint16_t numTags = static_cast<uint16_t>(exifBlob.tags.size());
    exifData.insert(exifData.end(),
        reinterpret_cast<uint8_t*>(&numTags),
        reinterpret_cast<uint8_t*>(&numTags) + sizeof(numTags));

    // Insert all tags (IFD entries)
    exifData.insert(exifData.end(),
        reinterpret_cast<uint8_t*>(exifBlob.tags.data()),
        reinterpret_cast<uint8_t*>(exifBlob.tags.data()) + (exifBlob.tags.size() * sizeof(ExifTag)));

    // Move the tag data from exifBlob into exifData (this avoids copying)
    exifData.insert(exifData.end(),
        std::make_move_iterator(exifBlob.dataBlob.begin()),
        std::make_move_iterator(exifBlob.dataBlob.end()));

    return exifData; // Return the complete EXIF data blob
}

//std::vector<uint8_t> createExifData(ExifBlob&& exifBlob) {
//    // Calculate the total length in advance
//    size_t totalSize = sizeof(ExifHeader) + sizeof(uint16_t) + (exifBlob.tags.size() * sizeof(ExifTag)) + exifBlob.dataBlob.size();
//    exifBlob.header.length = static_cast<uint16_t>(totalSize - 2); // Subtract 2 for the marker size
//
//    // Allocate the exact amount of space in the vector
//    std::vector<uint8_t> exifData;
//    exifData.reserve(totalSize);
//
//    // Ensure all fields of the header are correctly initialized
//    memset(&exifBlob.header, 0, sizeof(exifBlob.header));
//    exifBlob.header.marker = 0xE1FF; // APP1 marker
//    exifBlob.header.length = static_cast<uint16_t>(totalSize - 2);
//    std::memcpy(exifBlob.header.exifString, "Exif\0\0", 6);
//    exifBlob.header.byteOrder = 0x4D4D; // Big-endian
//    exifBlob.header.version = 0x002A;   // TIFF version
//    exifBlob.header.zeroPad = 0x0000;   // Zero padding
//    exifBlob.header.ifdOffset = 0x00000008; // Offset to the first IFD
//
//    // Add the EXIF header
//    exifData.insert(exifData.end(),
//        reinterpret_cast<uint8_t*>(&exifBlob.header),
//        reinterpret_cast<uint8_t*>(&exifBlob.header) + sizeof(ExifHeader));
//
//    // Insert the number of tags
//    uint16_t numTags = static_cast<uint16_t>(exifBlob.tags.size());
//    exifData.insert(exifData.end(),
//        reinterpret_cast<uint8_t*>(&numTags),
//        reinterpret_cast<uint8_t*>(&numTags) + sizeof(numTags));
//
//    // Insert all tags (IFD entries)
//    exifData.insert(exifData.end(),
//        reinterpret_cast<uint8_t*>(exifBlob.tags.data()),
//        reinterpret_cast<uint8_t*>(exifBlob.tags.data()) + (exifBlob.tags.size() * sizeof(ExifTag)));
//
//    // Move the tag data from exifBlob into exifData (this avoids copying)
//    exifData.insert(exifData.end(),
//        std::make_move_iterator(exifBlob.dataBlob.begin()),
//        std::make_move_iterator(exifBlob.dataBlob.end()));
//
//    return exifData; // Return the complete EXIF data blob
//}

std::vector<uint8_t> createExifBlob() {
    ExifBlob exifBlob;

    // Tag data
    const char* make = "EVT";
    const char* model = "HB-25000-SB-C";
    uint32_t xResolution[2] = { 300, 1 };
    uint32_t yResolution[2] = { 300, 1 };
    uint16_t resolutionUnit = 2;  // Inch
    uint16_t yCbCrPositioning = 1; // Centered

    // Create IFD entries using createIFDEntry
    exifBlob.tags.push_back(createIFDEntry(0x010F, 2, strlen(make) + 1, (void*)make, exifBlob.dataBlob)); // Make
    exifBlob.tags.push_back(createIFDEntry(0x0110, 2, strlen(model) + 1, (void*)model, exifBlob.dataBlob)); // Model
    exifBlob.tags.push_back(createIFDEntry(0x011A, 5, 1, xResolution, exifBlob.dataBlob)); // XResolution
    exifBlob.tags.push_back(createIFDEntry(0x011B, 5, 1, yResolution, exifBlob.dataBlob)); // YResolution
    exifBlob.tags.push_back(createIFDEntry(0x0128, 3, 1, &resolutionUnit, exifBlob.dataBlob)); // ResolutionUnit
    exifBlob.tags.push_back(createIFDEntry(0x0213, 3, 1, &yCbCrPositioning, exifBlob.dataBlob)); // YCbCrPositioning

    // Calculate the total size of the EXIF data
    size_t totalSize = sizeof(uint16_t) * 2 + 6 + sizeof(uint16_t) * 3 + 4 + sizeof(uint16_t) // Header and IFD structure sizes
        + (exifBlob.tags.size() * sizeof(ExifTag)) + exifBlob.dataBlob.size();
    exifBlob.header.length = static_cast<uint16_t>(totalSize - 2); // Subtract 2 for the marker size

    // Allocate the exact amount of space in the vector
    std::vector<uint8_t> exifData;
    exifData.reserve(totalSize);

    // Insert the EXIF header directly
    uint16_t marker = 0xE1FF; // APP1 marker
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&marker), reinterpret_cast<uint8_t*>(&marker) + sizeof(marker));

    uint16_t length = static_cast<uint16_t>(totalSize - 2); // Subtract 2 for the marker size
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&length), reinterpret_cast<uint8_t*>(&length) + sizeof(length));

    const char exifString[6] = { 'E', 'x', 'i', 'f', '\0', '\0' };
    exifData.insert(exifData.end(), exifString, exifString + 6);

    uint16_t byteOrder = 0x4D4D; // Big-endian
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&byteOrder), reinterpret_cast<uint8_t*>(&byteOrder) + sizeof(byteOrder));

    uint16_t version = 0x002A;   // TIFF version
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&version), reinterpret_cast<uint8_t*>(&version) + sizeof(version));

    uint16_t zeroPad = 0x0000;   // Zero padding
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&zeroPad), reinterpret_cast<uint8_t*>(&zeroPad) + sizeof(zeroPad));

    uint32_t ifdOffset = 0x00000008; // Offset to the first IFD
    exifData.insert(exifData.end(), reinterpret_cast<uint8_t*>(&ifdOffset), reinterpret_cast<uint8_t*>(&ifdOffset) + sizeof(ifdOffset));

    // Insert the number of tags
    uint16_t numTags = static_cast<uint16_t>(exifBlob.tags.size());
    exifData.insert(exifData.end(),
        reinterpret_cast<uint8_t*>(&numTags),
        reinterpret_cast<uint8_t*>(&numTags) + sizeof(numTags));

    // Insert all tags (IFD entries)
    exifData.insert(exifData.end(),
        reinterpret_cast<uint8_t*>(exifBlob.tags.data()),
        reinterpret_cast<uint8_t*>(exifBlob.tags.data()) + (exifBlob.tags.size() * sizeof(ExifTag)));

    // Move the tag data from exifBlob into exifData (this avoids copying)
    exifData.insert(exifData.end(),
        std::make_move_iterator(exifBlob.dataBlob.begin()),
        std::make_move_iterator(exifBlob.dataBlob.end()));

    return exifData; // Return the complete EXIF data blob
}


//std::vector<uint8_t> createExifBlob() {
//    // Initialize the ExifBlob structure
//    ExifBlob exifBlob;
//
//    // Tag data
//    const char* make = "EVT\0";
//    const char* model = "HB-25000-SB-C\0";
//    uint32_t xResolution[2] = { 300, 1 };
//    uint32_t yResolution[2] = { 300, 1 };
//    uint16_t resolutionUnit = 2;  // Inch
//    uint16_t orientation = 3;     // 180-degree rotation
//    uint16_t yCbCrPositioning = 1; // Centered
//    uint16_t iso = 200;
//    uint32_t exposureTime[2] = { 1, 100 };
//    uint32_t fnumber[2] = { 5, 1 };
//    uint32_t focalLength[2] = { 90, 1 };
//
//    // Create IFD entries using createIFDEntry
//    exifBlob.tags.push_back(createIFDEntry(0x010F, 2, strlen(make) + 1, (void*)make, exifBlob.dataBlob)); // Make
//    exifBlob.tags.push_back(createIFDEntry(0x0110, 2, strlen(model) + 1, (void*)model, exifBlob.dataBlob)); // Model
//    //exifBlob.tags.push_back(createIFDEntry(0x011A, 5, 1, xResolution, exifBlob.dataBlob)); // XResolution
//    //exifBlob.tags.push_back(createIFDEntry(0x011B, 5, 1, yResolution, exifBlob.dataBlob)); // YResolution
//    //exifBlob.tags.push_back(createIFDEntry(0x0128, 3, 1, &resolutionUnit, exifBlob.dataBlob)); // ResolutionUnit
//    //exifBlob.tags.push_back(createIFDEntry(0x0112, 3, 1, &orientation, exifBlob.dataBlob)); // Orientation
//    //exifBlob.tags.push_back(createIFDEntry(0x0213, 3, 1, &yCbCrPositioning, exifBlob.dataBlob)); // YCbCrPositioning
//    //exifBlob.tags.push_back(createIFDEntry(0x8827, 3, 1, &iso, exifBlob.dataBlob)); // ISO Speed Ratings
//    //exifBlob.tags.push_back(createIFDEntry(0x829A, 5, 1, exposureTime, exifBlob.dataBlob)); // ExposureTime
//    //exifBlob.tags.push_back(createIFDEntry(0x829D, 5, 1, fnumber, exifBlob.dataBlob)); // FNumber
//    //exifBlob.tags.push_back(createIFDEntry(0x920A, 5, 1, focalLength, exifBlob.dataBlob)); // FocalLength
//
//    // Create and return the complete EXIF data blob
//    return createExifData(std::move(exifBlob));
//}

int main() {
    auto exifBlob = createExifBlob();
    size_t exifSize = exifBlob.size();

    // Here you can write the exifBlob to a JPEG file as an APP1 segment
    // Example output
    std::cout << "EXIF blob size: " << exifBlob.size() << " bytes" << std::endl;

    // Output the EXIF blob as hex values
    for (size_t i = 0; i < exifBlob.size(); ++i) {
        if (i % 16 == 0 && i != 0) {
            std::cout << std::endl;
        }
        else if (i % 8 == 0 && i % 16 != 0) {
            std::cout << " "; // Add extra spacing every 8 bytes
        }
        std::cout << std::hex << std::uppercase << (exifBlob[i] < 0x10 ? "0" : "") << static_cast<int>(exifBlob[i]) << " ";
    }
    std::cout << std::endl;

    // Example: Write the EXIF blob to a file
    //std::ofstream file("output.bin", std::ios::binary);
    //file.write(reinterpret_cast<const char*>(exifBlob.data()), exifBlob.size());
    //file.close();

    try {
        // Inject EXIF data into a new JPEG file
        std::string originalFile = "x:/4DTEMP/24.08.22_13.45.15_10bit/0DC_14_2005513/000001.jpg";
        std::string newFile = "x:/4DTEMP/24.08.22_13.45.15_10bit/0DC_14_2005513/000001_exif.jpg";
        writeNewJpegWithExif(originalFile, newFile, exifBlob.data(), exifSize);

        std::cout << "EXIF data injected and new file created: " << newFile << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
