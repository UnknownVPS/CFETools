#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include "../../utils/logger/logger.h"
#include "../../utils/stream/stream_pipe.h"

namespace fs = std::filesystem;

// ── Original file-based API (unchanged) ──────────────────────────
bool pack_folder(const std::string& folderPath, const std::string& packedFilePath);
bool unpack_packed_file(const std::string& packedFilePath, const std::string& outputFolderPath);

// ── Streaming API ─────────────────────────────────────────────────

/**
 * Pack a folder, writing the .cfup byte-stream to `dst` instead of a file.
 * Used for zero-copy pack → compress chaining.
 */
bool pack_folder_stream(const std::string& folderPath, WriteFn dst);

/**
 * Unpack from a ReadFn byte-stream into outputFolderPath.
 * Used for zero-copy decompress → unpack chaining.
 *
 * peek_header: first 8 bytes already read from the stream (may be all zeros
 * if nothing has been consumed yet, in which case they are read via `src`).
 * has_peek: set true only when 8 bytes have genuinely been pre-consumed.
 */
bool unpack_stream(ReadFn src, const std::string& outputFolderPath);

/**
 * Probe the first 4 bytes of a file to check if it looks like a .cfup archive
 * (i.e. a raw little-endian uint32 file-count followed by valid path data).
 * This is a heuristic — not a magic-number check — but sufficient for auto-detect.
 */
bool looks_like_cfup(const std::string& filePath);

/**
 * Same probe but from raw bytes already read from a stream.
 * bytes must be at least 4 bytes.
 */
bool looks_like_cfup_header(const uint8_t* bytes, size_t len);