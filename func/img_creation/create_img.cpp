#include "create_img.h"
#include "../../utils/progressbar/progressbar.h"
#include "../../utils/bmp/writer/bmp_writer.h"  // Include the BMP writer header file

void create_img(const std::string& file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    std::string output_binary_file = save_path + "/" + file_name + ".bin";
    std::string metadata_file = save_path + "/metadata_" + file_name + ".json";

    // Open the input file in binary mode
    std::ifstream input_file(file_path, std::ios::binary);

    // Open the output file in binary mode
    std::ofstream output_file(output_binary_file, std::ios::binary);
    char c;
    long long int i = 0;
    while (input_file.get(c)) {
        // Convert the character to binary and write it to the output file
        output_file << std::bitset<8>(c).to_string();
        i += 8;
    }

    input_file.close();
    output_file.close();

    std::cout << "Binary data written to " << output_binary_file << "." << std::endl;

    // Create JSON object
    Json::Value metadata;
    metadata["original_file_name"] = std::filesystem::path(file_path).filename().string();
    metadata["binary_length"] = i;

    // Write JSON metadata to file
    std::ofstream metadata_file_stream(metadata_file);
    metadata_file_stream << metadata;
    metadata_file_stream.close();

    std::cout << "\nImage metadata written to " << metadata_file << "." << std::endl;

    // Call the writeBMP function
    writeBMP(save_path + "/" + file_name + ".bmp", output_binary_file);
    if (remove(output_binary_file.c_str()) != 0) {
        perror("\nError deleting temporary files");
    } else {
        puts("\nTemporary files successfully deleted");
    }
}