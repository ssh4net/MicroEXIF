# MicroEXIF C++ Library

The MicroEXIF library is a lightweight library for generating EXIF metadata data blob. It provides an easy way to configure and create EXIF metadata.
This library is only to generate EXIF metadata and does not support parsing or editing EXIF metadata from files.

## Core Components

### ExifTag Structure

The `ExifTag` structure represents individual EXIF metadata tags. Each tag consists of:

- **tag (uint16\_t)**: The tag ID uniquely identifies the metadata type (e.g., `0x010F` for Manufacturer).
- **type (uint16\_t)**: Specifies the type of data for the tag, such as `BYTE`, `ASCII`, `SHORT`, `LONG`, `RATIONAL`, etc.
- **count (uint32\_t)**: Number of values represented by this tag.
- **value (std::vector\<uint8\_t>)**: Stores the tag value data.

The `ExifTag` structure has multiple constructors for different tag types (e.g., `BYTE`, `SHORT`, `LONG`, `RATIONAL`, `ASCII`), making it easy to create and add tags with various types of data.

### ExifBuilder Class

The `ExifBuilder` class allows you to construct a complete EXIF metadata block by combining multiple `ExifTag` instances. Its key features include:

- **Adding Tags**: You can add EXIF metadata tags to the builder using the `addTag()` method.
- **Building the EXIF Blob**: The `buildExifBlob()` method compiles all the added tags into a complete EXIF metadata blob that can be injected into a JPEG file.

The `ExifBuilder` Internally manages EXIF data alignment (big-endian/little-endian) and handles the concatenation of multiple tags into a valid EXIF structure that adheres to the TIFF/EXIF specifications.

## Usage Example

To use the MicroEXIF library, follow these general steps:

1. **Create an `ExifBuilder` instance**.
2. **Add EXIF tags** using `ExifTag` constructors.
3. **Build the EXIF blob** and inject it into a JPEG file.

```cpp
#include "MicroExif.h"

ExifBuilder builder;

// Add Manufacturer tag
builder.addTag(ExifTag(0x010F, 0x0002, "Ximea"));
// Add Model tag
builder.addTag(ExifTag(0x0110, 0x0002, "MX245CG-SY-X4G3-FF"));
// Add Exposure Time (RATIONAL)
builder.addTag(ExifTag(0x829A, 0x0005, 1, 1, 100));

// Build EXIF Blob
std::vector<uint8_t> exifBlob = builder.buildExifBlob();
```
Example code to find `FFDB` marker and inject EXIF blob after it:

```cpp
// Example code to inject the EXIF blob into a JPEG file
writeNewJpegWithExif("input.jpg", "output_exif.jpg", exifBlob.data(), exifBlob.size());
```

## Contributing
We welcome your contributions! Please feel free to submit pull requests, feature requests, or issues to help the library get better.
See contributing.md for ways to get started.
