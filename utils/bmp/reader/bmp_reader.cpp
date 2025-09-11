#include "bmp_reader.h"
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

// Use SAME constants as writer for consistency
constexpr size_t CHACHA20_NONCE_SIZE = crypto_stream_chacha20_ietf_NONCEBYTES;
constexpr size_t KEY_SIZE = crypto_aead_chacha20poly1305_ietf_KEYBYTES; // MATCH WRITER
constexpr size_t MAC_SIZE = crypto_aead_chacha20poly1305_ietf_ABYTES;

void decrypt_chunk_chacha20(const uint8_t* input, size_t len, uint8_t* output,
                            const uint8_t* key, uint64_t nonce_counter) {
    uint8_t nonce[CHACHA20_NONCE_SIZE] = {0};
    // ChaCha20 uses 12-byte nonce - properly construct from counter
    for (int i = 0; i < 8; i++) {
        nonce[i] = (nonce_counter >> (i * 8)) & 0xFF;
    }

    // Decrypt by XORing with the same keystream used for encryption
    crypto_stream_chacha20_ietf_xor(output, input, len, nonce, key);
}

class ThreadSafeQueue {
public:
    void push(std::vector<char>&& item) {  // MATCH WRITER: use char not uint8_t
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(std::move(item));
        lock.unlock();
        cond.notify_one();
    }

    bool pop(std::vector<char>& item) {   // MATCH WRITER: use char not uint8_t
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
    std::queue<std::vector<char>> queue;  // MATCH WRITER: use char not uint8_t
    mutable std::mutex mutex;
    std::condition_variable cond;
    bool done = false;
};

class PixelReader {
public:
    PixelReader(const std::string& filename, const std::string& keyStr, bool noEncryption = false)
        : file(filename, std::ios::binary), noEncryption(noEncryption), 
          currentBatchPos(0), totalBytesOutput(0) {

        // Check if file opened successfully
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open BMP file: " + filename);
        }

        if (!noEncryption) {
            if (sodium_init() < 0) {
                throw std::runtime_error("libsodium initialization failed");
            }

            // MATCH WRITER: Use same key derivation logic
            if (keyStr.size() != KEY_SIZE) {
                crypto_generichash(key, KEY_SIZE, (const unsigned char*)keyStr.data(), keyStr.size(), nullptr, 0);
            } else {
                memcpy(key, keyStr.data(), KEY_SIZE);
            }
        }

        readHeaders();

        // Position file at start of pixel data using offset_data (SINGLE SOURCE OF TRUTH)
        file.seekg(fileHeader.offset_data);

        bufferSize = 1024 * 1024; // 1MB buffer - MATCH WRITER
        buffer.resize(bufferSize);

        // Separate buffer for worker thread to avoid race conditions
        if (!noEncryption) {
            workerBuffer.resize(bufferSize);
            decryptorDone = false;
            decryptorThread = std::thread(&PixelReader::decryptorWorker, this);
        }
    }

    ~PixelReader() {
        if (decryptorThread.joinable()) {
            decryptorThread.join();
        }
    }

    bool getDecryptedBatch(std::vector<char>& batch, uint64_t expectedLength) {  // MATCH WRITER: char not uint8_t
        if (noEncryption) {
            // Direct read for no encryption
            file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
            size_t bytesRead = file.gcount();
            if (bytesRead == 0) return false;

            batch.assign(reinterpret_cast<char*>(buffer.data()), 
                        reinterpret_cast<char*>(buffer.data()) + bytesRead);
        } else {
            // Use queue for encrypted data
            if (!decryptedQueue.pop(batch)) {
                return false;
            }
        }

        // Trim batch to expected length if this is the final batch
        size_t remainingBytes = expectedLength - totalBytesOutput;
        if (batch.size() > remainingBytes) {
            batch.resize(remainingBytes);
        }

        totalBytesOutput += batch.size();
        return true;
    }

    int_fast32_t getWidth() const {
        return infoHeader.width;
    }

    int_fast32_t getHeight() const {
        return infoHeader.height;
    }

    bool isDecryptionComplete() const {
        if (noEncryption) {
            return totalBytesOutput >= expectedFileSize;
        } else {
            return decryptorDone.load() && totalBytesOutput >= expectedFileSize;
        }
    }

    void setExpectedFileSize(uint64_t size) {
        expectedFileSize = size;
    }

    bool isGrayscaleMode() const {
        return grayscaleMode;
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    uint8_t key[KEY_SIZE];  // MATCH WRITER: use KEY_SIZE
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> workerBuffer; // Separate buffer for worker thread
    size_t bufferSize;
    bool noEncryption;           
    size_t currentBatchPos;
    uint64_t totalBytesOutput;
    uint64_t expectedFileSize = UINT64_MAX;
    bool grayscaleMode = false;

    ThreadSafeQueue decryptedQueue;
    std::thread decryptorThread;
    std::atomic<bool> decryptorDone;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        if (file.gcount() != sizeof(fileHeader)) {
            throw std::runtime_error("Failed to read BMP file header - file may be corrupted");
        }

        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        if (file.gcount() != sizeof(infoHeader)) {
            throw std::runtime_error("Failed to read BMP info header - file may be corrupted");
        }

        if (fileHeader.file_type != 0x4D42) {
            throw std::runtime_error("Invalid BMP file format - not a valid BMP file");
        }

        // Auto-detect grayscale mode
        grayscaleMode = (infoHeader.bit_count == 8);

        // DO NOT manually skip color table here - offset_data handles everything
        // This was causing double positioning and stream alignment issues
    }

    void decryptorWorker() {
        uint64_t nonceCounter = 0;
        const size_t batchSize = 1024 * 1024; // Match writer's batch size

        while (true) {
            // Use separate worker buffer to avoid race conditions
            file.read(reinterpret_cast<char*>(workerBuffer.data()), batchSize);
            size_t bytesRead = file.gcount();

            if (bytesRead == 0) {
                break; // End of file
            }

            // Decrypt the batch
            std::vector<char> decryptedBatch(bytesRead);  // MATCH WRITER: use char
            decrypt_chunk_chacha20(workerBuffer.data(), bytesRead, 
                                 reinterpret_cast<uint8_t*>(decryptedBatch.data()), key, nonceCounter++);

            decryptedQueue.push(std::move(decryptedBatch));
        }

        decryptedQueue.setDone();
        decryptorDone = true;
    }
};

void readBMP(const std::string& filename, const std::string& outputFilename, 
             uint_fast64_t binaryLength, const std::string& encryptionKey) {

    Logger::Log(LOG_DEBUG, "Initializing BMP reader...");

    try {
        bool noEncryption = encryptionKey.empty();
        PixelReader reader(filename, encryptionKey, noEncryption);
        reader.setExpectedFileSize(binaryLength);

        if (reader.isGrayscaleMode()) {
            Logger::Log(LOG_DEBUG, "Detected 8-bit grayscale BMP");
        } else {
            Logger::Log(LOG_DEBUG, "Detected 1-bit monochrome BMP");
        }

        std::ofstream outputFile(outputFilename, std::ios::binary);
        if (!outputFile) {
            Logger::Log(LOG_ERROR, "Failed to create output file: " + outputFilename);
            return;
        }

        if (noEncryption) {
            Logger::Log(LOG_DEBUG, "Reading without decryption...");
        } else {
            Logger::Log(LOG_DEBUG, "Decrypting and writing file...");
        }

        std::vector<char> decryptedBatch;  // MATCH WRITER: use char
        uint64_t totalBytesWritten = 0;

        // Process batches
        while (reader.getDecryptedBatch(decryptedBatch, binaryLength)) {
            // Write only the bytes we need (up to binaryLength total)
            size_t bytesToWrite = std::min(static_cast<size_t>(decryptedBatch.size()),
                                         static_cast<size_t>(binaryLength - totalBytesWritten));

            if (bytesToWrite > 0) {
                outputFile.write(decryptedBatch.data(), bytesToWrite);
                totalBytesWritten += bytesToWrite;
            }

            if (totalBytesWritten >= binaryLength) {
                break;
            }
        }

        outputFile.close();

        if (totalBytesWritten == binaryLength) {
            Logger::Log(LOG_INFO, "Extraction complete. Output written to: " + outputFilename);
            Logger::Log(LOG_DEBUG, "Total bytes extracted: " + std::to_string(totalBytesWritten));
        } else {
            Logger::Log(LOG_WARNING, "Expected " + std::to_string(binaryLength) + 
                       " bytes but extracted " + std::to_string(totalBytesWritten) + " bytes");
        }

    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "BMP reading failed: " + std::string(e.what()));
    }
}

