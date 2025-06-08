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

// ChaCha20 stream cipher constants (no MAC)
constexpr size_t CHACHA20_NONCE_SIZE = crypto_stream_chacha20_ietf_NONCEBYTES;
constexpr size_t CHACHA20_KEY_SIZE = crypto_stream_chacha20_ietf_KEYBYTES;

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
    void push(std::vector<uint8_t>&& item) {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(std::move(item));
        lock.unlock();
        cond.notify_one();
    }

    bool pop(std::vector<uint8_t>& item) {
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

private:
    std::queue<std::vector<uint8_t>> queue;
    std::mutex mutex;
    std::condition_variable cond;
    bool done = false;
};

class PixelReader {
public:
    PixelReader(const std::string& filename, const std::string& keyStr)
        : file(filename, std::ios::binary), currentBatchPos(0), totalBytesOutput(0) {
        
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
        
        // Initialize key same way as writer
        if (keyStr.size() != CHACHA20_KEY_SIZE) {
            crypto_generichash(key, CHACHA20_KEY_SIZE, (const unsigned char*)keyStr.data(), keyStr.size(), nullptr, 0);
        } else {
            memcpy(key, keyStr.data(), CHACHA20_KEY_SIZE);
        }
        
        readHeaders();
        
        // Calculate actual data size (excluding BMP padding)
        int rowSize = ((infoHeader.width + 31) / 32) * 4;
        pixelDataSize = rowSize * abs(infoHeader.height);
        
        // Position file at start of pixel data
        file.seekg(fileHeader.offset_data);
        
        bufferSize = std::min(static_cast<size_t>(1024 * 1024), static_cast<size_t>(pixelDataSize));
        buffer.resize(bufferSize);
        
        // Start decryption thread
        decryptorDone = false;
        decryptorThread = std::thread(&PixelReader::decryptorWorker, this);
    }
    
    ~PixelReader() {
        if (decryptorThread.joinable()) {
            decryptorThread.join();
        }
    }

    bool getDecryptedBatch(std::vector<uint8_t>& batch, uint64_t expectedLength) {
        if (!decryptedQueue.pop(batch)) {
            return false;
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
        return decryptorDone.load() && totalBytesOutput >= expectedFileSize;
    }
    
    void setExpectedFileSize(uint64_t size) {
        expectedFileSize = size;
    }

private:
    std::ifstream file;
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    uint8_t key[CHACHA20_KEY_SIZE];
    std::vector<uint8_t> buffer;
    size_t bufferSize;
    size_t currentBatchPos;
    uint64_t totalBytesOutput;
    uint64_t expectedFileSize = UINT64_MAX;
    size_t pixelDataSize;
    
    ThreadSafeQueue decryptedQueue;
    std::thread decryptorThread;
    std::atomic<bool> decryptorDone;

    void readHeaders() {
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        
        if (fileHeader.file_type != 0x4D42) {
            throw std::runtime_error("Invalid BMP file format");
        }
        
        // Skip the color table for 1-bit image
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    }
    
    void decryptorWorker() {
        uint64_t nonceCounter = 0;
        const size_t batchSize = 1024 * 1024; // Match writer's batch size
        
        while (true) {
            // Read encrypted batch from file (no MAC, so exact batch size)
            file.read(reinterpret_cast<char*>(buffer.data()), batchSize);
            size_t bytesRead = file.gcount();
            
            if (bytesRead == 0) {
                break; // End of file
            }
            
            // Decrypt the batch
            std::vector<uint8_t> decryptedBatch(bytesRead);
            
            decrypt_chunk_chacha20(buffer.data(), bytesRead, 
                                 decryptedBatch.data(), key, nonceCounter++);
            
            decryptedQueue.push(std::move(decryptedBatch));
        }
        
        decryptedQueue.setDone();
        decryptorDone = true;
    }
};

void readBMP(const std::string& filename, const std::string& outputFilename, 
             uint_fast64_t binaryLength, const std::string& encryptionKey) {
    
    Logger::Log(LOG_DEBUG, "Initializing BMP reader with ChaCha20 decryption...");
    
    try {
        PixelReader reader(filename, encryptionKey);
        reader.setExpectedFileSize(binaryLength);
        
        std::ofstream outputFile(outputFilename, std::ios::binary);
        if (!outputFile) {
            Logger::Log(LOG_ERROR, "Failed to create output file: " + outputFilename);
            return;
        }
        
        Logger::Log(LOG_DEBUG, "Decrypting and writing file...");
        
        std::vector<uint8_t> decryptedBatch;
        uint64_t totalBytesWritten = 0;
        
        // Process decrypted batches
        while (reader.getDecryptedBatch(decryptedBatch, binaryLength)) {
            // Write only the bytes we need (up to binaryLength total)
            size_t bytesToWrite = std::min(static_cast<size_t>(decryptedBatch.size()),
                                         static_cast<size_t>(binaryLength - totalBytesWritten));
            
            if (bytesToWrite > 0) {
                outputFile.write(reinterpret_cast<const char*>(decryptedBatch.data()), bytesToWrite);
                totalBytesWritten += bytesToWrite;
            }
            
            if (totalBytesWritten >= binaryLength) {
                break;
            }
        }
        
        outputFile.close();
        
        if (totalBytesWritten == binaryLength) {
            Logger::Log(LOG_INFO, "Decryption complete. Output written to: " + outputFilename);
            Logger::Log(LOG_DEBUG, "Total bytes decrypted: " + std::to_string(totalBytesWritten));
        } else {
            Logger::Log(LOG_WARNING, "Expected " + std::to_string(binaryLength) + 
                       " bytes but decrypted " + std::to_string(totalBytesWritten) + " bytes");
        }
        
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Decryption failed: " + std::string(e.what()));
    }
}