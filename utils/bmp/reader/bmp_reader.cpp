#include "bmp_reader.h"
#include <fstream>
#include <vector>
#include <cmath>
#include </workspaces/Nutox/utils/progressbar/progressbar.h>

class PixelReader {
public:
    PixelReader(const std::string& filename) : file(filename, std::ios::binary) {
        readHeaders();
    }

    unsigned char getNextByte() {
        unsigned char byte;
        if (!file.read(reinterpret_cast<char*>(&byte), sizeof(byte))) {
            return 0; // End of file reached
        }
        return byte;
    }

    int getWidth() const {
        return infoHeader.width;
    }

    int getHeight() const {
        return infoHeader.height;
    }

    bool isEndOfFile() const {
        return file.eof();
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    unsigned char byte = 0;
    int bits = 0;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

        // Skip the color table for 1-bit image
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    }
};


void readBMP(const std::string& filename, const std::string& outputFilename) {
    // Create PixelReader object
    PixelReader reader(filename);

    // Open the output file
    std::ofstream outputFile(outputFilename);

    // Read the pixel data
    for (int y = reader.getHeight() - 1; y >= 0; --y) {
        for (int x = 0; x < reader.getWidth(); ++x) {
            unsigned char byte = reader.getNextByte();
            if (reader.isEndOfFile()) {
                break;
            }
            for (int i = 7; i >= 0; --i) {
                bool pixel = byte & (1 << i);
                outputFile << (pixel ? '1' : '0');
            }
        }
    }


    outputFile.close();
}

