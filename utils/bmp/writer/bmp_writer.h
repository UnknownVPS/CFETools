#ifndef BMP_WRITER_H
#define BMP_WRITER_H

#include <string>
#include <iostream>
#include <fstream>
#include <cmath>
#include <bitset>
#include <filesystem>

#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t file_type{0x4D42}; // File type always BM which is 0x4D42
    uint32_t file_size{0}; // Size of the file (in bytes)
    uint16_t reserved1{0}; // Reserved, always 0
    uint16_t reserved2{0}; // Reserved, always 0
    uint32_t offset_data{0}; // Start position of pixel data (bytes from the beginning of the file)
};

struct BMPInfoHeader {
    uint32_t size{40};
    int32_t width{0};
    int32_t height{0};
    uint16_t planes{1};
    uint16_t bit_count{1};
    uint32_t compression{0};
    uint32_t size_image{0};
    int32_t x_pixels_per_meter{0};
    int32_t y_pixels_per_meter{0};
    uint32_t colors_used{0};
    uint32_t colors_important{0};
};
#pragma pack(pop)

void writeBMP(const std::string& filename, const std::string& inputFilename);

#endif  // BMP_WRITER_H
