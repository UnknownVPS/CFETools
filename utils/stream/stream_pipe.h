#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>

// Generic streaming interface used to chain operations without temp files.
// ReadFn:  fills buf with up to len bytes, returns actual bytes read (0 = EOF).
// WriteFn: writes len bytes from buf, returns false on write error.
using ReadFn  = std::function<size_t(void* buf, size_t len)>;
using WriteFn = std::function<bool(const void* buf, size_t len)>;

// Convenience: build a ReadFn that reads from a FILE*
inline ReadFn read_fn_from_file(FILE* f) {
    return [f](void* buf, size_t len) -> size_t {
        return fread(buf, 1, len, f);
    };
}

// Convenience: build a WriteFn that writes to a FILE*
inline WriteFn write_fn_to_file(FILE* f) {
    return [f](const void* buf, size_t len) -> bool {
        return fwrite(buf, 1, len, f) == len;
    };
}