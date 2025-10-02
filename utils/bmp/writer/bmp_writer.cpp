#include "bmp_writer.h"
#include "../../logger/logger.h"
#include <sodium.h>
#include <thread>
#include <vector>
#include <fstream>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include "../../../globals.h"
#include "../../aio/aio_header.h"
#include "../../../version.h"
#include "../../hashers/fileHasher.hpp"
#include "../../../utils/hashers/encryption.hpp"

// Lock-free ring buffer for zero-copy batching
template<size_t BufferCount>
class RingBuffer {
    struct Slot {
        std::vector<char> data;
        std::atomic<bool> ready{false};
        std::atomic<bool> consumed{true};
    };
    
    Slot slots[BufferCount];
    std::atomic<size_t> writeIdx{0};
    std::atomic<size_t> readIdx{0};
    std::atomic<bool> done{false};
    
public:
    RingBuffer() {
        // Pre-allocate all buffers to avoid runtime allocation
        for (auto& slot : slots) {
            slot.data.reserve(8 * 1024 * 1024); // 8MB per slot
        }
    }
    
    // Producer: get next available slot for writing
    std::vector<char>* getWriteSlot() {
        size_t idx = writeIdx.load(std::memory_order_relaxed);
        while (!slots[idx].consumed.load(std::memory_order_acquire)) {
            if (done.load(std::memory_order_relaxed)) return nullptr;
            std::this_thread::yield();
        }
        return &slots[idx].data;
    }
    
    // Producer: mark slot as ready
    void commitWrite(size_t size) {
        size_t idx = writeIdx.load(std::memory_order_relaxed);
        slots[idx].data.resize(size);
        slots[idx].consumed.store(false, std::memory_order_release);
        slots[idx].ready.store(true, std::memory_order_release);
        writeIdx.store((idx + 1) % BufferCount, std::memory_order_release);
    }
    
    // Consumer: get next ready slot
    std::vector<char>* getReadSlot() {
        size_t idx = readIdx.load(std::memory_order_relaxed);
        while (!slots[idx].ready.load(std::memory_order_acquire)) {
            if (done.load(std::memory_order_acquire) && 
                !slots[idx].ready.load(std::memory_order_acquire)) {
                return nullptr;
            }
            std::this_thread::yield();
        }
        return &slots[idx].data;
    }
    
    // Consumer: mark slot as consumed
    void commitRead() {
        size_t idx = readIdx.load(std::memory_order_relaxed);
        slots[idx].ready.store(false, std::memory_order_release);
        slots[idx].consumed.store(true, std::memory_order_release);
        readIdx.store((idx + 1) % BufferCount, std::memory_order_release);
    }
    
    void setDone() { done.store(true, std::memory_order_release); }
};

class FastPixelWriter {
public:
    FastPixelWriter(const std::string& filename, const std::string& inputFilename, 
                    int_fast32_t width, int_fast32_t height, bool grayscale = false, 
                    bool twofile_system = false, bool no_encrypt = false)
        : width(width), height(height), grayscale(grayscale), 
          twofile_system(twofile_system), inputFilename(inputFilename), no_encrypt(no_encrypt) {
        
        // Open with larger buffer for better OS-level performance
        file.rdbuf()->pubsetbuf(nullptr, 0); // Unbuffered for direct writes
        file.open(filename, std::ios::binary | std::ios::out);
        if (!file) throw std::runtime_error("Failed to open output file");
        
        writeHeaders();
    }

    void writePixelsBatch(const char* data, size_t size) {
        file.write(data, size);
    }

    void finish() {
        int rowSize;
        if (grayscale) {
            rowSize = ((width + 3) / 4) * 4;
        } else {
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
            infoHeader.bit_count = 8;
            infoHeader.colors_used = 256;
            infoHeader.colors_important = 256;
            rowSize = ((width + 3) / 4) * 4;
            colorTableSize = sizeof(unsigned int) * 256;
        } else {
            infoHeader.bit_count = 1;
            infoHeader.colors_used = 2;
            infoHeader.colors_important = 2;
            rowSize = ((width + 31) / 32) * 4;
            colorTableSize = sizeof(unsigned int) * 2;
        }

        size_t aioHeaderSize = 0;
        AIOHeaderWriter aioWriter;
        
        if (!twofile_system) {
            std::ifstream testFile(inputFilename, std::ios::binary | std::ios::ate);
            uint64_t binLen = 0;
            if (testFile) {
                binLen = static_cast<uint64_t>(testFile.tellg()) * 8;
                testFile.close();
            }
            
            std::string fname = std::filesystem::path(inputFilename).filename().string();
            
            aioWriter.addUInt64("binary_length", binLen);
            aioWriter.addString("filename", fname);
            aioWriter.addBool("encrypted", !no_encrypt);
            aioWriter.addString("v", VERSION);
            aioWriter.addBool("compress", isCompressed);
            aioWriter.addBool("pack", isPacked);
            
            std::string hash;
            if (!disableHash) {
                Logger::StartTimer("xxHash calculation");
                hash = fileHasher::xxhash_file(inputFilename);
                Logger::EndTimer("xxHash calculation", LOG_INFO);
            }
            aioWriter.addString("hash", hash);
            
            if (shaEnabled) {
                Logger::StartTimer("SHA Hashing");
                std::string sha = fileHasher::hashFileSHA256(inputFilename);
                Logger::Log(LOG_DEBUG, "SHA: " + sha);
                Logger::EndTimer("SHA Hashing", LOG_DEBUG);
                aioWriter.addString("SHA", sha);
            } 
            if (crcEnabled) {
                Logger::StartTimer("CRC32 Hashing");
                std::string crc = fileHasher::crc32_file(inputFilename);
                Logger::Log(LOG_DEBUG, "CRC32: " + crc);
                Logger::EndTimer("CRC32 Hashing", LOG_DEBUG);
                aioWriter.addString("CRC", crc);
            }
            aioHeaderSize = aioWriter.getTotalSize();
        }

        fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + colorTableSize + static_cast<uint32_t>(aioHeaderSize);
        int pixelDataSize = rowSize * abs(height);
        fileHeader.file_size = fileHeader.offset_data + pixelDataSize;
        infoHeader.size_image = pixelDataSize;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        if (grayscale) {
            for (int i = 0; i < 256; i++) {
                unsigned int grayColor = (i << 16) | (i << 8) | i;
                file.write(reinterpret_cast<const char*>(&grayColor), sizeof(grayColor));
            }
        } else {
            unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
            file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));
        }

        if (!twofile_system) {
            aioWriter.writeToStream(file);
        }

        dataStartPos = file.tellp();
    }
};

// Specialized versions for encrypted and unencrypted paths
class EncryptedProcessor {
    uint8_t key[encryption::CHACHA20_KEY_SIZE];
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> encrypted;
    
public:
    EncryptedProcessor(const std::string& keyStr) {
        if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
        if (keyStr.size() != encryption::CHACHA20_KEY_SIZE) {
            crypto_generichash(key, encryption::CHACHA20_KEY_SIZE, 
                               (const unsigned char*)keyStr.data(), keyStr.size(), nullptr, 0);
        } else {
            memcpy(key, keyStr.data(), encryption::CHACHA20_KEY_SIZE);
        }
        buffer.resize(8 * 1024 * 1024);
        encrypted.resize(8 * 1024 * 1024);
    }
    
    size_t process(std::ifstream& inputFile, char* output, uint64_t nonceCounter) {
        inputFile.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        size_t bytesRead = inputFile.gcount();
        if (bytesRead == 0) return 0;
        
        encryption::chacha20_xor(buffer.data(), bytesRead, encrypted.data(), key, nonceCounter);
        memcpy(output, encrypted.data(), bytesRead);
        return bytesRead;
    }
};

class UnencryptedProcessor {
public:
    size_t process(std::ifstream& inputFile, char* output, uint64_t) {
        inputFile.read(output, 8 * 1024 * 1024);
        return inputFile.gcount();
    }
};

void writeBMP(const std::string& filename, const std::string& inputFilename, const std::string& encryptionKey) {
    Logger::Log(LOG_DEBUG, "Initializing image writer..");
    std::ifstream inputFile(inputFilename, std::ios::binary | std::ios::ate);
    if (!inputFile) {
        Logger::Log(LOG_ERROR, "Failed to open input file.");
        return;
    }
    std::streamsize size = inputFile.tellg();
    inputFile.seekg(0);

    int_fast32_t width, height;
    if (grayscale) {
        width = std::ceil(std::sqrt(size));
        height = width;
    } else {
        width = std::ceil(std::sqrt(size * 8));
        height = width;
    }

    Logger::Log(LOG_DEBUG, "Encoding and writing file...");

    FastPixelWriter writer(filename, inputFilename, width, height, grayscale, twofile_system, no_encrypt);
    RingBuffer<4> ringBuffer; // 4 slots of 8MB each = 32MB total buffering

    std::atomic<bool> writerDone(false);

    // Writer thread: minimal work, just write
    std::thread writerThread([&]() {
        while (auto* slot = ringBuffer.getReadSlot()) {
            writer.writePixelsBatch(slot->data(), slot->size());
            ringBuffer.commitRead();
        }
        writer.finish();
        writerDone = true;
    });

    uint64_t nonceCounter = 0;

    // Producer: specialized path for encrypted vs unencrypted
    if (no_encrypt) {
        UnencryptedProcessor processor;
        while (!inputFile.eof()) {
            auto* slot = ringBuffer.getWriteSlot();
            if (!slot) break;
            
            size_t bytesRead = processor.process(inputFile, slot->data(), 0);
            if (bytesRead > 0) {
                ringBuffer.commitWrite(bytesRead);
            }
        }
    } else {
        EncryptedProcessor processor(encryptionKey);
        while (!inputFile.eof()) {
            auto* slot = ringBuffer.getWriteSlot();
            if (!slot) break;
            
            size_t bytesRead = processor.process(inputFile, slot->data(), nonceCounter++);
            if (bytesRead > 0) {
                ringBuffer.commitWrite(bytesRead);
            }
        }
    }
    
    ringBuffer.setDone();
    writerThread.join();

    Logger::Log(LOG_DEBUG, "File processing completed.");
}