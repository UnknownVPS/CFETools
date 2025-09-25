#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <sodium.h>

namespace fileHasher {

// ================= SHA-256 =================
inline std::string hashFileSHA256(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("File not found");

    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);

    std::vector<char> buf(1024 * 1024); // 1 MB chunks
    while (in) {
        in.read(buf.data(), buf.size());
        std::streamsize r = in.gcount();
        if (r > 0)
            crypto_hash_sha256_update(&state,
                reinterpret_cast<const unsigned char*>(buf.data()), r);
    }

    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256_final(&state, hash);

    // convert to hex string
    static const char hexmap[] = "0123456789abcdef";
    std::string hex(crypto_hash_sha256_BYTES * 2, ' ');
    for (size_t i = 0; i < crypto_hash_sha256_BYTES; i++) {
        hex[2*i]   = hexmap[(hash[i] >> 4) & 0xF];
        hex[2*i+1] = hexmap[hash[i] & 0xF];
    }
    return hex;
}

// ================= CRC32 =================
inline uint32_t& crc32Table() {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
            }
            table[i] = crc;
        }
        initialized = true;
    }
    return table[0]; // returns reference to first element (array is static)
}

inline int crc32_file(const std::string& path) {
    uint32_t* table = &crc32Table();
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("File not found");

    uint32_t crc = 0xFFFFFFFF;
    std::vector<char> buf(1024 * 1024); // 1 MB chunks

    while (in) {
        in.read(buf.data(), buf.size());
        std::streamsize r = in.gcount();
        for (std::streamsize i = 0; i < r; i++) {
            uint8_t byte = static_cast<uint8_t>(buf[i]);
            crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
        }
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace fileHasher