#include "bmp_writer.h"
#include "../../logger/logger.h"

class PixelGenerator {
public:
    PixelGenerator(const std::string& inputFilename, const std::string& key)
        : inputFile(inputFilename, std::ios::binary), key(key), keyIndex(0) {
        std::seed_seq seed(key.begin(), key.end());
        rng.seed(seed);
    }

    std::vector<char> getPixelsBatch(size_t batchSize) {
        std::vector<char> pixels(batchSize);
        size_t bytesRead = inputFile.read(pixels.data(), batchSize).gcount();
        pixels.resize(bytesRead);
        
        std::transform(pixels.begin(), pixels.end(), pixels.begin(),
            [this](char byte) { return encryptByte(byte); });

        return pixels;
    }

    bool isEOF() const {
        return inputFile.eof();
    }

private:
    std::ifstream inputFile;
    std::string key;
    size_t keyIndex;
    std::mt19937 rng;

    char encryptByte(char byte) {
        char keyByte = key[keyIndex];
        keyIndex = (keyIndex + 1) % key.length();
        return byte ^ keyByte ^ static_cast<char>(rng() & 0xFF);
    }
};

class PixelWriter {
public:
    PixelWriter(const std::string& filename, int_fast32_t width, int_fast32_t height)
        : file(filename, std::ios::binary), width(width), height(height) {
        writeHeaders();
    }

    void writePixelsBatch(const std::vector<char>& pixels) {
        for (char byte : pixels) {
            for (int i = 7; i >= 0; --i) {
                bool pixel = (byte >> i) & 1;
                writePixel(pixel);
            }
        }
    }

    void finish() {
        if (bits > 0) {
            byte <<= 8 - bits;
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
        }

        // Add padding to align to 4 bytes
        unsigned char paddingByte = 0;
        while ((file.tellp() % 4) != 0) {
            file.write(reinterpret_cast<const char*>(&paddingByte), sizeof(paddingByte));
        }

        file.close();
    }

private:
    std::ofstream file;
    int_fast32_t width;
    int_fast32_t height;
    unsigned char byte = 0;
    int bits = 0;

    void writeHeaders() {
        BMPFileHeader fileHeader;
        BMPInfoHeader infoHeader;

        // Set the remaining header fields
        fileHeader.file_type = 0x4D42; // 'BM' in little-endian
        fileHeader.reserved1 = 0;
        fileHeader.reserved2 = 0;
        fileHeader.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2;

        infoHeader.size = sizeof(BMPInfoHeader);
        infoHeader.width = width;
        infoHeader.height = height;
        infoHeader.planes = 1;
        infoHeader.bit_count = 1; // 1 bit per pixel for black and white image
        infoHeader.compression = 0; // No compression
        infoHeader.x_pixels_per_meter = 0;
        infoHeader.y_pixels_per_meter = 0;
        infoHeader.colors_used = 2; // Black and white
        infoHeader.colors_important = 2; // All colors are important

        // Calculate the size of the pixel data
        int rowSize = ((infoHeader.width + 31) / 32) * 4; // Each row is aligned to 4 bytes
        int pixelDataSize = rowSize * abs(infoHeader.height); // Use absolute value for biHeight

        fileHeader.file_size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + sizeof(unsigned int) * 2 + pixelDataSize;
        infoHeader.size_image = pixelDataSize;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        // Write the color table for 1-bit image
        unsigned int colorTable[2] = { 0x00000000, 0x00FFFFFF };
        file.write(reinterpret_cast<const char*>(colorTable), sizeof(colorTable));
    }

    void writePixel(bool pixel) {
        byte = (byte << 1) | (pixel ? 1 : 0);
        if (++bits == 8) {
            file.write(reinterpret_cast<const char*>(&byte), sizeof(byte));
            byte = 0;
            bits = 0;
        }
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

private:
    std::queue<std::vector<char>> queue;
    std::mutex mutex;
    std::condition_variable cond;
    bool done = false;
};

void writeBMP(const std::string& filename, const std::string& inputFilename, const std::string& encryptionKey) {
    Logger::Log(LOG_DEBUG, "Initializing image writer..");
    std::ifstream inputFile(inputFilename, std::ios::binary);
    inputFile.seekg(0, std::ios::end);
    std::streamsize size = inputFile.tellg();
    inputFile.close();

    int_fast32_t width = std::ceil(std::sqrt(size * 8));
    int_fast32_t height = width;

    Logger::Log(LOG_DEBUG, "Encoding and writing file...");
    PixelGenerator generator(inputFilename, encryptionKey);
    PixelWriter writer(filename, width, height);

    ThreadSafeQueue pixelQueue;
    std::atomic<bool> writerDone(false);

    std::thread writerThread([&]() {
        std::vector<char> pixelBatch;
        while (pixelQueue.pop(pixelBatch)) {
            writer.writePixelsBatch(pixelBatch);
        }
        writer.finish();
        writerDone = true;
    });

    const size_t batchSize = 1024 * 1024; // 1MB batch size
    while (!generator.isEOF()) {
        pixelQueue.push(generator.getPixelsBatch(batchSize));
    }

    pixelQueue.setDone();
    writerThread.join();

    Logger::Log(LOG_DEBUG, "File processing completed.");
}