#include "bmp_reader.h"
#include <fstream>

std::vector<std::vector<bool>> readBMP(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);

    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;

    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

    int width = infoHeader.width;
    int height = abs(infoHeader.height);  // Use absolute value for biHeight

    // Skip the color table
    file.seekg(sizeof(unsigned int) * 2, std::ios::cur);

    // Read the pixel data
    std::vector<std::vector<bool>> pixels(height, std::vector<bool>(width));
    for (int y = height - 1; y >= 0; --y) {
        unsigned char byte = 0;
        int bits = 0;

        for (int x = 0; x < width; ++x) {
            if (bits == 0) {
                file.read(reinterpret_cast<char*>(&byte), sizeof(byte));
                bits = 8;
            }

            pixels[y][x] = (byte & 0x80) != 0;
            byte <<= 1;
            --bits;
        }

        // Skip padding
        while ((file.tellg() % 4) != 0) {
            file.read(reinterpret_cast<char*>(&byte), sizeof(byte));
        }
    }

    file.close();

    return pixels;
}
