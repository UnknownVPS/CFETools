#pragma once

#include <string>

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
 * Returns the algorithm name for a given compression level.
 * 
 * @param compression_lvl Compression level (1-21).
 * @return Algorithm name as a string.
 */