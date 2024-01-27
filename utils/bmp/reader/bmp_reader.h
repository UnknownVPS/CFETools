#ifndef BMP_READER_H
#define BMP_READER_H

#include <vector>
#include <string>
#include "../writer/bmp_writer.h"  // Include the BMP writer header file for the BMPFileHeader and BMPInfoHeader structures

std::vector<std::vector<bool>> readBMP(const std::string& filename);

#endif  // BMP_READER_H
