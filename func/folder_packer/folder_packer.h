#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include "../../utils/logger/logger.h"
namespace fs = std::filesystem;

bool pack_folder_toc(const std::string& folderPath, const std::string& packedFilePath);

bool unpack_packed_file_toc(const std::string& packedFilePath, const std::string& outputFolderPath);