#include "create_file.h"
#include "../../utils/progressbar/progressbar.h"
#include "../../utils/bmp/reader/bmp_reader.h"

void create_file(const std::string& bmp_file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(bmp_file_path).filename().stem().string();
    std::string metadata_file = save_path + "/metadata_" + file_name + ".json";

    // Read metadata from JSON file
    Json::Value metadata;
    std::ifstream metadata_stream(metadata_file);
    metadata_stream >> metadata;
    metadata_stream.close();

    std::string original_file_name = metadata["original_file_name"].asString();
    unsigned long long int binary_length = metadata["binary_length"].asInt64();

    // Call the readBMP function
    std::string output_file_path = save_path + "/" + original_file_name;
    readBMP(bmp_file_path, output_file_path, binary_length);

    std::cout << "\nOriginal file reconstructed successfully." << std::endl;
}
