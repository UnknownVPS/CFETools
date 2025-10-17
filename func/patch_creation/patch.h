#pragma once

#include <cstdint>
#include <cstddef>

// Minimal, stable interface for creating/applying patches built by OptimizedFastCDC.
// Link against the .cpp that contains your current implementation.

namespace fastcdc {

// Tunables (optional): expose as constants so other code can align buffers if needed.
constexpr std::size_t kBufferSize = 16u * 1024u * 1024u;  // 16MB
constexpr std::size_t kBloomBits  = 1024u * 1024u;        // bits

// High-level API identical to what main() uses.
void createPatch(const char* srcFile,
                 const char* dstFile,
                 const char* patchFile);

void applyPatch(const char* srcFile,
                const char* patchFile,
                const char* outFile);

// Optional: exception type if you prefer catching a specific error.
// Your .cpp can throw std::runtime_error already; this is here if you want to switch later.
struct PatchError;

} // namespace fastcdc
