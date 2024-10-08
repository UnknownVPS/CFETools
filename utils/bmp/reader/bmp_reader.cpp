#include "bmp_reader.h"
#include "../../logger/logger.h"

class PixelReader {
public:
    PixelReader(const std::string& filename, const std::string& key) 
        : file(filename, std::ios::binary), key(key), keyIndex(0) {
        readHeaders();
        std::seed_seq seed(key.begin(), key.end());
        rng.seed(seed);
        bufferSize = std::min(static_cast<size_t>(1024 * 1024), static_cast<size_t>(infoHeader.width) * infoHeader.height);
        buffer.resize(bufferSize);
        readBuffer();
    }

    unsigned char getNextByte() {
        if (bufferPos >= bytesRead) {
            if (!readBuffer()) {
                return 0; // End of file reached
            }
        }
        return decryptByte(buffer[bufferPos++]);
    }

    int_fast32_t getWidth() const {
        return infoHeader.width;
    }

    int_fast32_t getHeight() const {
        return infoHeader.height;
    }

    bool isEndOfFile() const {
        return file.eof() && bufferPos >= bytesRead;
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    std::string key;
    size_t keyIndex;
    std::mt19937 rng;
    std::vector<unsigned char> buffer;
    size_t bufferSize;
    size_t bufferPos = 0;
    size_t bytesRead = 0;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        // Skip the color table for 1-bit image
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    }

    bool readBuffer() {
        file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
        bytesRead = file.gcount();
        bufferPos = 0;
        return bytesRead > 0;
    }

    unsigned char decryptByte(unsigned char byte) {
        char keyByte = key[keyIndex];
        keyIndex = (keyIndex + 1) % key.length();
        return byte ^ keyByte ^ static_cast<char>(rng() & 0xFF);
    }
};

void readBMP(const std::string& filename, const std::string& outputFilename, uint_fast64_t binaryLength, const std::string& encryptionKey) {  
    PixelReader reader(filename, encryptionKey);

    std::ofstream outputFile(outputFilename, std::ios::binary);
    std::vector<unsigned char> outputBuffer(std::min(static_cast<size_t>(1024 * 1024), static_cast<size_t>(binaryLength)));

    uint_fast64_t total_bits_processed = 0;
    size_t bufferPos = 0;

    for (int_fast32_t y = reader.getHeight() - 1; y >= 0 && total_bits_processed < binaryLength; --y) {
        for (int_fast32_t x = 0; x < reader.getWidth() && total_bits_processed < binaryLength; ++x) {
            unsigned char byte = reader.getNextByte();
            if (reader.isEndOfFile()) {
                break;
            }
            outputBuffer[bufferPos++] = byte;
            total_bits_processed += 8;

            if (bufferPos == outputBuffer.size()) {
                outputFile.write(reinterpret_cast<const char*>(outputBuffer.data()), bufferPos);
                bufferPos = 0;
            }
        }
    }

    if (bufferPos > 0) {
        outputFile.write(reinterpret_cast<const char*>(outputBuffer.data()), bufferPos);
    }

    outputFile.close();
    Logger::Log(LOG_INFO, "Decryption complete. Output written to: " + outputFilename);
}