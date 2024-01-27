#include "bmp_writer.h"
#include <fstream>

void writeBMP(const std::string& filename, const std::vector<std::vector<bool>>& pixels) {
    std::ofstream file(filename, std::ios::binary);

    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;

    infoHeader.width = pixels[0].size();
    infoHeader.height = pixels.size();

    // Calculate the size of the pixel data
    int rowSize = ((infoHeader.width + 31) / 32) * 4; // Each row is aligned to 4 bytes
    int pixelDataSize = rowSize * abs(infoHeader.height); // Use absolute value for biHeight

    // Set the remaining header fields
    fileHeader.file_size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2 + pixelDataSize;
    infoHeader.size_image = pixelDataSize;
    fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2;

    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    // Write the color table for 1-bit image
    unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
    file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));

    // Write the pixel data
    for (int y = infoHeader.height - 1; y >= 0; --y) {
        unsigned char byte = 0;
        int bits = 0;

        for (int x = 0; x < infoHeader.width; ++x) {
            byte <<= 1;
            byte |= pixels[y][x] ? 1 : 0;
            ++bits;

            if (bits == 8) {
                file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
                byte = 0;
                bits = 0;
            }
        }

        // Write the remaining bits (if any)
        if (bits > 0) {
            byte <<= 8 - bits;
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
        }

        // Add padding to align to 4 bytes
        while ((file.tellp() % 4) != 0) {
            byte = 0;
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
        }
    }

    file.close();
}
