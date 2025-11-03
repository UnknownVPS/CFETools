#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "compress.h"

// Conditional includes based on available libraries
#ifdef USE_ZSTD
#include <zstd.h>
#endif
#ifdef USE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#include <lz4frame.h>
#endif
#ifdef USE_LIBLZMA
#include <lzma.h>
#endif

/**
 * Memory-efficient streaming file compression function
 * Handles files of any size by processing in chunks
 * 
 * @param input_file Path to input file to compress
 * @param output_file Path for compressed output file
 * @param compression_lvl Compression level (1-21)
 * @return bool true if compression successful, false otherwise
 */
bool compressFile(const std::string& input_file, const std::string& output_file, int compression_lvl) {
    if (compression_lvl < 1 || compression_lvl > 21) {
        Logger::Log(LOG_ERROR, "Compression level must be between 1 and 21");
        return false;
    }

    std::ifstream infile(input_file, std::ios::binary);
    if (!infile.is_open()) {
        Logger::Log(LOG_ERROR, "Cannot open input file: " + input_file);
        return false;
    }

    infile.seekg(0, std::ios::end);
    size_t file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);
    if (file_size == 0) {
        Logger::Log(LOG_ERROR, "Input file is empty");
        return false;
    }

    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) {
        Logger::Log(LOG_ERROR, "Cannot create output file: " + output_file);
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    std::string algorithm_used;
    size_t total_compressed = 0;

    const size_t CHUNK_SIZE = 4 * 1024 * 1024;

    try {
        if (compression_lvl <= 3) {
            // LZ4 Fast streaming compression
#ifdef USE_LZ4
            Logger::Log(LOG_DEBUG, "Initializing LZ4 Fast compression context");
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID = LZ4F_max4MB;
            prefs.compressionLevel = (compression_lvl == 1) ? 1 : (compression_lvl == 2) ? 3 : 6;

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION))) {
                Logger::Log(LOG_ERROR, "LZ4F context creation failed");
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(LZ4F_compressBound(CHUNK_SIZE, &prefs));

            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.data(), outBuf.size(), &prefs);
            if (LZ4F_isError(headerSize)) {
                LZ4F_freeCompressionContext(cctx);
                Logger::Log(LOG_ERROR, "LZ4 header creation failed");
                return false;
            }
            outfile.write(outBuf.data(), headerSize);
            total_compressed += headerSize;

            while (infile) {
                infile.read(inBuf.data(), CHUNK_SIZE);
                std::streamsize bytesRead = infile.gcount();
                if (bytesRead <= 0) break;

                size_t compressedSize = LZ4F_compressUpdate(
                    cctx, outBuf.data(), outBuf.size(), 
                    inBuf.data(), bytesRead, nullptr
                );
                if (LZ4F_isError(compressedSize)) {
                    LZ4F_freeCompressionContext(cctx);
                    Logger::Log(LOG_ERROR, "LZ4 compression error during update");
                    return false;
                }
                outfile.write(outBuf.data(), compressedSize);
                total_compressed += compressedSize;
            }

            size_t finalSize = LZ4F_compressEnd(cctx, outBuf.data(), outBuf.size(), nullptr);
            if (!LZ4F_isError(finalSize)) {
                outfile.write(outBuf.data(), finalSize);
                total_compressed += finalSize;
                success = true;
                algorithm_used = "LZ4 Fast";
            }
            LZ4F_freeCompressionContext(cctx);
#else
            Logger::Log(LOG_ERROR, "LZ4 not available");
#endif
        }
        else if (compression_lvl <= 6) {
            // LZ4-HC streaming compression
#ifdef USE_LZ4
            Logger::Log(LOG_DEBUG, "Initializing LZ4-HC compression context");
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID = LZ4F_max4MB;
            prefs.compressionLevel = (compression_lvl == 4) ? 4 : (compression_lvl == 5) ? 7 : 9;

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION))) {
                Logger::Log(LOG_ERROR, "LZ4-HC context creation failed");
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(LZ4F_compressBound(CHUNK_SIZE, &prefs));

            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.data(), outBuf.size(), &prefs);
            if (LZ4F_isError(headerSize)) {
                LZ4F_freeCompressionContext(cctx);
                Logger::Log(LOG_ERROR, "LZ4-HC header creation failed");
                return false;
            }
            outfile.write(outBuf.data(), headerSize);
            total_compressed += headerSize;

            while (infile) {
                infile.read(inBuf.data(), CHUNK_SIZE);
                std::streamsize bytesRead = infile.gcount();
                if (bytesRead <= 0) break;

                size_t compressedSize = LZ4F_compressUpdate(
                    cctx, outBuf.data(), outBuf.size(), 
                    inBuf.data(), bytesRead, nullptr
                );
                if (LZ4F_isError(compressedSize)) {
                    LZ4F_freeCompressionContext(cctx);
                    Logger::Log(LOG_ERROR, "LZ4-HC compression error during update");
                    return false;
                }
                outfile.write(outBuf.data(), compressedSize);
                total_compressed += compressedSize;
            }

            size_t finalSize = LZ4F_compressEnd(cctx, outBuf.data(), outBuf.size(), nullptr);
            if (!LZ4F_isError(finalSize)) {
                outfile.write(outBuf.data(), finalSize);
                total_compressed += finalSize;
                success = true;
                algorithm_used = "LZ4-HC";
            }
            LZ4F_freeCompressionContext(cctx);
#else
            Logger::Log(LOG_ERROR, "LZ4 not available");
#endif
        }
        else if (compression_lvl <= 15) {
            // ZSTD streaming compression
#ifdef USE_ZSTD
            Logger::Log(LOG_DEBUG, "Initializing ZSTD compression context");
            ZSTD_CCtx* cctx = ZSTD_createCCtx();
            if (!cctx) {
                Logger::Log(LOG_ERROR, "ZSTD context creation failed");
                return false;
            }

            int zstd_level = std::min(19, compression_lvl + 5);
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, zstd_level);

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(ZSTD_compressBound(CHUNK_SIZE));

            while (infile) {
                infile.read(inBuf.data(), CHUNK_SIZE);
                std::streamsize bytesRead = infile.gcount();
                if (bytesRead <= 0) break;

                ZSTD_EndDirective mode = infile.eof() ? ZSTD_e_end : ZSTD_e_continue;
                ZSTD_inBuffer input = { inBuf.data(), static_cast<size_t>(bytesRead), 0 };

                bool finished = false;
                while (!finished) {
                    ZSTD_outBuffer output = { outBuf.data(), outBuf.size(), 0 };
                    size_t remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
                    if (ZSTD_isError(remaining)) {
                        ZSTD_freeCCtx(cctx);
                        Logger::Log(LOG_ERROR, "ZSTD compression error during update");
                        return false;
                    }
                    outfile.write(static_cast<char*>(output.dst), output.pos);
                    total_compressed += output.pos;
                    finished = (mode == ZSTD_e_end) ? (remaining == 0) : (input.pos == input.size);
                }
            }

            ZSTD_freeCCtx(cctx);
            success = true;
            algorithm_used = "ZSTD";
#else
            Logger::Log(LOG_ERROR, "ZSTD not available");
#endif
        }
        else {
            // LZMA2 streaming compression
#ifdef USE_LIBLZMA
            Logger::Log(LOG_DEBUG, "Initializing LZMA2 compression context");
            lzma_stream strm = LZMA_STREAM_INIT;
            lzma_options_lzma opt_lzma;
            lzma_lzma_preset(&opt_lzma, 9 | LZMA_PRESET_EXTREME);

            if (compression_lvl <= 17) {
                opt_lzma.dict_size = (compression_lvl == 16) ? (64U << 20) : (128U << 20);
                opt_lzma.depth = 512;
                algorithm_used = "LZMA2 Ultra";
            } else if (compression_lvl <= 19) {
                opt_lzma.dict_size = (compression_lvl == 18) ? (128U << 20) : (256U << 20);
                opt_lzma.depth = 1000;
                algorithm_used = "LZMA2 Maximum";
            } else {
                opt_lzma.dict_size = (compression_lvl == 20) ? (128U << 20) : (256U << 20);
                opt_lzma.depth = 2000;
                algorithm_used = "LZMA2 Extreme";
            }

            opt_lzma.nice_len = 273;
            opt_lzma.mf = LZMA_MF_BT4;
            opt_lzma.lc = 4;
            opt_lzma.lp = 0;
            opt_lzma.pb = 2;

            lzma_filter filters[3];
            filters[1].id = LZMA_FILTER_LZMA2;
            filters[1].options = &opt_lzma;
            filters[2].id = LZMA_VLI_UNKNOWN;
            filters[2].options = nullptr;

            if (compression_lvl >= 20) {
                filters[0].id = LZMA_FILTER_X86;
                filters[0].options = nullptr;
            } else {
                filters[0] = filters[1];
                filters[1] = filters[2];
            }

            if (lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC64) != LZMA_OK) {
                Logger::Log(LOG_ERROR, "LZMA encoder initialization failed");
                return false;
            }

            std::vector<uint8_t> inBuf(CHUNK_SIZE);
            std::vector<uint8_t> outBuf(CHUNK_SIZE);
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
                lzma_ret ret = lzma_code(&strm, action);

                size_t write_size = outBuf.size() - strm.avail_out;
                if (write_size > 0) {
                    outfile.write(reinterpret_cast<char*>(outBuf.data()), write_size);
                    total_compressed += write_size;
                }

                if (ret == LZMA_STREAM_END) {
                    success = true;
                    break;
                } else if (ret != LZMA_OK) {
                    std::ostringstream oss;
                    oss << "LZMA compression error: code " << ret;
                    Logger::Log(LOG_ERROR, oss.str());
                    break;
                }
            }
            lzma_end(&strm);
#else
            Logger::Log(LOG_ERROR, "liblzma not available");
#endif
        }
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, std::string("Exception: ") + e.what());
        success = false;
    }

    infile.close();
    outfile.close();

    auto end = std::chrono::high_resolution_clock::now();
    double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (success) {
        double ratio = static_cast<double>(file_size) / total_compressed;
        double speed_mbps = (file_size / (1024.0 * 1024.0)) / (time_ms / 1000.0);

        std::ostringstream oss;
        oss << "Compression successful!\n"
            << "  Algorithm: " << algorithm_used << " (level " << compression_lvl << ")\n"
            << "  Size: " << file_size << " -> " << total_compressed << " bytes\n"
            << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n"
            << "  Reduction: " << std::fixed << std::setprecision(1)
            << (100.0 * (1.0 - static_cast<double>(total_compressed) / file_size)) << "%\n"
            << "  Speed: " << std::fixed << std::setprecision(1) << speed_mbps << " MB/s\n"
            << "  Time: " << std::fixed << std::setprecision(2) << time_ms << " ms";
        Logger::Log(LOG_INFO, oss.str());
        return true;
    } else {
        Logger::Log(LOG_ERROR, "Compression failed");
        return false;
    }
}

// Helper function to get algorithm name for a given level
std::string getAlgorithmName(int compression_lvl) {
    if (compression_lvl <= 3) return "LZ4_Fast";
    else if (compression_lvl <= 6) return "LZ4-HC";
    else if (compression_lvl <= 15) return "ZSTD";
    else if (compression_lvl <= 17) return "LZMA2_Ultra";
    else if (compression_lvl <= 19) return "LZMA2_Maximum";
    else return "LZMA2_Extreme";
}