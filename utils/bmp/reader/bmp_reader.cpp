#include "bmp_reader.h"
#include <random>
class PixelReader {
public:
    PixelReader(const std::string& filename, const std::string& key) 
        : file(filename, std::ios::binary), key(key), keyIndex(0) {
        readHeaders();
        std::seed_seq seed(key.begin(), key.end());
        rng.seed(seed);
    }

    unsigned char getNextByte() {
        unsigned char byte;
        if (!file.read(reinterpret_cast<char*>(&byte), sizeof(byte))) {
            return 0; // End of file reached
        }
        return decryptByte(byte);
    }

    int_fast32_t getWidth() const {
        return infoHeader.width;
    }

    int_fast32_t getHeight() const {
        return infoHeader.height;
    }

    bool isEndOfFile() const {
        return file.eof();
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    std::string key;
    size_t keyIndex;
    std::mt19937 rng;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        // Skip the color table for 1-bit image
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    }

    unsigned char decryptByte(unsigned char byte) {
        char keyByte = key[keyIndex];
        keyIndex = (keyIndex + 1) % key.length();
        return byte ^ keyByte ^ static_cast<char>(rng() & 0xFF);
    }
};

void readBMP(const std::string& filename, const std::string& outputFilename, uint_fast64_t binaryLength, const std::string& encryptionKey) {  
    // Create PixelReader object with encryption key
    PixelReader reader(filename, encryptionKey);
    std::cout << "Output file: " << outputFilename << ", Binary length: " << binaryLength << std::endl;

    // Open the output file in binary mode
    std::ofstream outputFile(outputFilename, std::ios::binary);

    // Read and decrypt the pixel data
    uint_fast64_t total_bits_processed = 0;
    for (int_fast32_t y = reader.getHeight() - 1; y >= 0; --y) {
        for (int_fast32_t x = 0; x < reader.getWidth(); ++x) {
            unsigned char byte = reader.getNextByte();
            if (reader.isEndOfFile() || total_bits_processed >= binaryLength) {
                break;
            }
            // Write the decrypted byte to the output file
            outputFile.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
            total_bits_processed += 8;
        }
        if (total_bits_processed >= binaryLength) {
            break;
        }
    }

    outputFile.close();
    std::cout << "Decryption complete. Output written to: " << outputFilename << std::endl;
}
