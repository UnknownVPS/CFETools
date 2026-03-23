#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <cstdio>

#ifdef USE_ZSTD
#include <zstd.h>
#endif
#ifdef USE_LZ4
#include <lz4frame.h>
#endif
#ifdef USE_LIBLZMA
#include <lzma.h>
#endif

#include "decompress.h"

// ─────────────────────────────────────────────────────────────────
//  Format detection — works from raw bytes, no seeking needed.
// ─────────────────────────────────────────────────────────────────

CompressionFormat detectFormatFromHeader(const uint8_t h[8]) {
    // LZ4F magic: 0x184D2204 (LE)
    if (h[0]==0x04 && h[1]==0x22 && h[2]==0x4D && h[3]==0x18)
        return CompressionFormat::LZ4F;
    // ZSTD magic: 0x28B52FFD (LE)
    if (h[0]==0x28 && h[1]==0xB5 && h[2]==0x2F && h[3]==0xFD)
        return CompressionFormat::ZSTD;
    // XZ magic: FD 37 7A 58 5A 00
    if (h[0]==0xFD && h[1]==0x37 && h[2]==0x7A &&
        h[3]==0x58 && h[4]==0x5A && h[5]==0x00)
        return CompressionFormat::LZMA2_XZ;
    return CompressionFormat::UNKNOWN;
}

static CompressionFormat detectFromFile(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return CompressionFormat::UNKNOWN;
    uint8_t h[8] = {};
    f.read(reinterpret_cast<char*>(h), 8);
    return detectFormatFromHeader(h);
}

static std::string formatName(CompressionFormat fmt) {
    switch (fmt) {
        case CompressionFormat::LZ4F:    return "LZ4 Frame";
        case CompressionFormat::ZSTD:    return "ZSTD";
        case CompressionFormat::LZMA2_XZ:return "LZMA2/XZ";
        default:                          return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────
//  Core streaming decompressor.
//  peek_header: the first 8 bytes already consumed from the stream.
//  src:         callback for the remaining bytes (after those 8).
// ─────────────────────────────────────────────────────────────────

bool decompressStream(const uint8_t peek_header[8], ReadFn src, WriteFn dst) {
    CompressionFormat fmt = detectFormatFromHeader(peek_header);
    if (fmt == CompressionFormat::UNKNOWN) {
        Logger::Log(LOG_ERROR, "decompressStream: unknown format magic");
        return false;
    }
    Logger::Log(LOG_INFO, "Detected format: " + formatName(fmt));

    // We have already consumed 8 bytes; prepend them to the stream via a
    // small stateful wrapper so the decoders see a complete, contiguous stream.
    uint8_t leftover[8];
    memcpy(leftover, peek_header, 8);
    size_t leftover_pos = 0;

    // Wraps `src` so it first replays the 8 peeked bytes then calls the real src.
    auto full_src = [&](void* buf, size_t len) -> size_t {
        size_t out = 0;
        uint8_t* b = static_cast<uint8_t*>(buf);
        // Drain leftover first
        if (leftover_pos < 8) {
            size_t avail = 8 - leftover_pos;
            size_t take  = std::min(avail, len);
            memcpy(b, leftover + leftover_pos, take);
            leftover_pos += take;
            out += take;
            b   += take;
            len -= take;
        }
        if (len > 0) out += src(b, len);
        return out;
    };

    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;
    size_t total_out = 0;
    const size_t CHUNK = 4 * 1024 * 1024;

    try {
        // ── LZ4F ───────────────────────────────────────────────────
        if (fmt == CompressionFormat::LZ4F) {
#ifdef USE_LZ4
            LZ4F_decompressionContext_t dctx;
            if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION)))
                throw std::runtime_error("LZ4F context creation failed");

            const size_t IN_SZ  = 256 * 1024;
            const size_t OUT_SZ = 4   * 1024 * 1024;
            std::vector<char> inBuf(IN_SZ), outBuf(OUT_SZ);
            bool done = false;

            while (!done) {
                size_t bytesRead = full_src(inBuf.data(), IN_SZ);
                if (bytesRead == 0) break;

                size_t srcConsumed = bytesRead;
                const char* srcPtr = inBuf.data();

                while (srcConsumed > 0) {
                    size_t dstCap  = OUT_SZ;
                    size_t srcUsed = srcConsumed;
                    size_t ret = LZ4F_decompress(dctx, outBuf.data(), &dstCap,
                                                 srcPtr, &srcUsed, nullptr);
                    if (LZ4F_isError(ret)) throw std::runtime_error("LZ4F decompress error");
                    if (dstCap > 0) {
                        if (!dst(outBuf.data(), dstCap)) throw std::runtime_error("Write error (LZ4)");
                        total_out += dstCap;
                    }
                    srcPtr       += srcUsed;
                    srcConsumed  -= srcUsed;
                    if (ret == 0) { done = true; break; }
                }
            }
            LZ4F_freeDecompressionContext(dctx);
            success = true;
#else
            throw std::runtime_error("LZ4 not compiled in");
#endif
        }
        // ── ZSTD ───────────────────────────────────────────────────
        else if (fmt == CompressionFormat::ZSTD) {
#ifdef USE_ZSTD
            ZSTD_DCtx* dctx = ZSTD_createDCtx();
            if (!dctx) throw std::runtime_error("ZSTD context creation failed");

            std::vector<char> inBuf(CHUNK), outBuf(CHUNK * 4);
            while (true) {
                size_t bytesRead = full_src(inBuf.data(), CHUNK);
                if (bytesRead == 0) break;

                ZSTD_inBuffer input = { inBuf.data(), bytesRead, 0 };
                while (input.pos < input.size) {
                    ZSTD_outBuffer output = { outBuf.data(), outBuf.size(), 0 };
                    size_t ret = ZSTD_decompressStream(dctx, &output, &input);
                    if (ZSTD_isError(ret)) throw std::runtime_error("ZSTD decompress error");
                    if (output.pos > 0) {
                        if (!dst(outBuf.data(), output.pos)) throw std::runtime_error("Write error (ZSTD)");
                        total_out += output.pos;
                    }
                }
            }
            ZSTD_freeDCtx(dctx);
            success = true;
#else
            throw std::runtime_error("ZSTD not compiled in");
#endif
        }
        // ── LZMA2/XZ ───────────────────────────────────────────────
        else {
#ifdef USE_LIBLZMA
            lzma_stream strm = LZMA_STREAM_INIT;
            if (lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK)
                throw std::runtime_error("LZMA decoder init failed");

            std::vector<uint8_t> inBuf(CHUNK), outBuf(CHUNK * 4);
            lzma_action action = LZMA_RUN;
            bool eof = false;

            while (true) {
                if (strm.avail_in == 0 && !eof) {
                    size_t r = full_src(inBuf.data(), CHUNK);
                    strm.next_in  = inBuf.data();
                    strm.avail_in = r;
                    if (r < CHUNK) { action = LZMA_FINISH; eof = true; }
                }
                strm.next_out  = outBuf.data();
                strm.avail_out = outBuf.size();
                lzma_ret ret   = lzma_code(&strm, action);
                size_t ws = outBuf.size() - strm.avail_out;
                if (ws > 0) {
                    if (!dst(outBuf.data(), ws)) throw std::runtime_error("Write error (LZMA)");
                    total_out += ws;
                }
                if (ret == LZMA_STREAM_END) { success = true; break; }
                if (ret != LZMA_OK) throw std::runtime_error("LZMA decode error");
            }
            lzma_end(&strm);
#else
            throw std::runtime_error("liblzma not compiled in");
#endif
        }
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, std::string("Decompression exception: ") + e.what());
        return false;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    if (success)
        Logger::Log(LOG_INFO, "Decompression done in " +
                    std::to_string(static_cast<int>(ms)) + " ms, " +
                    std::to_string(total_out) + " bytes out");
    return success;
}

// ─────────────────────────────────────────────────────────────────
//  File-based wrapper (original public API, unchanged).
// ─────────────────────────────────────────────────────────────────

bool decompressFile(const std::string& input_file, const std::string& output_file) {
    // Peek at header bytes then open a streaming read.
    std::ifstream probe(input_file, std::ios::binary);
    if (!probe) {
        Logger::Log(LOG_ERROR, "Cannot open: " + input_file);
        return false;
    }
    uint8_t hdr[8] = {};
    probe.read(reinterpret_cast<char*>(hdr), 8);
    probe.close();

    FILE* infile  = fopen(input_file.c_str(),  "rb");
    FILE* outfile = fopen(output_file.c_str(), "wb");
    if (!infile || !outfile) {
        if (infile)  fclose(infile);
        if (outfile) fclose(outfile);
        Logger::Log(LOG_ERROR, "Cannot open files for decompression");
        return false;
    }

    // For file-based path, src already has full data — pass a zero-byte peek.
    // We re-read from the beginning so nothing is skipped.
    uint8_t zero_peek[8] = {};  // no bytes pre-consumed
    ReadFn src = [infile](void* b, size_t n) -> size_t { return fread(b, 1, n, infile); };
    WriteFn dst = [outfile](const void* b, size_t n) -> bool { return fwrite(b, 1, n, outfile) == n; };

    // Use the header we peeked but let the ReadFn replay from offset 0 (FILE* is still at 0).
    bool ok = decompressStream(hdr, src, dst);
    fclose(infile);
    fclose(outfile);
    if (!ok) std::remove(output_file.c_str());
    return ok;
}

bool isCompressedFile(const std::string& filename) {
    return detectFromFile(filename) != CompressionFormat::UNKNOWN;
}

std::string getFileCompressionFormat(const std::string& filename) {
    return formatName(detectFromFile(filename));
}