#ifndef BMP_READER_H
#define BMP_READER_H
#include <iostream>
#include "../writer/bmp_writer.h"  // Include the BMP writer header file for the BMPFileHeader and BMPInfoHeader structures
#include <fstream>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <thread>
#include <algorithm>

void readBMP(const std::string& inputFile, const std::string& outputFile, uint_fast64_t binaryLength, const std::string& encryptionKey);
#endif  // BMP_READER_H
