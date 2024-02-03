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
    std::string binary_file_path = save_path + "/" + file_name + "_recons.bin";
    readBMP(bmp_file_path, binary_file_path);

    // Open the binary file
    std::ifstream binary_file(binary_file_path);

    std::cout << "Reconstructing original file:" << std::endl;
    std::ofstream output_file(save_path + "/" + original_file_name, std::ios::binary);

    std::string binary_string;
    char byte;
    int bit_count = 0;
    unsigned long long int total_bits_processed = 0;
    while (binary_file.get(byte) && total_bits_processed < binary_length) {
        binary_string += byte;
        if (++bit_count == 8) {
            std::bitset<8> bits(binary_string);
            char byte = static_cast<char>(bits.to_ulong());
            output_file << byte;
            binary_string.clear();
            bit_count = 0;
            total_bits_processed += 8;
        }
    }

    output_file.close();
    binary_file.close();

    if (remove(binary_file_path.c_str()) != 0) {
        perror("Error deleting temporary files");
    } else {
        puts("Temporary files successfully deleted");
    }
    std::cout << "\nOriginal file reconstructed successfully." << std::endl;
}
