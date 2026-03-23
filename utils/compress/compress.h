#pragma once

#include <string>
#include "../logger/logger.h"
#include "../stream/stream_pipe.h"
/**
 * Compresses a file using a memory-efficient streaming algorithm.
 * 
 * @param input_file Path to the input file to compress.
 * @param output_file Path for the compressed output file.
 * @param compression_lvl Compression level (1-21).
 * @return true if compression was successful, false otherwise.
 */
bool compressFile(const std::string& input_file, const std::string& output_file, int compression_lvl);

/**
 * Streaming compress: read from src, write compressed bytes to dst.
 * input_size is used only for progress/ratio logging — pass 0 if unknown.
 */
bool compressStream(ReadFn  src,
                    WriteFn dst,
                    int     compression_lvl,
                    size_t  input_size = 0);
 
/**
 * Returns the algorithm name for a given compression level.
 * 
 * @param compression_lvl Compression level (1-21).
 * @return Algorithm name as a string.
 */
std::string getAlgorithmName(int compression_lvl);