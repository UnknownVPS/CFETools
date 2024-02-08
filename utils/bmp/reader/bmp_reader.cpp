#include "bmp_reader.h"

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

    long int getWidth() const {
        return infoHeader.width;
    }

    long int getHeight() const {
        return infoHeader.height;
    }

    bool isEndOfFile() const {
        return file.eof();
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        // Skip the color table for 1-bit image
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    }
};


void readBMP(const std::string& filename, const std::string& outputFilename, unsigned long long int binaryLength) {  
    // Create PixelReader object
    PixelReader reader(filename);

    // Open the output file in binary mode
    std::ofstream outputFile(outputFilename, std::ios::binary);

    // Read the pixel data
    unsigned long long int total_bits_processed = 0;
    for (long int y = reader.getHeight() - 1; y >= 0; --y) {
        for (long int x = 0; x < reader.getWidth(); ++x) {
            unsigned char byte = reader.getNextByte();
            if (reader.isEndOfFile() || total_bits_processed >= binaryLength) {
                break;
            }
            // Write the byte directly to the output file
            outputFile.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
            total_bits_processed += 8;
        }
        if (total_bits_processed >= binaryLength) {
            break;
        }
    }

    outputFile.close();
}
