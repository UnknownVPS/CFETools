#ifndef BMP_READER_H
#define BMP_READER_H

#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <thread>
#include <algorithm>
#include <string>
#include "../writer/bmp_writer.h"  // Include writer header for struct parity

void readBMP(const std::string& filename, const std::string& outputFilename, 
             uint_fast64_t binaryLength, const std::string& encryptionKey);

void readBMPNoEncrypt(const std::string& filename, const std::string& outputFilename, 
                     uint_fast64_t binaryLength);

#endif  // BMP_READER_H