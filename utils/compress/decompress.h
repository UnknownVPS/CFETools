#pragma once

#include <string>

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