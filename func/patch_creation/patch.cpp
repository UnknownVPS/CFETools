#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstring>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cerrno>
#include "../../utils/logger/logger.h"
#include "../../version.h"
#include "../../utils/hashers/fileHasher.hpp"

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

struct SignatureHeader {
    uint32_t magic = 0x5349474E; // "SIGN"
    uint64_t srcSize;
    uint8_t hashLen;
    char srcHash[64];
};

struct MultiLevelChunk { 
    uint64_t pos; 
    uint32_t len; 
    uint64_t hash64;    // For standard createPatch (local memcmp)
    XXH128_hash_t hash128; // For signatures (collision-proof)
    uint8_t level; 
};

struct SigEntry {
    XXH128_hash_t hash; // 128-bit on disk
    uint64_t pos;
    uint32_t len;
} __attribute__((packed)); // Ensure cross-platform binary compatibility

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

// -------------------------- Fast Hash (Content Defined) --------------------------
static inline uint64_t ultraFastHash(const uint8_t* data, size_t len) {
    return XXH3_64bits(data, len);
}



// -------------------------- Bloom Filter --------------------------
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
        if (!file) throw std::runtime_error(std::string("Cannot open file: ") + filename);
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
                
                // Evaluate boundaries without hard-capping early
                if (chunkLen >= NORMAL_MIN && gh.isBoundary((chunkLen <= NORMAL_AVG) ? NORMAL_MASK_S : NORMAL_MASK_L, chunkLen, NORMAL_AVG)) {
                    level = 2; isEnd = true;
                } else if (chunkLen >= SMALL_MIN && gh.isBoundary((chunkLen <= SMALL_AVG) ? SMALL_MASK_S : SMALL_MASK_L, chunkLen, SMALL_AVG)) {
                    level = 1; isEnd = true;
                } else if (chunkLen >= MICRO_MIN && gh.isBoundary(MICRO_MASK_S, chunkLen, MICRO_AVG)) {
                    level = 0; isEnd = true;
                }

                // ONLY force the chunk to end if it reaches the absolute maximum limit
                if (chunkLen >= NORMAL_MAX) {
                    level = 2; isEnd = true;
                }
                // Inside the isEnd block of findMultiLevelChunks:
                if (isEnd) {
                    // We compute both. XXH3 is so fast this won't hurt performance.
                    XXH128_hash_t h128 = XXH3_128bits(chunkBuf.data(), chunkLen);
                    uint64_t h64 = h128.low64; // Use the lower 64 bits of the 128-bit hash as the 64-bit version

                    chunks.push_back({chunkStart, static_cast<uint32_t>(chunkLen), h64, h128, level});
                    
                    chunkStart = filePos + i + 1; 
                    chunkLen = 0; 
                    chunkBuf.clear(); 
                    gh.reset();
                }
            }
            filePos += bytesRead;
        }
        if (chunkLen > 0) {
            XXH128_hash_t h128 = XXH3_128bits(chunkBuf.data(), chunkLen);
            chunks.push_back({chunkStart, static_cast<uint32_t>(chunkLen), h128.low64, h128, 2});
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
    static void emit_insert(std::ofstream& patch, std::ifstream& dst, uint64_t off, uint64_t len, std::vector<uint8_t>& buf) {
        if (len == 0) return;
        uint8_t cmd = 1;
        write_val(patch, cmd);
        
        // Write the length as 64-bit (8 bytes) to handle >4GB inserts
        write_val(patch, len); 
        
        dst.clear(); 
        dst.seekg(off);
        
        uint64_t remaining = len;
        while (remaining) {
            uint32_t toRead = static_cast<uint32_t>(std::min<uint64_t>(remaining, buf.size()));
            dst.read(reinterpret_cast<char*>(buf.data()), toRead);
            
            if (static_cast<size_t>(dst.gcount()) != toRead) 
                throw std::runtime_error("dst short read in INSERT");
                
            patch.write(reinterpret_cast<const char*>(buf.data()), toRead);
            remaining -= toRead;
        }
    }

public:
    void createSignature(const char* srcFile, const char* sigFile) {
        // 1. Compute Source Metadata
        std::string srcHashStr = fileHasher::xxhash_file(srcFile);
        std::ifstream src(srcFile, std::ios::binary | std::ios::ate);
        uint64_t srcSize = src.tellg();
        src.close();

        // 2. Generate Chunks
        auto chunks = findMultiLevelChunks(srcFile);

        // 3. Write Signature File
        std::ofstream sig(sigFile, std::ios::binary);
        SignatureHeader hdr;
        hdr.srcSize = srcSize;
        hdr.hashLen = static_cast<uint8_t>(srcHashStr.size());
        std::memcpy(hdr.srcHash, srcHashStr.c_str(), hdr.hashLen);
        
        write_val(sig, hdr);
        for (const auto& c : chunks) {
            SigEntry entry = {c.hash128, c.pos, c.len}; // Save 128-bit
            write_val(sig, entry);
        }
    }

    void createPatchFromSignature(const char* sigFile, const char* dstFile, const char* patchFile) {
        if (ioBuf.size() < BUFFER_SIZE) ioBuf.resize(BUFFER_SIZE);
        std::ifstream sig(sigFile, std::ios::binary);
        std::ifstream dst(dstFile, std::ios::binary);
        std::ofstream patch(patchFile, std::ios::binary);
        if (!sig || !dst || !patch) throw std::runtime_error("Cannot open files for signature patching");

        // 1. Load Signature
        SignatureHeader hdr;
        if (!read_exact(sig, &hdr, sizeof(SignatureHeader))) throw std::runtime_error("Invalid signature header");
        
        std::string srcHashStr(hdr.srcHash, hdr.hashLen);
        
        while (true) {
            SigEntry e; // This now contains XXH128_hash_t hash;
            if (!read_exact(sig, &e, sizeof(SigEntry))) break;

            // --- CHANGE HERE ---
            // We use the low 64 bits of the 128-bit hash for our RAM-efficient map.
            // This makes the logic identical to the 64-bit createPatch method.
            uint64_t h64 = e.hash.low64; 
            chunkIndex[h64] = e.pos;
            bloomFilter.insert(h64);
        }

        // 2. Setup Patch Header (Identical to createPatch)
        dst.seekg(0, std::ios::end);
        uint64_t dstSize = dst.tellg();
        dst.seekg(0);

        uint8_t patchHdr = 0xFF; 
        write_val(patch, patchHdr); 
        write_val(patch, dstSize);
        
        std::string versionStr = std::string(VERSION);
        uint8_t verLen = static_cast<uint8_t>(versionStr.size());
        write_val(patch, verLen);
        patch.write(versionStr.c_str(), verLen);

        uint8_t hashLen = static_cast<uint8_t>(srcHashStr.size());
        write_val(patch, hashLen);
        patch.write(srcHashStr.c_str(), hashLen);

        // 3. Process Destination and find matches
        auto dstChunks = findMultiLevelChunks(dstFile);
        uint64_t cursor = 0;
        bool hasPendingCopy = false;
        uint64_t pendingSrcPos = 0, pendingLen = 0;

        auto flush_copy = [&]() {
            if (hasPendingCopy) {
                uint8_t cmd = 0; write_val(patch, cmd);
                write_val(patch, pendingSrcPos);
                write_val(patch, pendingLen);
                hasPendingCopy = false;
            }
        };

        for (const auto& c : dstChunks) {
            // --- CHANGE HERE ---
            // Use c.hash64 (which you now derive from the 128-bit low64 in findMultiLevelChunks)
            auto it = chunkIndex.find(c.hash64);
            
            if (it != chunkIndex.end() && bloomFilter.mayContain(c.hash64)) {
                if (c.pos > cursor) {
                    flush_copy();
                    emit_insert(patch, dst, cursor, c.pos - cursor, ioBuf);
                }

                if (hasPendingCopy && it->second == pendingSrcPos + pendingLen && c.pos == cursor) {
                    pendingLen += c.len;
                } else {
                    flush_copy();
                    pendingSrcPos = it->second;
                    pendingLen = c.len;
                    hasPendingCopy = true;
                }
                cursor = c.pos + c.len;
            }
        }

        flush_copy();
        if (cursor < dstSize) emit_insert(patch, dst, cursor, dstSize - cursor, ioBuf);
    }
    void createPatch(const char* srcFile, const char* dstFile, const char* patchFile){
        if (ioBuf.size() < BUFFER_SIZE) ioBuf.resize(BUFFER_SIZE);
        std::ifstream src(srcFile, std::ios::binary);
        std::ifstream dst(dstFile, std::ios::binary);
        std::ofstream patch(patchFile, std::ios::binary);
        if (!src || !dst || !patch) throw std::runtime_error("Cannot open files");

        // 1. Calculate Destination Size (for Header)
        dst.seekg(0, std::ios::end);
        uint64_t dstSize = dst.tellg();
        dst.seekg(0);

        // 2. Source File Verification Hash (XXHash)
        std::string srcHashStr;
        try {
            srcHashStr = fileHasher::xxhash_file(srcFile);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to hash source file: ") + e.what());
        }

        // 3. Version String
        std::string versionStr = std::string(VERSION);

        // Write Patch Header
        // [Magic 1B] [DstSize 8B] [VersionLen 1B] [VersionStr ...] [SrcHashLen 1B] [SrcHashStr ...]
        uint8_t hdr = 0xFF; 
        write_val(patch, hdr); 
        
        write_val(patch, dstSize);
        
        uint8_t verLen = static_cast<uint8_t>(versionStr.size());
        if (verLen > 255) throw std::runtime_error("Version string too long");
        write_val(patch, verLen);
        patch.write(versionStr.c_str(), verLen);

        uint8_t hashLen = static_cast<uint8_t>(srcHashStr.size());
        if (hashLen > 64) throw std::runtime_error("Computed hash length is invalid");
        write_val(patch, hashLen);
        patch.write(srcHashStr.c_str(), hashLen);

        // Build index from source
        auto srcChunks = findMultiLevelChunks(srcFile);
        for (const auto& c : srcChunks) { chunkIndex[c.hash64] = c.pos; bloomFilter.insert(c.hash64); }

        // Process destination
        auto dstChunks = findMultiLevelChunks(dstFile);
        std::vector<uint8_t> chunkData; chunkData.reserve(NORMAL_MAX);

        uint64_t cursor = 0; // emitted up to this dst offset

        // State for coalescing contiguous COPY commands
        bool hasPendingCopy = false;
        uint64_t pendingSrcPos = 0;
        uint64_t pendingLen = 0;

        auto flush_copy = [&]() {
            if (hasPendingCopy) {
                uint8_t cmd = 0; write_val(patch, cmd);
                write_val(patch, pendingSrcPos);
                write_val(patch, pendingLen);
                hasPendingCopy = false;
            }
        };

        for (const auto& c : dstChunks) {
            auto it = chunkIndex.find(c.hash64);
            if (it == chunkIndex.end()) continue;

            // Read candidate chunk from dst and verify with src
            if (chunkData.size() < c.len) chunkData.resize(c.len);
            dst.clear(); dst.seekg(c.pos);
            dst.read(reinterpret_cast<char*>(chunkData.data()), c.len);
            if (static_cast<size_t>(dst.gcount()) != c.len) continue;
            if (!fastVerifyChunk(src, it->second, chunkData.data(), c.len, c.hash64)) continue;

            // Emit gap [cursor, c.pos) as an INSERT
            if (c.pos > cursor) {
                flush_copy(); // Flush any pending copies before inserting
                uint64_t gapLen = c.pos - cursor;
                emit_insert(patch, dst, cursor, gapLen, ioBuf);
            }

            // Coalesce COPY commands if contiguous in BOTH source and destination
            if (hasPendingCopy && 
                it->second == pendingSrcPos + pendingLen && // Contiguous in Source
                c.pos == cursor)                          // Contiguous in Destination
            {
                pendingLen += c.len; // Merge!
            } else {
                flush_copy(); // Write old copy, start a new one
                pendingSrcPos = it->second;
                pendingLen = c.len;
                hasPendingCopy = true;
            }

            cursor = c.pos + c.len;
        }

        flush_copy(); // flush final

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

        // Inside applyPatch
        uint8_t hdr; 
        if (!read_u8(hdr)) throw std::runtime_error("Empty patch");
        // Since you are in dev, you can just log it or check it simply
        if (hdr != 0xFF) Logger::Log(LOG_WARNING, "Experimental patch format detected");

        uint64_t expected;
        if (!read_u64(expected)) throw std::runtime_error("Missing size in header");

        uint8_t versionLen;
        if (!read_u8(versionLen)) throw std::runtime_error("Missing version length");

        // Read Version
        std::vector<char> verBuf(versionLen);
        if (!read_exact(patch, verBuf.data(), versionLen)) {
            throw std::runtime_error("bad patch header (version truncated)");
        }
        std::string patchVersion(verBuf.data(), versionLen);
        Logger::Log(LOG_INFO, "Patch created using: " + patchVersion);
        // Read Hash
        uint8_t storedHashLen;
        if (!read_u8(storedHashLen)) {
            throw std::runtime_error("bad patch header (hash len)");
        }
        if (storedHashLen > 64) throw std::runtime_error("Invalid hash length in patch");
        
        std::vector<char> storedHashBuf(storedHashLen);
        if (!read_exact(patch, storedHashBuf.data(), storedHashLen)) {
            throw std::runtime_error("bad patch header (hash truncated)");
        }
        std::string storedHash(storedHashBuf.data(), storedHashLen);

        // Source File Verification (XXHash)
        std::string actualSrcHash;
        try {
            actualSrcHash = fileHasher::xxhash_file(srcFile);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to hash current source file: ") + e.what());
        }

        if (storedHash != actualSrcHash) {
            throw std::runtime_error("Source file hash mismatch! The patch was created for a different version of the source file.");
        }

        // Determine source size for validation
        src.seekg(0, std::ios::end);
        uint64_t srcSize = src.tellg();
        src.seekg(0);

        // Helper to check stream errors
        auto checkOutStream = [&]() {
            if (!out.good()) {
                if (errno == ENOSPC) {
                    throw std::runtime_error("No space left on device while writing output.");
                }
                throw std::runtime_error("Write failed while applying patch (Disk full or IO error).");
            }
        };

        uint64_t written = 0;

        while (true) {
            int p = patch.peek(); if (p == EOF) break;
            uint8_t cmd; if (!read_u8(cmd)) break;
            
            if (cmd == 0) {
                // COPY
                uint64_t srcPos; uint64_t len;
                if (!read_u64(srcPos) || !read_u64(len)) throw std::runtime_error("bad COPY record");
                
                // Input Validation: Check bounds
                if (srcPos > srcSize) throw std::runtime_error("Patch error: Copy offset exceeds source file size");
                if (static_cast<uint64_t>(srcPos) + len > srcSize) throw std::runtime_error("Patch error: Copy length exceeds source file size");

                src.clear(); src.seekg(srcPos);
                uint64_t remaining = len;
                while (remaining){
                    uint32_t toRead = std::min<uint32_t>(remaining, static_cast<uint64_t>(ioBuf.size()));
                    src.read(reinterpret_cast<char*>(ioBuf.data()), toRead);
                    if (static_cast<size_t>(src.gcount()) != toRead) throw std::runtime_error("src short read in COPY");
                    
                    out.write(reinterpret_cast<const char*>(ioBuf.data()), toRead);
                    checkOutStream(); 

                    remaining -= toRead;
                    written += toRead;
                }
            } else if (cmd == 1) {
                // INSERT [cmd 1B] [len 8B] [data...]
                uint64_t len;
                if (!read_u64(len)) throw std::runtime_error("bad INSERT record");
                
                uint64_t remaining = len;
                while (remaining) {
                    uint32_t toRead = static_cast<uint32_t>(std::min<uint64_t>(remaining, ioBuf.size()));
                    if (!read_exact(patch, ioBuf.data(), toRead)) throw std::runtime_error("patch short read");
                    
                    out.write(reinterpret_cast<const char*>(ioBuf.data()), toRead);
                    remaining -= toRead;
                    written += toRead;
                }
            } else {
                throw std::runtime_error("unknown command");
            }
        }

        // Final flush and check
        out.flush();
        checkOutStream();

        if (written != expected) throw std::runtime_error("reconstructed size mismatch");
    }
};

namespace fastcdc {

void createSignature(const char* src, const char* sig) {
    OptimizedFastCDC cdc;
    cdc.createSignature(src, sig);
}

void createPatchFromSig(const char* sig, const char* dst, const char* patch) {
    OptimizedFastCDC cdc;
    cdc.createPatchFromSignature(sig, dst, patch);
}
void createPatch(const char* src, const char* dst, const char* patch) {
    OptimizedFastCDC cdc;
    cdc.createPatch(src, dst, patch);
}

void applyPatch(const char* src, const char* patch, const char* out) {
    OptimizedFastCDC cdc;
    cdc.applyPatch(src, patch, out);
}

} // namespace fastcdc