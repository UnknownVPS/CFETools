#include "bmp_writer_noenc.h"
#include "../../logger/logger.h"
#include "bmp_writer.h"
#include <fstream>
#include <cmath>
#include <vector>
#include <filesystem>

void writeBMPNoEncrypt(const std::string& filename, const std::string& inputFilename) {
    Logger::Log(LOG_DEBUG, "Initializing image writer (no encryption)..");

    std::ifstream inputFile(inputFilename, std::ios::binary | std::ios::ate);
    if (!inputFile) {
        Logger::Log(LOG_ERROR, "Failed to open input file.");
        return;
    }
    std::streamsize size = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    int_fast32_t width = std::ceil(std::sqrt(size * 8));
    int_fast32_t height = width;

    // Write BMP headers
    std::ofstream file(filename, std::ios::binary);
    BMPFileHeader fileHeader{};
    BMPInfoHeader infoHeader{};
    fileHeader.file_type = 0x4D42;
    fileHeader.reserved1 = 0;
    fileHeader.reserved2 = 0;
    fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2;
    infoHeader.size = sizeof(BMPInfoHeader);
    infoHeader.width = width;
    infoHeader.height = height;
    infoHeader.planes = 1;
    infoHeader.bit_count = 1;
    infoHeader.compression = 0;
    infoHeader.x_pixels_per_meter = 0;
    infoHeader.y_pixels_per_meter = 0;
    infoHeader.colors_used = 2;
    infoHeader.colors_important = 2;
    int rowSize = ((width + 31) / 32) * 4;
    int pixelDataSize = rowSize * abs(height);
    fileHeader.file_size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2 + pixelDataSize;
    infoHeader.size_image = pixelDataSize;
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
    file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));

    // Write raw data as pixels (no encryption)
    std::vector<char> buffer(1024 * 1024);
    std::streamsize totalWritten = 0;
    while (!inputFile.eof() && totalWritten < size) {
        inputFile.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = inputFile.gcount();
        if (bytesRead > 0) {
            file.write(buffer.data(), bytesRead);
            totalWritten += bytesRead;
        }
    }
    // Padding
    int currentSize = static_cast<int>(file.tellp()) - fileHeader.offset_data;
    if (currentSize < pixelDataSize) {
        std::vector<char> padding(pixelDataSize - currentSize, 0);
        file.write(padding.data(), padding.size());
    }
    file.close();
    Logger::Log(LOG_DEBUG, "File processing completed (no encryption).");
}