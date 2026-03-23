#pragma once

#include <string>
#include "../stream/stream_pipe.h"
#include "../logger/logger.h"

/**
 * Memory-efficient streaming file decompression function with auto-detection.
 * Handles files of any size by processing in chunks.
 * 
 * @param input_file Path to compressed input file.
 * @param output_file Path for decompressed output file.
 * @return true if decompression successful, false otherwise.
 */
bool decompressFile(const std::string& input_file, const std::string& output_file);

/**
 * Check if file is compressed by attempting format detection.
 * 
 * @param filename Path to file.
 * @return true if file is a recognized compressed format, false otherwise.
 */
bool isCompressedFile(const std::string& filename);

/**
 * Get compression format of a file as string.
 * 
 * @param filename Path to file.
 * @return Format name as a string.
 */
std::string getFileCompressionFormat(const std::string& filename);

/**
 * Streaming decompress: read compressed bytes from src, write plain bytes to dst.
 * Returns false if the format is unrecognised or decompression fails.
 * peek_header must contain the first 8 bytes already read from the stream
 * so the format can be detected without seeking.
 */
bool decompressStream(const uint8_t peek_header[8],
                      ReadFn        src,
                      WriteFn       dst);

enum class CompressionFormat { UNKNOWN, LZ4F, ZSTD, LZMA2_XZ };
CompressionFormat detectFormatFromHeader(const uint8_t header[8]);