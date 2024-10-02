#ifndef BMP_READER_H
#define BMP_READER_H
#include <iostream>
#include "../writer/bmp_writer.h"  // Include the BMP writer header file for the BMPFileHeader and BMPInfoHeader structures
#include <fstream>
#include <cmath>
#include <cstdint>

void readBMP(const std::string& filename, const std::string& outputFilename, uint_fast64_t binaryLength, const std::string& encryptionKey);
#endif  // BMP_READER_H
