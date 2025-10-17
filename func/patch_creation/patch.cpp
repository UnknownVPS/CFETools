#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstring>
#include <array>
#include <stdexcept>
#include "patch.h"
// -------------------------- Parameters --------------------------
constexpr size_t MICRO_MIN = 256;
constexpr size_t MICRO_AVG = 512;
constexpr size_t MICRO_MAX = 1024;

constexpr size_t SMALL_MIN = 1024;
constexpr size_t SMALL_AVG = 2048;
constexpr size_t SMALL_MAX = 4096;

constexpr size_t NORMAL_MIN = 2048;
constexpr size_t NORMAL_AVG = 4096;
constexpr size_t NORMAL_MAX = 8192;

constexpr uint32_t MICRO_MASK_S = (1u << 8) - 1u;
constexpr uint32_t MICRO_MASK_L = (1u << 9) - 1u;
constexpr uint32_t SMALL_MASK_S = (1u << 10) - 1u;
constexpr uint32_t SMALL_MASK_L = (1u << 11) - 1u;
constexpr uint32_t NORMAL_MASK_S = (1u << 11) - 1u;
constexpr uint32_t NORMAL_MASK_L = (1u << 12) - 1u;

constexpr size_t BUFFER_SIZE = 16 * 1024 * 1024;  // 16MB streaming
constexpr size_t BLOOM_SIZE  = 1024 * 1024;       // bits

// -------------------------- Utilities --------------------------
static inline bool read_exact(std::istream& in, void* dst, size_t n) {
    in.read(reinterpret_cast<char*>(dst), n);
    return static_cast<size_t>(in.gcount()) == n;
}

template<typename T>
static inline void write_val(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

// -------------------------- Gear Hash --------------------------
class OptimizedGearHash {
private:
    static constexpr uint32_t GEAR_TABLE[256] = {
        0x67ed26b7, 0x32da500f, 0x53d0fee3, 0xce620efb, 0xd4c8c93c, 0xc6f250bb, 0x30ddc3e8, 0xa2a8515e,
        0x3bc9bd50, 0x4b136f67, 0x67a3b0c9, 0x42f66ef8, 0xf0842d0d, 0xdc56c4e5, 0x7b8b26b7, 0x8c8ca762,
        0xb0e7b346, 0x2482fb41, 0x7e315179, 0x27ff677e, 0xf8d0dc3f, 0x49d4b519, 0x64b0f3a1, 0x8fe3b520,
        0xf4ef00a3, 0x2c2f8f2d, 0x2b41b5c7, 0x2d7e4a6e, 0x89f4c7c8, 0x9f49ff7e, 0x9e847c80, 0x3c9e96cb,
        0x7c7f9df6, 0x4ff61e9f, 0x3d3e7e41, 0x6e2d4e27, 0x0e3f4c30, 0xf2c4c8f3, 0x3f13e0e6, 0x4d73aaf9,
        0x3d3e8444, 0x5e8e6c8c, 0xa3e8e8c0, 0xd3b1c3c7, 0x8a8c8484, 0xe3c0c0c0, 0x8c8c8c8c, 0xc0c0c0c0,
        0x1e6e6e6e, 0x7a7a7a7a, 0x4f4f4f4f, 0xb3b3b3b3, 0x26262626, 0x9a9a9a9a, 0x5d5d5d5d, 0xe1e1e1e1,
        0x38383838, 0x8c8c8c8c, 0x70707070, 0xc4c4c4c4, 0x19191919, 0x6d6d6d6d, 0xa1a1a1a1, 0xd5d5d5d5,
        0x2a2a2a2a, 0x7e7e7e7e, 0xb2b2b2b2, 0xe6e6e6e6, 0x3b3b3b3b, 0x8f8f8f8f, 0xc3c3c3c3, 0xf7f7f7f7,
        0x4c4c4c4c, 0x90909090, 0xd4d4d4d4, 0x18181818, 0x5c5c5c5c, 0xa0a0a0a0, 0xe4e4e4e4, 0x28282828,
        0x6c6c6c6c, 0xb0b0b0b0, 0xf4f4f4f4, 0x39393939, 0x7d7d7d7d, 0xc1c1c1c1, 0x05050505, 0x49494949,
        0x8d8d8d8d, 0xd1d1d1d1, 0x16161616, 0x5a5a5a5a, 0x9e9e9e9e, 0xe2e2e2e2, 0x27272727, 0x6b6b6b6b,
        0xafafafaf, 0xf3f3f3f3, 0x37373737, 0x7b7b7b7b, 0xbfbfbfbf, 0x03030303, 0x47474747, 0x8b8b8b8b,
        0xcfcfcfcf, 0x13131313, 0x57575757, 0x9b9b9b9b, 0xdfdfdfdf, 0x23232323, 0x67676767, 0xabababab,
        0xefefefef, 0x33333333, 0x77777777, 0xbbbbbbbb, 0xffffffff, 0x43434343, 0x87878787, 0xcbcbcbcb,
        0x0f0f0f0f, 0x53535353, 0x97979797, 0xdbdbdbdb, 0x1f1f1f1f, 0x63636363, 0xa7a7a7a7, 0xebebebeb,
        0x2f2f2f2f, 0x73737373, 0xb7b7b7b7, 0xfbfbfbfb, 0x3f3f3f3f, 0x83838383, 0xc7c7c7c7, 0x0b0b0b0b,
        0x4f4f4f4f, 0x93939393, 0xd7d7d7d7, 0x1b1b1b1b, 0x5f5f5f5f, 0xa3a3a3a3, 0xe7e7e7e7, 0x2b2b2b2b,
        0x6f6f6f6f, 0xb3b3b3b3, 0xf7f7f7f7, 0x3b3b3b3b, 0x7f7f7f7f, 0xc3c3c3c3, 0x07070707, 0x4b4b4b4b,
        0x8f8f8f8f, 0xd3d3d3d3, 0x17171717, 0x5b5b5b5b, 0x9f9f9f9f, 0xe3e3e3e3, 0x27272727, 0x6b6b6b6b,
        0xafafafaf, 0xf3f3f3f3, 0x37373737, 0x7b7b7b7b, 0xbfbfbfbf, 0x03030303, 0x47474747, 0x8b8b8b8b,
        0xcfcfcfcf, 0x13131313, 0x57575757, 0x9b9b9b9b, 0xdfdfdfdf, 0x23232323, 0x67676767, 0xabababab,
        0xefefefef, 0x33333333, 0x77777777, 0xbbbbbbbb, 0xffffffff, 0x43434343, 0x87878787, 0xcbcbcbcb,
        0x0f0f0f0f, 0x53535353, 0x97979797, 0xdbdbdbdb, 0x1f1f1f1f, 0x63636363, 0xa7a7a7a7, 0xebebebeb
    };

    uint32_t hash;

public:
    OptimizedGearHash() : hash(0) {}
    inline void roll(uint8_t byte) { hash = (hash << 1) ^ GEAR_TABLE[byte]; }
    inline bool isBoundary(uint32_t mask, size_t pos, size_t target) const {
        uint32_t effective_mask = mask;
        if (pos < target)       effective_mask = (mask << 1) | 1u;
        else if (pos > target*3/2) effective_mask = (mask >> 1);
        return (hash & effective_mask) == 0u;
    }
    inline void reset() { hash = 0; }
};

// -------------------------- Hash --------------------------
static inline uint64_t ultraFastHash(const uint8_t* data, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint64_t prime = 0x100000001b3ULL;
    while (len >= 8) {
        uint64_t w; memcpy(&w, data, 8); // avoid unaligned UB
        h ^= w; h *= prime;
        data += 8; len -= 8;
    }
    while (len--) { h = (h ^ *data++) * prime; }
    return h ^ (h >> 32);
}

struct MultiLevelChunk { uint64_t pos; uint32_t len; uint64_t hash; uint8_t level; };

// -------------------------- Bloom --------------------------
class BloomFilter {
    std::vector<uint64_t> bits; // BLOOM_SIZE bits
    static inline size_t h1(uint64_t k){ return k % BLOOM_SIZE; }
    static inline size_t h2(uint64_t k){ return (k * 0x9e3779b97f4a7c15ULL) % BLOOM_SIZE; }
    static inline size_t h3(uint64_t k){ return (k * 0xc6a4a7935bd1e995ULL) % BLOOM_SIZE; }
public:
    BloomFilter() : bits(BLOOM_SIZE/64, 0) {}
    inline void insert(uint64_t v){ size_t a=h1(v), b=h2(v), c=h3(v); bits[a/64]|=1ULL<<(a%64); bits[b/64]|=1ULL<<(b%64); bits[c/64]|=1ULL<<(c%64); }
    inline bool mayContain(uint64_t v) const { size_t a=h1(v), b=h2(v), c=h3(v); return (bits[a/64]>> (a%64) &1ULL) && (bits[b/64]>> (b%64) &1ULL) && (bits[c/64]>> (c%64) &1ULL); }
};

// -------------------------- OptimizedFastCDC --------------------------
class OptimizedFastCDC {
    std::unordered_map<uint64_t, uint64_t> chunkIndex; // hash -> src pos
    BloomFilter bloomFilter;
    std::vector<uint8_t> ioBuf; // shared streaming buffer

    std::vector<MultiLevelChunk> findMultiLevelChunks(const char* filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file");
        std::vector<MultiLevelChunk> chunks;
        if (ioBuf.size() < BUFFER_SIZE) ioBuf.resize(BUFFER_SIZE);

        OptimizedGearHash gh;
        uint64_t filePos = 0, chunkStart = 0; size_t chunkLen = 0;
        std::vector<uint8_t> chunkBuf; chunkBuf.reserve(NORMAL_MAX);

        while (file) {
            file.read(reinterpret_cast<char*>(ioBuf.data()), BUFFER_SIZE);
            size_t bytesRead = file.gcount();
            for (size_t i=0;i<bytesRead;i++){
                uint8_t byte = ioBuf[i];
                chunkBuf.push_back(byte); gh.roll(byte); chunkLen++;
                uint8_t level = 2; bool isEnd = false;
                if (chunkLen >= MICRO_MIN) {
                    if (gh.isBoundary(MICRO_MASK_S, chunkLen, MICRO_AVG) || chunkLen >= MICRO_MAX) { level=0; isEnd=true; }
                }
                if (!isEnd && chunkLen >= SMALL_MIN) {
                    uint32_t mask = (chunkLen <= SMALL_AVG) ? SMALL_MASK_S : SMALL_MASK_L;
                    if (gh.isBoundary(mask, chunkLen, SMALL_AVG) || chunkLen >= SMALL_MAX) { level=1; isEnd=true; }
                }
                if (!isEnd && chunkLen >= NORMAL_MIN) {
                    uint32_t mask = (chunkLen <= NORMAL_AVG) ? NORMAL_MASK_S : NORMAL_MASK_L;
                    if (gh.isBoundary(mask, chunkLen, NORMAL_AVG) || chunkLen >= NORMAL_MAX) { level=2; isEnd=true; }
                }
                if (isEnd) {
                    uint64_t h = ultraFastHash(chunkBuf.data(), chunkLen);
                    chunks.push_back({chunkStart, static_cast<uint32_t>(chunkLen), h, level});
                    chunkStart = filePos + i + 1; chunkLen = 0; chunkBuf.clear(); gh.reset();
                }
            }
            filePos += bytesRead;
        }
        if (chunkLen > 0) {
            uint64_t h = ultraFastHash(chunkBuf.data(), chunkLen);
            chunks.push_back({chunkStart, static_cast<uint32_t>(chunkLen), h, 2});
        }
        return chunks;
    }

    inline bool fastVerifyChunk(std::ifstream& file, uint64_t pos, const uint8_t* data, uint32_t len, uint64_t hash) {
        if (!bloomFilter.mayContain(hash)) return false;
        static std::vector<uint8_t> tmp; if (tmp.size() < len) tmp.resize(len);
        file.clear(); file.seekg(pos);
        file.read(reinterpret_cast<char*>(tmp.data()), len);
        return static_cast<size_t>(file.gcount()) == len && memcmp(tmp.data(), data, len) == 0;
    }

    // Streaming insert from dst into patch
    static void emit_insert(std::ofstream& patch, std::ifstream& dst, uint64_t off, uint64_t len, std::vector<uint8_t>& buf){
        if (len == 0) return; uint8_t cmd = 1; write_val(patch, cmd);
        uint32_t len32 = static_cast<uint32_t>(len); // command format uses u32 length
        write_val(patch, len32);
        dst.clear(); dst.seekg(off);
        uint64_t remaining = len;
        while (remaining){
            uint32_t toRead = static_cast<uint32_t>(std::min<uint64_t>(remaining, buf.size()));
            dst.read(reinterpret_cast<char*>(buf.data()), toRead);
            if (static_cast<size_t>(dst.gcount()) != toRead) throw std::runtime_error("dst short read in INSERT");
            patch.write(reinterpret_cast<const char*>(buf.data()), toRead);
            remaining -= toRead;
        }
    }

public:
    void createPatch(const char* srcFile, const char* dstFile, const char* patchFile){
        if (ioBuf.size() < BUFFER_SIZE) ioBuf.resize(BUFFER_SIZE);
        std::ifstream src(srcFile, std::ios::binary);
        std::ifstream dst(dstFile, std::ios::binary);
        std::ofstream patch(patchFile, std::ios::binary);
        if (!src || !dst || !patch) throw std::runtime_error("Cannot open files");

        // Expected output size header
        dst.seekg(0, std::ios::end); uint64_t dstSize = dst.tellg(); dst.seekg(0);
        uint8_t hdr = 0xFF; write_val(patch, hdr); write_val(patch, dstSize);

        // Build index from source
        auto srcChunks = findMultiLevelChunks(srcFile);
        for (const auto& c : srcChunks) { chunkIndex[c.hash] = c.pos; bloomFilter.insert(c.hash); }

        // Process destination
        auto dstChunks = findMultiLevelChunks(dstFile);
        std::vector<uint8_t> chunkData; chunkData.reserve(NORMAL_MAX);

        uint64_t cursor = 0; // emitted up to this dst offset
        for (const auto& c : dstChunks) {
            auto it = chunkIndex.find(c.hash);
            if (it == chunkIndex.end()) continue;

            // Read candidate chunk from dst and verify with src
            if (chunkData.size() < c.len) chunkData.resize(c.len);
            dst.clear(); dst.seekg(c.pos);
            dst.read(reinterpret_cast<char*>(chunkData.data()), c.len);
            if (static_cast<size_t>(dst.gcount()) != c.len) continue;
            if (!fastVerifyChunk(src, it->second, chunkData.data(), c.len, c.hash)) continue;

            // Emit gap [cursor, c.pos)
            if (c.pos > cursor) {
                uint64_t gapLen = c.pos - cursor;
                emit_insert(patch, dst, cursor, gapLen, ioBuf);
            }

            // Emit COPY (cmd=0, pos, len)
            uint8_t cmd = 0; write_val(patch, cmd);
            write_val(patch, it->second);
            write_val(patch, c.len);

            cursor = c.pos + c.len;
        }

        // Tail
        if (cursor < dstSize) {
            uint64_t tail = dstSize - cursor;
            emit_insert(patch, dst, cursor, tail, ioBuf);
        }
    }

    void applyPatch(const char* srcFile, const char* patchFile, const char* outFile){
        if (ioBuf.size() < BUFFER_SIZE) ioBuf.resize(BUFFER_SIZE);
        std::ifstream src(srcFile, std::ios::binary);
        std::ifstream patch(patchFile, std::ios::binary);
        std::ofstream out(outFile, std::ios::binary);
        if (!src || !patch || !out) throw std::runtime_error("Cannot open files");

        auto read_u8  = [&](uint8_t &v){ return read_exact(patch, &v, 1); };
        auto read_u32 = [&](uint32_t&v){ return read_exact(patch, &v, 4); };
        auto read_u64 = [&](uint64_t&v){ return read_exact(patch, &v, 8); };

        // Header
        uint8_t hdr; uint64_t expected;
        if (!read_u8(hdr) || hdr != 0xFF || !read_u64(expected)) throw std::runtime_error("bad patch header");
        uint64_t written = 0;

        while (true) {
            int p = patch.peek(); if (p == EOF) break;
            uint8_t cmd; if (!read_u8(cmd)) break;
            if (cmd == 0) {
                uint64_t srcPos; uint32_t len;
                if (!read_u64(srcPos) || !read_u32(len)) throw std::runtime_error("bad COPY record");
                src.clear(); src.seekg(srcPos);
                uint32_t remaining = len;
                while (remaining){
                    uint32_t toRead = std::min<uint32_t>(remaining, static_cast<uint32_t>(ioBuf.size()));
                    src.read(reinterpret_cast<char*>(ioBuf.data()), toRead);
                    if (static_cast<size_t>(src.gcount()) != toRead) throw std::runtime_error("src short read in COPY");
                    out.write(reinterpret_cast<const char*>(ioBuf.data()), toRead);
                    remaining -= toRead;
                }
                written += len;
            } else if (cmd == 1) {
                uint32_t len; if (!read_u32(len)) throw std::runtime_error("bad INSERT record");
                uint32_t remaining = len;
                while (remaining){
                    uint32_t toRead = std::min<uint32_t>(remaining, static_cast<uint32_t>(ioBuf.size()));
                    if (!read_exact(patch, ioBuf.data(), toRead)) throw std::runtime_error("patch short read in INSERT");
                    out.write(reinterpret_cast<const char*>(ioBuf.data()), toRead);
                    remaining -= toRead;
                }
                written += len;
            } else {
                throw std::runtime_error("unknown command");
            }
        }

        if (written != expected) throw std::runtime_error("reconstructed size mismatch");
    }
};

// Include or forward-declare your internal class
// class OptimizedFastCDC { public: void createPatch(const char*, const char*, const char*); void applyPatch(const char*, const char*, const char*); };

namespace {
    // If the full class is defined in another included .cpp, include it here or compile that .cpp too.
    // Prefer: keep full class definition in a dedicated .cpp and compile it.
}

namespace fastcdc {

void createPatch(const char* src, const char* dst, const char* patch) {
    OptimizedFastCDC cdc;
    cdc.createPatch(src, dst, patch);
}

void applyPatch(const char* src, const char* patch, const char* out) {
    OptimizedFastCDC cdc;
    cdc.applyPatch(src, patch, out);
}

} // namespace fastcdc
