#include "bmp_writer.h"
#include "../../logger/logger.h"
#include <sodium.h>
#include <thread>
#include <vector>
#include <fstream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <queue>
#include <filesystem>
#include "../../../globals.h"
#include "../../aio/aio_header.h"
#include "../../../version.h"
#include "../../hashers/fileHasher.hpp"
#include "../../../utils/hashers/encryption.hpp"

class PixelGenerator {
public:
    PixelGenerator(const std::string& inputFilename, const std::string& keyStr, bool no_encrypt = false)
        : inputFile(inputFilename, std::ios::binary), no_encrypt(no_encrypt) {
        if (!no_encrypt) {
            if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
            if (keyStr.size() != encryption::CHACHA20_KEY_SIZE) {
                crypto_generichash(key, encryption::CHACHA20_KEY_SIZE, 
                                   (const unsigned char*)keyStr.data(), keyStr.size(), nullptr, 0);
            } else {
                memcpy(key, keyStr.data(), encryption::CHACHA20_KEY_SIZE);
            }
            encrypted.resize(maxBatchSize);
        }
        buffer.resize(maxBatchSize);
    }

    std::vector<char> getEncryptedBatch(size_t batchSize, uint64_t nonceCounter) {
        inputFile.read(reinterpret_cast<char*>(buffer.data()), batchSize);
        size_t bytesRead = inputFile.gcount();
        if (bytesRead == 0) return {};

        if (no_encrypt) {
            return std::vector<char>(reinterpret_cast<char*>(buffer.data()), 
                                     reinterpret_cast<char*>(buffer.data()) + bytesRead);
        }

        encryption::chacha20_xor(buffer.data(), bytesRead, encrypted.data(), key, nonceCounter);
        return std::vector<char>(encrypted.begin(), encrypted.begin() + bytesRead);
    }

    bool isEOF() const { return inputFile.eof(); }

private:
    std::ifstream inputFile;
    uint8_t key[encryption::CHACHA20_KEY_SIZE];
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> encrypted;
    const size_t maxBatchSize = 1024 * 1024;
    bool no_encrypt;
};

class PixelWriter {
public:
    PixelWriter(const std::string& filename, const std::string& inputFilename, int_fast32_t width, int_fast32_t height, 
                bool grayscale = false, bool twofile_system = false, bool no_encrypt = false)
        : file(filename, std::ios::binary), width(width), height(height), grayscale(grayscale), 
          twofile_system(twofile_system), inputFilename(inputFilename), no_encrypt(no_encrypt) {
        writeHeaders();
    }

    void writePixelsBatch(const std::vector<char>& encryptedBytes) {
        file.write(encryptedBytes.data(), encryptedBytes.size());
    }

    void finish() {
        int rowSize;
        if (grayscale) {
            // 8-bit grayscale: each pixel is 1 byte, rows padded to 4-byte boundary
            rowSize = ((width + 3) / 4) * 4;
        } else {
            // 1-bit monochrome: 8 pixels per byte, rows padded to 4-byte boundary
            rowSize = ((width + 31) / 32) * 4;
        }

        int expectedSize = rowSize * height;
        int currentSize = file.tellp() - dataStartPos;
        if (currentSize < expectedSize) {
            std::vector<char> padding(expectedSize - currentSize, 0);
            file.write(padding.data(), padding.size());
        }
        file.close();
    }

private:
    std::ofstream file;
    int_fast32_t width;
    int_fast32_t height;
    std::streampos dataStartPos;
    bool grayscale;
    bool twofile_system;
    std::string inputFilename;
    bool no_encrypt;

    void writeHeaders() {
        BMPFileHeader fileHeader{};
        BMPInfoHeader infoHeader{};
        fileHeader.file_type = 0x4D42;
        fileHeader.reserved1 = 0;
        fileHeader.reserved2 = 0;

        infoHeader.size = sizeof(BMPInfoHeader);
        infoHeader.width = width;
        infoHeader.height = height;
        infoHeader.planes = 1;
        infoHeader.compression = 0;
        infoHeader.x_pixels_per_meter = 0;
        infoHeader.y_pixels_per_meter = 0;

        int rowSize;
        int colorTableSize;

        if (grayscale) {
            // 8-bit grayscale
            infoHeader.bit_count = 8;
            infoHeader.colors_used = 256;
            infoHeader.colors_important = 256;
            rowSize = ((width + 3) / 4) * 4;
            colorTableSize = sizeof(unsigned int) * 256;
        } else {
            // 1-bit monochrome
            infoHeader.bit_count = 1;
            infoHeader.colors_used = 2;
            infoHeader.colors_important = 2;
            rowSize = ((width + 31) / 32) * 4;
            colorTableSize = sizeof(unsigned int) * 2;
        }

        // Calculate AIO header size FIRST
        size_t aioHeaderSize = 0;
        AIOHeaderWriter aioWriter; // Declare outside if block for proper scope
        
        if (!twofile_system) {
            // Get file size for binary length calculation
            std::ifstream testFile(inputFilename, std::ios::binary | std::ios::ate);
            uint64_t binLen = 0;
            if (testFile) {
                binLen = static_cast<uint64_t>(testFile.tellg()) * 8; // Convert bytes to bits
                testFile.close();
            }
            
            // Extract filename from path
            std::string fname = std::filesystem::path(inputFilename).filename().string();
            
            // Add fields with automatic bit calculation
            aioWriter.addUInt64("binary_length", binLen);        // Auto-calculates bits needed
            aioWriter.addString("filename", fname);              // String length auto-calculated
            aioWriter.addBool("encrypted", !no_encrypt);       // Always 1 bit
            aioWriter.addString("v", VERSION);                    // Auto-calculates (probably 1 bit)
            aioWriter.addBool("compress", isCompressed);
            aioWriter.addBool("pack", isPacked);
            std::string hash;
            if (!disableHash) {
                Logger::StartTimer("SHA-256 hash calculation");
                hash = fileHasher::hashFileSHA256(inputFilename);
                Logger::EndTimer("SHA-256 hash calculation", LOG_INFO);
            }
            aioWriter.addString("hash", hash);
            // Get the total size for offset calculation
            aioHeaderSize = aioWriter.getTotalSize();
        }

        // NOW calculate the correct offset
        fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + colorTableSize + static_cast<uint32_t>(aioHeaderSize);
        int pixelDataSize = rowSize * abs(height);
        fileHeader.file_size = fileHeader.offset_data + pixelDataSize;
        infoHeader.size_image = pixelDataSize;

        // Write headers in correct order
        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        if (grayscale) {
            // Write grayscale color table (0-255)
            for (int i = 0; i < 256; i++) {
                unsigned int grayColor = (i << 16) | (i << 8) | i; // RGB all same value for gray
                file.write(reinterpret_cast<const char*>(&grayColor), sizeof(grayColor));
            }
        } else {
            // Write monochrome color table
            unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
            file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));
        }

        // Write AIO header if enabled (aioWriter is already prepared above)
        if (!twofile_system) {
            aioWriter.writeToStream(file);
        }

        dataStartPos = file.tellp();
    }
};

class ThreadSafeQueue {
public:
    void push(std::vector<char>&& item) {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(std::move(item));
        lock.unlock();
        cond.notify_one();
    }

    bool pop(std::vector<char>& item) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this] { return !queue.empty() || done; });
        if (queue.empty()) return false;
        item = std::move(queue.front());
        queue.pop();
        return true;
    }

    void setDone() {
        std::unique_lock<std::mutex> lock(mutex);
        done = true;
        lock.unlock();
        cond.notify_all();
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex);
        return queue.size();
    }

private:
    std::queue<std::vector<char>> queue;
    mutable std::mutex mutex;
    std::condition_variable cond;
    bool done = false;
};

void writeBMP(const std::string& filename, const std::string& inputFilename, const std::string& encryptionKey) {
    Logger::Log(LOG_DEBUG, "Initializing image writer..");
    std::ifstream inputFile(inputFilename, std::ios::binary | std::ios::ate);
    if (!inputFile) {
        Logger::Log(LOG_ERROR, "Failed to open input file.");
        return;
    }
    std::streamsize size = inputFile.tellg();
    inputFile.close();

    int_fast32_t width, height;
    if (grayscale) {
        // For 8-bit grayscale, each byte is one pixel
        width = std::ceil(std::sqrt(size));
        height = width;
    } else {
        // For 1-bit monochrome, each bit is one pixel (8 pixels per byte)
        width = std::ceil(std::sqrt(size * 8));
        height = width;
    }

    Logger::Log(LOG_DEBUG, "Encoding and writing file...");

    PixelGenerator generator(inputFilename, encryptionKey, no_encrypt);
    PixelWriter writer(filename, inputFilename, width, height, grayscale, twofile_system, no_encrypt);

    ThreadSafeQueue pixelQueue;
    std::atomic<bool> writerDone(false);

    // Writer thread: consumes encrypted pixel batches
    std::thread writerThread([&]() {
        std::vector<char> pixelBatch;
        while (pixelQueue.pop(pixelBatch)) {
            writer.writePixelsBatch(pixelBatch);
        }
        writer.finish();
        writerDone = true;
    });

    const size_t batchSize = 1024 * 1024; // 1 MB batch size
    uint64_t nonceCounter = 0;

    // Producer: reads input, encrypts (if enabled) and pushes batches
    while (!generator.isEOF()) {
        auto encryptedBatch = generator.getEncryptedBatch(batchSize, no_encrypt ? 0 : nonceCounter++);
        if (!encryptedBatch.empty()) {
            pixelQueue.push(std::move(encryptedBatch));
        }
    }
    pixelQueue.setDone();
    writerThread.join();

    Logger::Log(LOG_DEBUG, "File processing completed.");
}