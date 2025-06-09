#include "bmp_reader_noenc.h"
#include "../../logger/logger.h"
#include "../writer/bmp_writer.h"
#include <fstream>
#include <vector>
#include <cstdint>

void readBMPNoEncrypt(const std::string& filename, const std::string& outputFilename, uint_fast64_t binaryLength) {
    Logger::Log(LOG_DEBUG, "Initializing BMP reader (no encryption)..");

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        Logger::Log(LOG_ERROR, "Failed to open BMP file.");
        return;
    }

    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
    file.seekg(sizeof(unsigned int) * 2, std::ios::cur); // skip color table

    file.seekg(fileHeader.offset_data);

    std::ofstream outputFile(outputFilename, std::ios::binary);
    if (!outputFile) {
        Logger::Log(LOG_ERROR, "Failed to create output file: " + outputFilename);
        return;
    }

    std::vector<char> buffer(1024 * 1024);
    uint_fast64_t totalWritten = 0;
    while (totalWritten < binaryLength) {
        uint_fast64_t toRead = std::min<uint_fast64_t>(buffer.size(), binaryLength - totalWritten);
        file.read(buffer.data(), toRead);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            outputFile.write(buffer.data(), bytesRead);
            totalWritten += bytesRead;
        } else {
            break;
        }
    }
    outputFile.close();
    Logger::Log(LOG_INFO, "Decryption complete (no encryption). Output written to: " + outputFilename);
}