
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>

// Conditional includes based on available libraries
#ifdef USE_ZSTD
#include <zstd.h>
#endif

#ifdef USE_LZ4
#include <lz4frame.h>
#endif

#ifdef USE_LIBLZMA
#include <lzma.h>
#endif

enum class CompressionFormat {
    UNKNOWN,
    LZ4F,
    ZSTD,
    LZMA2_XZ
};

/**
 * Detect compression format from file header magic bytes
 */
CompressionFormat detectCompressionFormat(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return CompressionFormat::UNKNOWN;
    }

    // Read first 8 bytes to check magic numbers
    uint8_t header[8] = {0};
    file.read(reinterpret_cast<char*>(header), 8);
    file.close();

    // LZ4F magic: 0x184D2204 (little endian)
    if (header[0] == 0x04 && header[1] == 0x22 && header[2] == 0x4D && header[3] == 0x18) {
        return CompressionFormat::LZ4F;
    }

    // ZSTD magic: 0x28B52FFD (little endian) - note: this is the correct order
    if (header[0] == 0x28 && header[1] == 0xB5 && header[2] == 0x2F && header[3] == 0xFD) {
        return CompressionFormat::ZSTD;
    }

    // XZ magic: 0xFD 0x37 0x7A 0x58 0x5A 0x00
    if (header[0] == 0xFD && header[1] == 0x37 && header[2] == 0x7A && 
        header[3] == 0x58 && header[4] == 0x5A && header[5] == 0x00) {
        return CompressionFormat::LZMA2_XZ;
    }

    return CompressionFormat::UNKNOWN;
}

/**
 * Get format name as string
 */
std::string getFormatName(CompressionFormat format) {
    switch (format) {
        case CompressionFormat::LZ4F: return "LZ4 Frame";
        case CompressionFormat::ZSTD: return "ZSTD";
        case CompressionFormat::LZMA2_XZ: return "LZMA2/XZ";
        default: return "Unknown";
    }
}

/**
 * Memory-efficient streaming file decompression function with auto-detection
 * Handles files of any size by processing in chunks
 * 
 * @param input_file Path to compressed input file
 * @param output_file Path for decompressed output file
 * @return bool true if decompression successful, false otherwise
 */
bool decompressFile(const std::string& input_file, const std::string& output_file) {
    // Auto-detect compression format
    CompressionFormat format = detectCompressionFormat(input_file);
    if (format == CompressionFormat::UNKNOWN) {
        std::cerr << "Error: Unknown or unsupported compression format" << std::endl;
        return false;
    }

    std::cout << "Detected format: " << getFormatName(format) << std::endl;

    std::ifstream infile(input_file, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_file << std::endl;
        return false;
    }

    // Get compressed file size
    infile.seekg(0, std::ios::end);
    size_t compressed_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Cannot create output file: " << output_file << std::endl;
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    size_t total_decompressed = 0;

    // Chunk size for streaming
    const size_t CHUNK_SIZE = 4 * 1024 * 1024;

    try {
        if (format == CompressionFormat::LZ4F) {
            // LZ4F decompression
#ifdef USE_LZ4
            LZ4F_dctx* dctx = nullptr;
            LZ4F_errorCode_t result = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
            if (LZ4F_isError(result)) {
                std::cerr << "LZ4F context creation failed: " << LZ4F_getErrorName(result) << std::endl;
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(CHUNK_SIZE * 4); // Larger output buffer for decompression

            while (infile && !infile.eof()) {
                infile.read(inBuf.data(), CHUNK_SIZE);
                std::streamsize bytesRead = infile.gcount();
                if (bytesRead <= 0) break;

                const char* srcPtr = inBuf.data();
                size_t srcSize = bytesRead;

                while (srcSize > 0) {
                    char* dstPtr = outBuf.data();
                    size_t dstSize = outBuf.size();
                    size_t srcSizeOrig = srcSize;

                    size_t result = LZ4F_decompress(dctx, dstPtr, &dstSize, srcPtr, &srcSize, nullptr);
                    if (LZ4F_isError(result)) {
                        std::cerr << "LZ4F decompression error: " << LZ4F_getErrorName(result) << std::endl;
                        LZ4F_freeDecompressionContext(dctx);
                        return false;
                    }

                    if (dstSize > 0) {
                        outfile.write(dstPtr, dstSize);
                        total_decompressed += dstSize;
                    }

                    srcPtr += (srcSizeOrig - srcSize);
                }
            }

            LZ4F_freeDecompressionContext(dctx);
            success = true;
#else
            std::cerr << "Error: LZ4 not available for decompression" << std::endl;
#endif
        }
        else if (format == CompressionFormat::ZSTD) {
            // ZSTD decompression
#ifdef USE_ZSTD
            ZSTD_DCtx* dctx = ZSTD_createDCtx();
            if (!dctx) {
                std::cerr << "ZSTD decompression context creation failed" << std::endl;
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(CHUNK_SIZE * 4);

            while (infile && !infile.eof()) {
                infile.read(inBuf.data(), CHUNK_SIZE);
                std::streamsize bytesRead = infile.gcount();
                if (bytesRead <= 0) break;

                ZSTD_inBuffer input = { inBuf.data(), static_cast<size_t>(bytesRead), 0 };

                while (input.pos < input.size) {
                    ZSTD_outBuffer output = { outBuf.data(), outBuf.size(), 0 };

                    size_t result = ZSTD_decompressStream(dctx, &output, &input);
                    if (ZSTD_isError(result)) {
                        std::cerr << "ZSTD decompression error: " << ZSTD_getErrorName(result) << std::endl;
                        ZSTD_freeDCtx(dctx);
                        return false;
                    }

                    if (output.pos > 0) {
                        outfile.write(static_cast<char*>(output.dst), output.pos);
                        total_decompressed += output.pos;
                    }
                }
            }

            ZSTD_freeDCtx(dctx);
            success = true;
#else
            std::cerr << "Error: ZSTD not available for decompression" << std::endl;
#endif
        }
        else if (format == CompressionFormat::LZMA2_XZ) {
            // LZMA2/XZ decompression
#ifdef USE_LIBLZMA
            lzma_stream strm = LZMA_STREAM_INIT;

            // Initialize decoder for .xz format
            lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
            if (ret != LZMA_OK) {
                std::cerr << "LZMA decoder initialization failed: " << ret << std::endl;
                return false;
            }

            std::vector<uint8_t> inBuf(CHUNK_SIZE);
            std::vector<uint8_t> outBuf(CHUNK_SIZE * 4);

            lzma_action action = LZMA_RUN;

            while (true) {
                if (strm.avail_in == 0 && !infile.eof()) {
                    infile.read(reinterpret_cast<char*>(inBuf.data()), CHUNK_SIZE);
                    std::streamsize bytesRead = infile.gcount();

                    strm.next_in = inBuf.data();
                    strm.avail_in = bytesRead;

                    if (infile.eof()) {
                        action = LZMA_FINISH;
                    }
                }

                strm.next_out = outBuf.data();
                strm.avail_out = outBuf.size();

                ret = lzma_code(&strm, action);

                size_t write_size = outBuf.size() - strm.avail_out;
                if (write_size > 0) {
                    outfile.write(reinterpret_cast<char*>(outBuf.data()), write_size);
                    total_decompressed += write_size;
                }

                if (ret == LZMA_STREAM_END) {
                    success = true;
                    break;
                } else if (ret != LZMA_OK) {
                    std::cerr << "LZMA decompression error: " << ret << std::endl;
                    break;
                }
            }

            lzma_end(&strm);
#else
            std::cerr << "Error: liblzma not available for decompression" << std::endl;
#endif
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception during decompression: " << e.what() << std::endl;
        success = false;
    }

    infile.close();
    outfile.close();

    auto end = std::chrono::high_resolution_clock::now();
    double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (success) {
        double speed_mbps = (total_decompressed / (1024.0 * 1024.0)) / (time_ms / 1000.0);

        std::cout << "Decompression successful!" << std::endl;
        std::cout << "  Format: " << getFormatName(format) << std::endl;
        std::cout << "  Size: " << compressed_size << " : " << total_decompressed << " bytes" << std::endl;
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) 
                  << (static_cast<double>(total_decompressed) / compressed_size) << ":1" << std::endl;
        std::cout << "  Expansion: " << std::fixed << std::setprecision(1)
                  << (100.0 * (static_cast<double>(total_decompressed) / compressed_size - 1.0)) << "%" << std::endl;
        std::cout << "  Speed: " << std::fixed << std::setprecision(1) << speed_mbps << " MB/s" << std::endl;
        std::cout << "  Time: " << std::fixed << std::setprecision(2) << time_ms << " ms" << std::endl;

        return true;
    } else {
        std::cerr << "Error: Decompression failed" << std::endl;
        return false;
    }
}

/**
 * Check if file is compressed by attempting format detection
 */
bool isCompressedFile(const std::string& filename) {
    return detectCompressionFormat(filename) != CompressionFormat::UNKNOWN;
}

/**
 * Get compression format of a file as string
 */
std::string getFileCompressionFormat(const std::string& filename) {
    return getFormatName(detectCompressionFormat(filename));
}

