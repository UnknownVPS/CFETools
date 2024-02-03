#include "bmp_writer.h"
#include "../../progressbar/progressbar.h"
#include <fstream>
#include <vector>
#include <cmath>

class PixelGenerator {
public:
    PixelGenerator(const std::string& inputFilename) : inputFile(inputFilename) {}

    bool getNextPixel() {
        char bitChar;
        if (!(inputFile.get(bitChar))) {
            return false; // End of file reached
        }
        return bitChar == '1' ? true : false;
    }

private:
    std::ifstream inputFile;
};


class PixelWriter {
public:
    PixelWriter(const std::string& filename, int width, int height) : file(filename, std::ios::binary), width(width), height(height) {
        writeHeaders();
    }

    PixelWriter& operator<<(PixelGenerator& generator) {
        bool pixel = generator.getNextPixel();
        writePixel(pixel);
        return *this;
    }

    void finish() {
        if (bits > 0) {
            byte <<= 8 - bits;
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
        }

        // Add padding to align to 4 bytes
        unsigned char paddingByte = 0;
        while ((file.tellp() % 4) != 0) {
            file.write(reinterpret_cast<const char*>(&paddingByte), sizeof(paddingByte));
        }

        file.close();
    }

private:
    std::ofstream file;
    int width;
    int height;
    unsigned char byte = 0;
    int bits = 0;

    void writeHeaders() {
        BMPFileHeader fileHeader;
        BMPInfoHeader infoHeader;

        // Set the remaining header fields
        fileHeader.file_type = 0x4D42; // 'BM' in little-endian
        fileHeader.reserved1 = 0;
        fileHeader.reserved2 = 0;
        fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2;

        infoHeader.size = sizeof(BMPInfoHeader);
        infoHeader.width = width;
        infoHeader.height = height;
        infoHeader.planes = 1;
        infoHeader.bit_count = 1; // 1 bit per pixel for black and white image
        infoHeader.compression = 0; // No compression
        infoHeader.x_pixels_per_meter = 0;
        infoHeader.y_pixels_per_meter = 0;
        infoHeader.colors_used = 2; // Black and white
        infoHeader.colors_important = 2; // All colors are important

        // Calculate the size of the pixel data
        int rowSize = ((infoHeader.width + 31) / 32) * 4; // Each row is aligned to 4 bytes
        int pixelDataSize = rowSize * abs(infoHeader.height); // Use absolute value for biHeight

        fileHeader.file_size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2 + pixelDataSize;
        infoHeader.size_image = pixelDataSize;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        // Write the color table for 1-bit image
        unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
        file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));
    }

    void writePixel(bool pixel) {
        byte <<= 1;
        byte |= pixel ? 1 : 0;
        ++bits;

        if (bits == 8) {
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
            byte = 0;
            bits = 0;
        }
    }
};

void writeBMP(const std::string& filename, const std::string& inputFilename) {
    // Calculate the width and height of the image
    std::ifstream inputFile(inputFilename);
    inputFile.seekg(0, std::ios::end);
    std::streamsize size = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);
    long long int width = std::ceil(std::sqrt(size));
    long long int height = width;
    std::cout << width * height << std::endl;
    std::cout << width << std::endl;
    inputFile.close();

    // Create PixelGenerator and PixelWriter objects
    PixelGenerator generator(inputFilename);
    PixelWriter writer(filename, width, height);
    // Write the pixel data

    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            writer << generator;
        }
    }

    writer.finish();

}