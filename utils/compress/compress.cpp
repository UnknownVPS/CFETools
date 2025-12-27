#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <memory>
#include <thread> // For hardware_concurrency
#include <cstdio> // For FILE*, fopen, fread, fwrite

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

#include "compress.h"

bool compressFile(const std::string& input_file, const std::string& output_file, int compression_lvl) {
    if (compression_lvl < 1 || compression_lvl > 21) {
        Logger::Log(LOG_ERROR, "Compression level must be between 1 and 21");
        return false;
    }

    // Use C-style file handles for lower overhead than std::fstream
    // We do NOT use setvbuf with _IONBF here because it hurts performance on fast algorithms
    // by disabling the OS/C library read-ahead cache.
    FILE* infile = fopen(input_file.c_str(), "rb");
    if (!infile) {
        Logger::Log(LOG_ERROR, "Cannot open input file: " + input_file);
        return false;
    }

    fseek(infile, 0, SEEK_END);
    size_t file_size = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    if (file_size == 0) {
        fclose(infile);
        Logger::Log(LOG_ERROR, "Input file is empty");
        return false;
    }

    FILE* outfile = fopen(output_file.c_str(), "wb");
    if (!outfile) {
        fclose(infile);
        Logger::Log(LOG_ERROR, "Cannot create output file: " + output_file);
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    std::string algorithm_used;
    size_t total_compressed = 0;

    const size_t CHUNK_SIZE = 4 * 1024 * 1024;

    try {
        // Use unique_ptr to avoid zero-initialization overhead
        std::unique_ptr<char[]> inBuf(new char[CHUNK_SIZE]);
        std::unique_ptr<char[]> outBuf;
        size_t outBufSize = 0;

        if (compression_lvl <= 6) {
            // LZ4 / LZ4-HC
#ifdef USE_LZ4
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID = LZ4F_max4MB;
            prefs.compressionLevel = (compression_lvl <= 3) ? 
                ((compression_lvl == 1) ? 1 : (compression_lvl == 2) ? 3 : 6) :
                ((compression_lvl == 4) ? 4 : (compression_lvl == 5) ? 7 : 9);

            algorithm_used = (compression_lvl <= 3) ? "LZ4 Fast" : "LZ4-HC";

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION))) {
                throw std::runtime_error("LZ4F context creation failed");
            }

            outBufSize = LZ4F_compressBound(CHUNK_SIZE, &prefs);
            outBuf.reset(new char[outBufSize]);

            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.get(), outBufSize, &prefs);
            if (LZ4F_isError(headerSize)) throw std::runtime_error("LZ4 header creation failed");
            
            fwrite(outBuf.get(), 1, headerSize, outfile);
            total_compressed += headerSize;

            while (true) {
                size_t bytesRead = fread(inBuf.get(), 1, CHUNK_SIZE, infile);
                if (bytesRead == 0) break; // EOF or Error

                size_t compressedSize = LZ4F_compressUpdate(
                    cctx, outBuf.get(), outBufSize, 
                    inBuf.get(), bytesRead, nullptr
                );
                if (LZ4F_isError(compressedSize)) throw std::runtime_error("LZ4 compression error");

                fwrite(outBuf.get(), 1, compressedSize, outfile);
                total_compressed += compressedSize;
            }

            size_t finalSize = LZ4F_compressEnd(cctx, outBuf.get(), outBufSize, nullptr);
            if (!LZ4F_isError(finalSize)) {
                fwrite(outBuf.get(), 1, finalSize, outfile);
                total_compressed += finalSize;
                success = true;
            }
            LZ4F_freeCompressionContext(cctx);
#else
            throw std::runtime_error("LZ4 not available");
#endif
        }
        else if (compression_lvl <= 15) {
            // ZSTD streaming compression
#ifdef USE_ZSTD
            ZSTD_CCtx* cctx = ZSTD_createCCtx();
            if (!cctx) throw std::runtime_error("ZSTD context creation failed");

            int zstd_level = std::min(19, compression_lvl + 5);
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, zstd_level);
            
            // Enable Multithreading
            unsigned nbWorkers = std::thread::hardware_concurrency();
            if (nbWorkers > 1) {
                ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nbWorkers);
            }

            outBufSize = ZSTD_compressBound(CHUNK_SIZE);
            outBuf.reset(new char[outBufSize]);

            ZSTD_EndDirective mode = ZSTD_e_continue;
            size_t bytesRead = 0;
            
            do {
                bytesRead = fread(inBuf.get(), 1, CHUNK_SIZE, infile);
                // If we read less than chunk size, we might be at EOF. Check strictly.
                if (bytesRead < CHUNK_SIZE && feof(infile)) {
                    mode = ZSTD_e_end;
                } else if (bytesRead == 0 && feof(infile)) {
                    mode = ZSTD_e_end;
                }

                ZSTD_inBuffer input = { inBuf.get(), bytesRead, 0 };

                bool finished = false;
                while (!finished) {
                    ZSTD_outBuffer output = { outBuf.get(), outBufSize, 0 };
                    size_t remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
                    if (ZSTD_isError(remaining)) throw std::runtime_error("ZSTD compression error");

                    fwrite(outBuf.get(), 1, output.pos, outfile);
                    total_compressed += output.pos;

                    finished = (mode == ZSTD_e_end) ? (remaining == 0) : (input.pos == input.size);
                }
            } while (bytesRead == CHUNK_SIZE); 

            ZSTD_freeCCtx(cctx);
            success = true;
            algorithm_used = "ZSTD MT";
#else
            throw std::runtime_error("ZSTD not available");
#endif
        }
        else {
            // LZMA2 streaming compression
#ifdef USE_LIBLZMA
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
                throw std::runtime_error("LZMA encoder initialization failed");
            }

            outBufSize = CHUNK_SIZE; 
            outBuf.reset(new char[outBufSize]);

            lzma_action action = LZMA_RUN;
            size_t bytesRead = 0;

            while (true) {
                // Only read if buffer is empty and we are not at EOF
                if (strm.avail_in == 0 && !feof(infile)) {
                    bytesRead = fread(inBuf.get(), 1, CHUNK_SIZE, infile);
                    strm.next_in = reinterpret_cast<uint8_t*>(inBuf.get());
                    strm.avail_in = bytesRead;
                    
                    if (feof(infile)) {
                        action = LZMA_FINISH;
                    }
                }

                // If we have no input left and we hit EOF previously, we might just need to flush
                if (strm.avail_in == 0 && feof(infile) && action != LZMA_FINISH) {
                     action = LZMA_FINISH;
                }

                strm.next_out = reinterpret_cast<uint8_t*>(outBuf.get());
                strm.avail_out = outBufSize;
                lzma_ret ret = lzma_code(&strm, action);

                size_t write_size = outBufSize - strm.avail_out;
                if (write_size > 0) {
                    fwrite(outBuf.get(), 1, write_size, outfile);
                    total_compressed += write_size;
                }

                if (ret == LZMA_STREAM_END) {
                    success = true;
                    break;
                } else if (ret != LZMA_OK) {
                    std::ostringstream oss;
                    oss << "LZMA compression error: code " << ret;
                    throw std::runtime_error(oss.str());
                }
            }
            lzma_end(&strm);
#else
            throw std::runtime_error("liblzma not available");
#endif
        }
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, std::string("Exception: ") + e.what());
        success = false;
    }

    fclose(infile);
    fclose(outfile);

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
        std::remove(output_file.c_str());
        return false;
    }
}