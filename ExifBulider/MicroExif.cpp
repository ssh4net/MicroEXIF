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

#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "MicroExif.h"

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
int main(int argc, char* argv[]) {

	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <JPEG file>" << std::endl;
		return 1;
	}


	ExifBuilder builder;

	// Add Manufacturer tag
	builder.addTag(ExifTag(0x010F, 0x0002, "Ximea"));
	// Add Model tag
	builder.addTag(ExifTag(0x0110, 0x0002, "MX245CG-SY-X4G3-FF"));
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
	builder.addTag(ExifTag(0x0131, 0x0002, "V Capture"));

	// Add Orientation tag (1 - top-left)
	// 1 = Horizontal (normal), 3 = Rotate 180, 6 = Rotate 90 CW, 8 = Rotate 270 CW
	builder.addTag(ExifTag(0x0112, 0x0003, 1, uint16_t(8)));

	// Add Copyright tag
	builder.addTag(ExifTag(0x8298, 0x0002, "2024 Vlad Erium, Japan"));

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
		std::string originalFile = argv[1];
		std::filesystem::path path = originalFile;
		if (!std::filesystem::exists(path)) {
			throw std::runtime_error("File not found.");
		}
		
		std::string newFile = path.parent_path().string() + "/" + path.stem().string() + "_exif.jpg";

		writeNewJpegWithExif(originalFile, newFile, exifBlob.data(), exifBlob.size());

		std::cout << "EXIF data injected and new file created: " << newFile << std::endl;

	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}

	return 0;
}