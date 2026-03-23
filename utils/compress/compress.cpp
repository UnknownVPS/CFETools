#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <memory>
#include <thread>
#include <cstdio>
#include <cstring>

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

// ─────────────────────────────────────────────────────────────────
//  Internal: all compression logic works through ReadFn / WriteFn.
//  The file-based public function just wraps FILE* into those.
// ─────────────────────────────────────────────────────────────────

std::string getAlgorithmName(int level) {
    if (level <= 3) return "LZ4 Fast";
    if (level <= 6) return "LZ4-HC";
    if (level <= 15) return "ZSTD";
    if (level <= 17) return "LZMA2 Ultra";
    if (level <= 19) return "LZMA2 Maximum";
    return "LZMA2 Extreme";
}

bool compressStream(ReadFn src, WriteFn dst, int compression_lvl, size_t input_size) {
    if (compression_lvl < 1 || compression_lvl > 21) {
        Logger::Log(LOG_ERROR, "Compression level must be between 1 and 21");
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    size_t total_compressed = 0;
    const size_t CHUNK_SIZE = 4 * 1024 * 1024;
    std::string algorithm_used = getAlgorithmName(compression_lvl);

    try {
        std::unique_ptr<char[]> inBuf(new char[CHUNK_SIZE]);
        std::unique_ptr<char[]> outBuf;
        size_t outBufSize = 0;

        // ── LZ4 / LZ4-HC ───────────────────────────────────────────
        if (compression_lvl <= 6) {
#ifdef USE_LZ4
            LZ4F_preferences_t prefs{};
            prefs.frameInfo.blockMode      = LZ4F_blockLinked;
            prefs.frameInfo.blockSizeID    = LZ4F_max4MB;
            prefs.compressionLevel =
                (compression_lvl == 1) ? 1 :
                (compression_lvl == 2) ? 3 :
                (compression_lvl == 3) ? 6 :
                (compression_lvl == 4) ? 4 :
                (compression_lvl == 5) ? 7 : 9;

            LZ4F_compressionContext_t cctx;
            if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION)))
                throw std::runtime_error("LZ4F context creation failed");

            outBufSize = LZ4F_compressBound(CHUNK_SIZE, &prefs);
            outBuf.reset(new char[outBufSize]);

            size_t headerSize = LZ4F_compressBegin(cctx, outBuf.get(), outBufSize, &prefs);
            if (LZ4F_isError(headerSize)) throw std::runtime_error("LZ4 header creation failed");
            if (!dst(outBuf.get(), headerSize)) throw std::runtime_error("Write error (LZ4 header)");
            total_compressed += headerSize;

            while (true) {
                size_t bytesRead = src(inBuf.get(), CHUNK_SIZE);
                if (bytesRead == 0) break;

                size_t compressedSize = LZ4F_compressUpdate(
                    cctx, outBuf.get(), outBufSize, inBuf.get(), bytesRead, nullptr);
                if (LZ4F_isError(compressedSize)) throw std::runtime_error("LZ4 compression error");
                if (!dst(outBuf.get(), compressedSize)) throw std::runtime_error("Write error (LZ4 data)");
                total_compressed += compressedSize;
            }

            size_t finalSize = LZ4F_compressEnd(cctx, outBuf.get(), outBufSize, nullptr);
            if (LZ4F_isError(finalSize)) throw std::runtime_error("LZ4 finalise error");
            if (!dst(outBuf.get(), finalSize)) throw std::runtime_error("Write error (LZ4 footer)");
            total_compressed += finalSize;
            LZ4F_freeCompressionContext(cctx);
            success = true;
#else
            throw std::runtime_error("LZ4 not available");
#endif
        }
        // ── ZSTD ────────────────────────────────────────────────────
        else if (compression_lvl <= 15) {
#ifdef USE_ZSTD
            ZSTD_CCtx* cctx = ZSTD_createCCtx();
            if (!cctx) throw std::runtime_error("ZSTD context creation failed");

            int zstd_level = std::min(19, compression_lvl + 5);
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, zstd_level);
            unsigned nbWorkers = std::thread::hardware_concurrency();
            if (nbWorkers > 1)
                ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nbWorkers);

            outBufSize = ZSTD_compressBound(CHUNK_SIZE);
            outBuf.reset(new char[outBufSize]);

            // Use read-ahead: read the next chunk before deciding the mode for
            // the current one. Only set ZSTD_e_end when ReadFn returns 0 (true EOF),
            // not on any short read — short reads happen legitimately on pipes/streams.
            size_t curRead = src(inBuf.get(), CHUNK_SIZE);

            while (true) {
                std::unique_ptr<char[]> nextBuf(new char[CHUNK_SIZE]);
                size_t nextRead = (curRead > 0) ? src(nextBuf.get(), CHUNK_SIZE) : 0;

                ZSTD_EndDirective mode = (nextRead == 0) ? ZSTD_e_end : ZSTD_e_continue;
                ZSTD_inBuffer input = { inBuf.get(), curRead, 0 };

                bool finished = false;
                while (!finished) {
                    ZSTD_outBuffer output = { outBuf.get(), outBufSize, 0 };
                    size_t remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
                    if (ZSTD_isError(remaining)) throw std::runtime_error("ZSTD compression error");
                    if (!dst(outBuf.get(), output.pos)) throw std::runtime_error("Write error (ZSTD)");
                    total_compressed += output.pos;
                    finished = (mode == ZSTD_e_end) ? (remaining == 0) : (input.pos == input.size);
                }

                if (nextRead == 0) break;
                memcpy(inBuf.get(), nextBuf.get(), nextRead);
                curRead = nextRead;
            }
            ZSTD_freeCCtx(cctx);
            success = true;
#else
            throw std::runtime_error("ZSTD not available");
#endif
        }
        // ── LZMA2 ───────────────────────────────────────────────────
        else {
#ifdef USE_LIBLZMA
            lzma_stream strm = LZMA_STREAM_INIT;
            lzma_options_lzma opt_lzma;
            lzma_lzma_preset(&opt_lzma, 9 | LZMA_PRESET_EXTREME);

            if (compression_lvl <= 17) {
                opt_lzma.dict_size = (compression_lvl == 16) ? (64U << 20) : (128U << 20);
                opt_lzma.depth     = 512;
            } else if (compression_lvl <= 19) {
                opt_lzma.dict_size = (compression_lvl == 18) ? (128U << 20) : (256U << 20);
                opt_lzma.depth     = 1000;
            } else {
                opt_lzma.dict_size = (compression_lvl == 20) ? (128U << 20) : (256U << 20);
                opt_lzma.depth     = 2000;
            }
            opt_lzma.nice_len = 273;
            opt_lzma.mf      = LZMA_MF_BT4;
            opt_lzma.lc = 4; opt_lzma.lp = 0; opt_lzma.pb = 2;

            lzma_filter filters[3];
            filters[1].id = LZMA_FILTER_LZMA2; filters[1].options = &opt_lzma;
            filters[2].id = LZMA_VLI_UNKNOWN;  filters[2].options = nullptr;
            if (compression_lvl >= 20) {
                filters[0].id = LZMA_FILTER_X86; filters[0].options = nullptr;
            } else {
                filters[0] = filters[1]; filters[1] = filters[2];
            }

            if (lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC64) != LZMA_OK)
                throw std::runtime_error("LZMA encoder initialization failed");

            outBufSize = CHUNK_SIZE;
            outBuf.reset(new char[outBufSize]);

            lzma_action action = LZMA_RUN;

            while (true) {
                if (strm.avail_in == 0 && action != LZMA_FINISH) {
                    size_t bytesRead = src(inBuf.get(), CHUNK_SIZE);
                    strm.next_in  = reinterpret_cast<uint8_t*>(inBuf.get());
                    strm.avail_in = bytesRead;
                    if (bytesRead == 0) action = LZMA_FINISH;  // ReadFn returns 0 = true EOF
                }

                strm.next_out  = reinterpret_cast<uint8_t*>(outBuf.get());
                strm.avail_out = outBufSize;
                lzma_ret ret   = lzma_code(&strm, action);

                size_t write_size = outBufSize - strm.avail_out;
                if (write_size > 0) {
                    if (!dst(outBuf.get(), write_size)) throw std::runtime_error("Write error (LZMA)");
                    total_compressed += write_size;
                }
                if (ret == LZMA_STREAM_END) { success = true; break; }
                if (ret != LZMA_OK) {
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
        Logger::Log(LOG_ERROR, std::string("Compression exception: ") + e.what());
        success = false;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (success) {
        std::ostringstream oss;
        oss << "Compression done: " << algorithm_used
            << " (level " << compression_lvl << ")";
        if (input_size > 0) {
            double ratio = static_cast<double>(input_size) / total_compressed;
            double speed = (input_size / (1024.0 * 1024.0)) / (time_ms / 1000.0);
            oss << "\n  Size: " << input_size << " -> " << total_compressed << " bytes"
                << "\n  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1"
                << "\n  Reduction: " << std::setprecision(1)
                << (100.0 * (1.0 - static_cast<double>(total_compressed) / input_size)) << "%"
                << "\n  Speed: " << std::setprecision(1) << speed << " MB/s";
        }
        oss << "\n  Time: " << std::fixed << std::setprecision(2) << time_ms << " ms";
        Logger::Log(LOG_INFO, oss.str());
    }
    return success;
}

// ── File-based wrapper (original public API, unchanged) ──────────
bool compressFile(const std::string& input_file,
                  const std::string& output_file,
                  int compression_lvl) {
    FILE* infile = fopen(input_file.c_str(), "rb");
    if (!infile) {
        Logger::Log(LOG_ERROR, "Cannot open input file: " + input_file);
        return false;
    }
    fseek(infile, 0, SEEK_END);
    size_t file_size = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    if (file_size == 0) { fclose(infile); Logger::Log(LOG_ERROR, "Input file is empty"); return false; }

    FILE* outfile = fopen(output_file.c_str(), "wb");
    if (!outfile) {
        fclose(infile);
        Logger::Log(LOG_ERROR, "Cannot create output file: " + output_file);
        return false;
    }

    bool ok = compressStream(read_fn_from_file(infile), write_fn_to_file(outfile),
                             compression_lvl, file_size);
    fclose(infile);
    fclose(outfile);
    if (!ok) std::remove(output_file.c_str());
    return ok;
}