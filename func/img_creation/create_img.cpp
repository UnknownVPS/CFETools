#include "create_img.h"
#include "../../utils/bmp/writer/bmp_writer.h"  // Include the BMP writer header file
#include "../../utils/json/json.h"
void create_img(const std::string& file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    std::ifstream inputFile(file_path, std::ios::binary);
    inputFile.seekg(0, std::ios::end);
    int size = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);
    Data data;
    data.original_filename = std::filesystem::path(file_path).filename().string();
    data.binary_length = (size * 8);
    write_json(save_path + '/' + file_name + ".mtd", data);
    // Call the writeBMP function
    writeBMP(save_path + "/" + file_name + ".bmp", file_path);
    std::cout << "Image written successfully" << std::endl;
}
