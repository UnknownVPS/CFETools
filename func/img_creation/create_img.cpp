#include "create_img.h"
#include "../../utils/progressbar/progressbar.h"
#include "../../utils/bmp/writer/bmp_writer.h"  // Include the BMP writer header file

void create_img(const std::string& file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    std::string metadata_file = save_path + "/metadata_" + file_name + ".json";

    // Open the input file in binary mode
    std::ifstream input_file(file_path, std::ios::binary);

    // Calculate the binary length
    input_file.seekg(0, std::ios::end);
    long long int binary_length = input_file.tellg() * 8; // Each byte is 8 bits
    input_file.seekg(0, std::ios::beg);

    input_file.close();

    // Create JSON object
    Json::Value metadata;
    metadata["original_file_name"] = std::filesystem::path(file_path).filename().string();
    metadata["binary_length"] = binary_length;

    // Write JSON metadata to file
    std::ofstream metadata_file_stream(metadata_file);
    metadata_file_stream << metadata;
    metadata_file_stream.close();

    std::cout << "\nImage metadata written to " << metadata_file << "." << std::endl;

    // Call the writeBMP function
    writeBMP(save_path + "/" + file_name + ".bmp", file_path);
}
