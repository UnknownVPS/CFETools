
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

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
 *   1-3: LZ4 Fast (>500 MB/s, ~2.1:1 ratio) - Speed priority
 *   4-6: LZ4-HC (41 MB/s, ~2.7:1 ratio) - Balanced
 *   7-15: ZSTD (50-515 MB/s, 2.9-4.5:1 ratio) - Best general purpose
 *   16-17: LZMA2 Ultra (3-1 MB/s, 4.8-5.2:1) - Ultra compression
 *   18-19: LZMA2 Maximum (1-0.5 MB/s, 5.2-5.8:1) - Maximum settings
 *   20-21: LZMA2 Extreme (0.3-0.1 MB/s, 5.8-6.2:1) - Extreme compression
 * @return bool true if compression successful, false otherwise
 */
bool compressFile(const std::string& input_file, const std::string& output_file, int compression_lvl) {
    if (compression_lvl < 1 || compression_lvl > 21) {
        std::cerr << "Error: Compression level must be between 1 and 21" << std::endl;
        return false;
    }

    std::ifstream infile(input_file, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_file << std::endl;
        return false;
    }

    // Get file size
    infile.seekg(0, std::ios::end);
    size_t file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    if (file_size == 0) {
        std::cerr << "Error: Input file is empty" << std::endl;
        return false;
    }

    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Cannot create output file: " << output_file << std::endl;
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    std::string algorithm_used;
    size_t total_compressed = 0;

    // Chunk size for streaming (4MB - good balance of memory vs performance)
    const size_t CHUNK_SIZE = 4 * 1024 * 1024;

    try {
        if (compression_lvl <= 3) {
            // LZ4 Fast streaming compression
#ifdef USE_LZ4
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID = LZ4F_max4MB;
            prefs.compressionLevel = (compression_lvl == 1) ? 1 : (compression_lvl == 2) ? 3 : 6;

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION))) {
                std::cerr << "LZ4F context creation failed" << std::endl;
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(LZ4F_compressBound(CHUNK_SIZE, &prefs));

            // Write header
            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.data(), outBuf.size(), &prefs);
            if (LZ4F_isError(headerSize)) {
                LZ4F_freeCompressionContext(cctx);
                return false;
            }
            outfile.write(outBuf.data(), headerSize);
            total_compressed += headerSize;

            // Process file in chunks
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
                    return false;
                }

                outfile.write(outBuf.data(), compressedSize);
                total_compressed += compressedSize;
            }

            // Finalize
            size_t finalSize = LZ4F_compressEnd(cctx, outBuf.data(), outBuf.size(), nullptr);
            if (!LZ4F_isError(finalSize)) {
                outfile.write(outBuf.data(), finalSize);
                total_compressed += finalSize;
                success = true;
                algorithm_used = "LZ4 Fast (Streaming)";
            }

            LZ4F_freeCompressionContext(cctx);
#else
            std::cerr << "Error: LZ4 not available" << std::endl;
#endif
        }
        else if (compression_lvl <= 6) {
            // LZ4-HC streaming compression
#ifdef USE_LZ4
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID = LZ4F_max4MB;
            prefs.compressionLevel = (compression_lvl == 4) ? 4 : (compression_lvl == 5) ? 7 : 9;

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION))) {
                return false;
            }

            std::vector<char> inBuf(CHUNK_SIZE);
            std::vector<char> outBuf(LZ4F_compressBound(CHUNK_SIZE, &prefs));

            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.data(), outBuf.size(), &prefs);
            if (LZ4F_isError(headerSize)) {
                LZ4F_freeCompressionContext(cctx);
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
                algorithm_used = "LZ4-HC (Streaming)";
            }

            LZ4F_freeCompressionContext(cctx);
#else
            std::cerr << "Error: LZ4 not available" << std::endl;
#endif
        }
        else if (compression_lvl <= 15) {
            // ZSTD streaming compression
#ifdef USE_ZSTD
            ZSTD_CCtx* cctx = ZSTD_createCCtx();
            if (!cctx) {
                std::cerr << "ZSTD context creation failed" << std::endl;
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
                        return false;
                    }

                    outfile.write(static_cast<char*>(output.dst), output.pos);
                    total_compressed += output.pos;

                    finished = (mode == ZSTD_e_end) ? (remaining == 0) : (input.pos == input.size);
                }
            }

            ZSTD_freeCCtx(cctx);
            success = true;
            algorithm_used = "ZSTD (Streaming)";
#else
            std::cerr << "Error: ZSTD not available" << std::endl;
#endif
        }
        else {
            // LZMA2 streaming compression for levels 16-21
#ifdef USE_LIBLZMA
            lzma_stream strm = LZMA_STREAM_INIT;
            lzma_options_lzma opt_lzma;
            lzma_lzma_preset(&opt_lzma, 9 | LZMA_PRESET_EXTREME);

            // Adjust settings based on level but keep memory usage reasonable
            if (compression_lvl <= 17) {
                opt_lzma.dict_size = (compression_lvl == 16) ? (64U << 20) : (128U << 20);
                opt_lzma.depth = 512;
                algorithm_used = "LZMA2 Ultra (Streaming)";
            } else if (compression_lvl <= 19) {
                opt_lzma.dict_size = (compression_lvl == 18) ? (128U << 20) : (256U << 20);
                opt_lzma.depth = 1000;
                algorithm_used = "LZMA2 Maximum (Streaming)";
            } else {
                opt_lzma.dict_size = (compression_lvl == 20) ? (128U << 20) : (256U << 20);
                opt_lzma.depth = 2000;
                algorithm_used = "LZMA2 Extreme (Streaming)";
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

            // Add BCJ filter for levels 20-21
            if (compression_lvl >= 20) {
                filters[0].id = LZMA_FILTER_X86;
                filters[0].options = nullptr;
            } else {
                filters[0] = filters[1];
                filters[1] = filters[2];
            }

            if (lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC64) != LZMA_OK) {
                std::cerr << "LZMA encoder initialization failed" << std::endl;
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
                    std::cerr << "LZMA compression error: " << ret << std::endl;
                    break;
                }
            }

            lzma_end(&strm);
#else
            std::cerr << "Error: liblzma not available" << std::endl;
#endif
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        success = false;
    }

    infile.close();
    outfile.close();

    auto end = std::chrono::high_resolution_clock::now();
    double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (success) {
        double ratio = static_cast<double>(file_size) / total_compressed;
        double speed_mbps = (file_size / (1024.0 * 1024.0)) / (time_ms / 1000.0);

        std::cout << "Compression successful!" << std::endl;
        std::cout << "  Algorithm: " << algorithm_used << " (level " << compression_lvl << ")" << std::endl;
        std::cout << "  Size: " << file_size << " : " << total_compressed << " bytes" << std::endl;
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1" << std::endl;
        std::cout << "  Reduction: " << std::fixed << std::setprecision(1) 
                  << (100.0 * (1.0 - static_cast<double>(total_compressed) / file_size)) << "%" << std::endl;
        std::cout << "  Speed: " << std::fixed << std::setprecision(1) << speed_mbps << " MB/s" << std::endl;
        std::cout << "  Time: " << std::fixed << std::setprecision(2) << time_ms << " ms" << std::endl;

        return true;
    } else {
        std::cerr << "Error: Compression failed" << std::endl;
        return false;
    }
}

// Helper function to get algorithm name for a given level
std::string getAlgorithmName(int compression_lvl) {
    if (compression_lvl <= 3) return "LZ4 Fast (Streaming)";
    else if (compression_lvl <= 6) return "LZ4-HC (Streaming)";
    else if (compression_lvl <= 15) return "ZSTD (Streaming)";
    else if (compression_lvl <= 17) return "LZMA2 Ultra (Streaming)";
    else if (compression_lvl <= 19) return "LZMA2 Maximum (Streaming)";
    else return "LZMA2 Extreme (Streaming)";
}
