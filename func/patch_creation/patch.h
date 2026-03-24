#pragma once

#include <cstdint>
#include <cstddef>

// Minimal, stable interface for creating/applying patches built by OptimizedFastCDC.
// Link against the .cpp that contains your current implementation.

namespace fastcdc {

// Tunables (optional): expose as constants so other code can align buffers if needed.
constexpr std::size_t kBufferSize = 16u * 1024u * 1024u;  // 16MB
constexpr std::size_t kBloomBits  = 1024u * 1024u;        // bits

// --- 128-bit Implementation API ---
void createPatch(const char* srcFile,
                 const char* dstFile,
                 const char* patchFile);

void applyPatch(const char* srcFile,
                const char* patchFile,
                const char* outFile);

void createSignature(const char* src, const char* sig);

void createPatchFromSig(const char* sig, const char* dst, const char* patch);

// --- 64-bit Implementation API ---
// These use 64-bit hashes for smaller signatures and faster processing 
// when the collision resistance of 128-bit is not required.
void createPatch64(const char* srcFile,
                   const char* dstFile,
                   const char* patchFile);

void applyPatch64(const char* srcFile,
                  const char* patchFile,
                  const char* outFile);

void createSignature64(const char* src, const char* sig);

void createPatchFromSig64(const char* sig, const char* dst, const char* patch);

// Optional: exception type if you prefer catching a specific error.
// Your .cpp can throw std::runtime_error already; this is here if you want to switch later.
struct PatchError;

} // namespace fastcdc