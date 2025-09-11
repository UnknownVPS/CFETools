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

// Option 1: Fixed ChaCha20-Poly1305 with proper nonce handling
constexpr size_t NONCE_SIZE = crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
constexpr size_t KEY_SIZE = crypto_aead_chacha20poly1305_ietf_KEYBYTES;
constexpr size_t MAC_SIZE = crypto_aead_chacha20poly1305_ietf_ABYTES;

void encrypt_chunk_aead_fixed(const uint8_t* input, size_t len, uint8_t* output,
                              const uint8_t* key, uint64_t nonce_counter) {
    uint8_t nonce[NONCE_SIZE] = {0};
    // Properly construct nonce - use counter in little endian format
    for (int i = 0; i < 8; i++) {
        nonce[i] = (nonce_counter >> (i * 8)) & 0xFF;
    }
    // Last 4 bytes remain zero (or could be a fixed value)

    unsigned long long out_len = 0;
    crypto_aead_chacha20poly1305_ietf_encrypt(output, &out_len,
                                              input, len,
                                              nullptr, 0,
                                              nullptr, nonce, key);
}

// Option 2: Simple XSalsa20 stream cipher (simpler, no MAC)
constexpr size_t XSALSA20_NONCE_SIZE = crypto_stream_xsalsa20_NONCEBYTES;
constexpr size_t XSALSA20_KEY_SIZE = crypto_stream_xsalsa20_KEYBYTES;

void encrypt_chunk_xsalsa20(const uint8_t* input, size_t len, uint8_t* output,
                            const uint8_t* key, uint64_t nonce_counter) {
    uint8_t nonce[XSALSA20_NONCE_SIZE] = {0};
    memcpy(nonce, &nonce_counter, sizeof(nonce_counter));

    crypto_stream_xsalsa20_xor(output, input, len, nonce, key);
}

// Option 3: Simple ChaCha20 stream cipher (no authentication)
constexpr size_t CHACHA20_NONCE_SIZE = crypto_stream_chacha20_ietf_NONCEBYTES;
constexpr size_t CHACHA20_KEY_SIZE = crypto_stream_chacha20_ietf_KEYBYTES;

void encrypt_chunk_chacha20(const uint8_t* input, size_t len, uint8_t* output,
                            const uint8_t* key, uint64_t nonce_counter) {
    uint8_t nonce[CHACHA20_NONCE_SIZE] = {0};
    // ChaCha20 uses 12-byte nonce
    for (int i = 0; i < 8; i++) {
        nonce[i] = (nonce_counter >> (i * 8)) & 0xFF;
    }

    crypto_stream_chacha20_ietf_xor(output, input, len, nonce, key);
}


class PixelGenerator {
public:
    PixelGenerator(const std::string& inputFilename, const std::string& keyStr, bool noEncryption = false) 
        : inputFile(inputFilename, std::ios::binary), noEncryption(noEncryption) {
        if (!noEncryption) {
            if (sodium_init() < 0) {
                throw std::runtime_error("libsodium initialization failed");
            }

            // Derive a fixed-size key
            if (keyStr.size() != KEY_SIZE) {
                crypto_generichash(key, KEY_SIZE, (const unsigned char*)keyStr.data(), keyStr.size(), nullptr, 0);
            } else {
                memcpy(key, keyStr.data(), KEY_SIZE);
            }

            encrypted.resize(maxBatchSize + MAC_SIZE);  // Add MAC_SIZE for AEAD if needed
        }

        // Allocate reusable buffer once (max possible batch size, you can set this externally if needed)
        buffer.resize(maxBatchSize);
    }

    // Reuse vector and resize only the *used portion* for output
    std::vector<char> getEncryptedBatch(size_t batchSize, uint64_t nonceCounter) {
        inputFile.read(reinterpret_cast<char*>(buffer.data()), batchSize);
        size_t bytesRead = inputFile.gcount();
        if (bytesRead == 0) return {};

        if (noEncryption) {
            // No encryption - just return the raw data
            return std::vector<char>(reinterpret_cast<char*>(buffer.data()), 
                                   reinterpret_cast<char*>(buffer.data()) + bytesRead);
        }

        // Option 2: ChaCha20 stream cipher (no MAC)
        encrypt_chunk_chacha20(buffer.data(), bytesRead, encrypted.data(), key, nonceCounter);
        return std::vector<char>(encrypted.begin(), encrypted.begin() + bytesRead);
    }

    bool isEOF() const {
        return inputFile.eof();
    }

private:
    std::ifstream inputFile;
    uint8_t key[KEY_SIZE];
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> encrypted;
    const size_t maxBatchSize = 1024 * 1024; // Default to 1 MB
    bool noEncryption;
};

class PixelWriter {
public:
    PixelWriter(const std::string& filename, const std::string& inputFilename, int_fast32_t width, int_fast32_t height, 
                bool grayscaleMode = false, bool aioMode = false, bool noEncryption = false)
        : file(filename, std::ios::binary), width(width), height(height), grayscaleMode(grayscaleMode), 
          aioMode(aioMode), inputFilename(inputFilename), noEncryption(noEncryption) {
        writeHeaders();
    }

    void writePixelsBatch(const std::vector<char>& encryptedBytes) {
        file.write(encryptedBytes.data(), encryptedBytes.size());
    }

    void finish() {
        int rowSize;
        if (grayscaleMode) {
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
    bool grayscaleMode;
    bool aioMode;
    std::string inputFilename;
    bool noEncryption;

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

        if (grayscaleMode) {
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

        // Calculate AIO header size if enabled
        size_t aioHeaderSize = 0;
        std::string fname;
        uint16_t fnameLen = 0;
        if (aioMode) {
            fname = std::filesystem::path(inputFilename).filename().string();
            fnameLen = fname.size();
            aioHeaderSize = sizeof(uint64_t) + sizeof(uint16_t) + fnameLen + sizeof(uint8_t);
        }

        fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + colorTableSize + aioHeaderSize;
        int pixelDataSize = rowSize * abs(height);
        fileHeader.file_size = fileHeader.offset_data + pixelDataSize;
        infoHeader.size_image = pixelDataSize;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        if (grayscaleMode) {
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

        // Write AIO header if enabled
        if (aioMode) {
            // Get file size to calculate binary length
            std::ifstream testFile(inputFilename, std::ios::binary | std::ios::ate);
            uint64_t binLen = 0;
            if (testFile) {
                binLen = static_cast<uint64_t>(testFile.tellg()) * 8; // Convert bytes to bits
                testFile.close();
            }

            file.write(reinterpret_cast<const char*>(&binLen), sizeof(binLen));
            file.write(reinterpret_cast<const char*>(&fnameLen), sizeof(fnameLen));
            file.write(fname.data(), fnameLen);

            // Write encrypted boolean (1 = encrypted, 0 = not encrypted)
            uint8_t encryptedFlag = noEncryption ? 0 : 1;
            file.write(reinterpret_cast<const char*>(&encryptedFlag), sizeof(encryptedFlag));
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

void writeBMP(const std::string& filename, const std::string& inputFilename, const std::string& encryptionKey, 
              bool noEncryption, bool grayscaleMode, bool aioMode) {
    Logger::Log(LOG_DEBUG, "Initializing image writer..");

    std::ifstream inputFile(inputFilename, std::ios::binary | std::ios::ate);
    if (!inputFile) {
        Logger::Log(LOG_ERROR, "Failed to open input file.");
        return;
    }
    std::streamsize size = inputFile.tellg();
    inputFile.close();

    int_fast32_t width, height;
    if (grayscaleMode) {
        // For 8-bit grayscale, each byte is one pixel
        width = std::ceil(std::sqrt(size));
        height = width;
    } else {
        // For 1-bit monochrome, each bit is one pixel (8 pixels per byte)
        width = std::ceil(std::sqrt(size * 8));
        height = width;
    }

    Logger::Log(LOG_DEBUG, "Encoding and writing file...");

    PixelGenerator generator(inputFilename, encryptionKey, noEncryption);
    PixelWriter writer(filename, inputFilename, width, height, grayscaleMode, aioMode, noEncryption);

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
        auto encryptedBatch = generator.getEncryptedBatch(batchSize, noEncryption ? 0 : nonceCounter++);
        if (!encryptedBatch.empty()) {
            pixelQueue.push(std::move(encryptedBatch));
        }
    }
    pixelQueue.setDone();
    writerThread.join();

    Logger::Log(LOG_DEBUG, "File processing completed.");
}