#ifndef BMP_READER_H
#define BMP_READER_H
#include <iostream>
#include "../writer/bmp_writer.h"  // Include the BMP writer header file for the BMPFileHeader and BMPInfoHeader structures
#include <fstream>
#include <cmath>

void readBMP(const std::string& filename, const std::string& outputFilename, unsigned long long int binary_length);
#endif  // BMP_READER_H
