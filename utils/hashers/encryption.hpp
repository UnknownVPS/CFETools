#pragma once
#include <cstdint>
#include <sodium.h>
#include <cstring>
#include <stdexcept>

namespace encryption {

// AEAD ChaCha20-Poly1305
constexpr size_t AEAD_NONCE_SIZE = crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
constexpr size_t AEAD_KEY_SIZE   = crypto_aead_chacha20poly1305_ietf_KEYBYTES;
constexpr size_t AEAD_MAC_SIZE   = crypto_aead_chacha20poly1305_ietf_ABYTES;

inline void aead_chacha20poly1305_encrypt(const uint8_t* input, size_t len,
                                          uint8_t* output, const uint8_t* key,
                                          uint64_t nonce_counter) {
    uint8_t nonce[AEAD_NONCE_SIZE] = {0};
    for (int i = 0; i < 8; i++) {
        nonce[i] = (nonce_counter >> (i * 8)) & 0xFF;
    }
    unsigned long long out_len = 0;
    crypto_aead_chacha20poly1305_ietf_encrypt(output, &out_len,
                                              input, len,
                                              nullptr, 0,
                                              nullptr, nonce, key);
}

// XSalsa20 stream cipher
constexpr size_t XSALSA20_NONCE_SIZE = crypto_stream_xsalsa20_NONCEBYTES;
constexpr size_t XSALSA20_KEY_SIZE   = crypto_stream_xsalsa20_KEYBYTES;

inline void xsalsa20_xor(const uint8_t* input, size_t len,
                         uint8_t* output, const uint8_t* key,
                         uint64_t nonce_counter) {
    uint8_t nonce[XSALSA20_NONCE_SIZE] = {0};
    memcpy(nonce, &nonce_counter, sizeof(nonce_counter));
    crypto_stream_xsalsa20_xor(output, input, len, nonce, key);
}

// ChaCha20 stream cipher (no authentication)
constexpr size_t CHACHA20_NONCE_SIZE = crypto_stream_chacha20_ietf_NONCEBYTES;
constexpr size_t CHACHA20_KEY_SIZE   = crypto_stream_chacha20_ietf_KEYBYTES;

inline void chacha20_xor(const uint8_t* input, size_t len,
                         uint8_t* output, const uint8_t* key,
                         uint64_t nonce_counter) {
    uint8_t nonce[CHACHA20_NONCE_SIZE] = {0};
    for (int i = 0; i < 8; i++) {
        nonce[i] = (nonce_counter >> (i * 8)) & 0xFF;
    }
    crypto_stream_chacha20_ietf_xor(output, input, len, nonce, key);
}

} // namespace encryption