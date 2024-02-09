#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"

void create_file(const std::string& bmp_file_path, const std::string& save_path) {
    Data data = read_json(save_path + "/" + std::filesystem::path(bmp_file_path).filename().stem().string() + ".mtd");
    uint_fast64_t binary_length = data.binary_length;
    std::string original_filename = data.original_filename;
    std::cout << original_filename << binary_length << std::endl;
    readBMP(bmp_file_path, save_path + "/" + original_filename, binary_length);

    std::cout << "\nOriginal file reconstructed successfully." << std::endl;
}
