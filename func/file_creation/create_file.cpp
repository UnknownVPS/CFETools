#include "create_file.h"
#include "../../utils/progressbar/progressbar.h"
#include "../../utils/bmp/reader/bmp_reader.h"  // Include the BMP reader header file

void create_file(const std::string& file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    std::string metadata_file = std::filesystem::path(file_path).parent_path().string() + "/metadata_" + file_name + ".json";
    std::string output_image_file = file_path;
    Json::Value metadata;
    try {
        // Read metadata from JSON file
        std::ifstream metadata_stream(metadata_file);
        metadata_stream >> metadata;
        metadata_stream.close();
    } catch (const Json::RuntimeError& e) {
        std::cerr << "Caught RuntimeError: " << e.what() << std::endl;
        std::cerr << "Failed to parse the JSON file: " << metadata_file << std::endl;
    }

    std::cout << metadata["original_file_name"] << std::endl;
    std::string output_video_file = save_path + "/" + metadata["original_file_name"].asString();
    int length = metadata["binary_length"].asInt();

    // Read BMP file using custom BMP reader
    std::vector<std::vector<bool>> bmpPixels = readBMP(output_image_file);

    std::cout << "Reconstructing binary:" << std::endl;
    std::deque<int> binary;
    std::vector<char> buffer;
    int count = 0; // Add a counter to keep track of the number of pixels processed
    auto start_time = std::chrono::steady_clock::now();
    for (int i = 0; i < bmpPixels.size(); i++) {
        for (int j = 0; j < bmpPixels[i].size(); j++) {
            int index = i * bmpPixels[i].size() + j;
            if(index < length) {
                binary.push_back(bmpPixels[i][j]); // Push the pixel value into the binary deque
                if (binary.size() == 8) {
                    char byte = 0;
                    for (int k = 0; k < 8; k++) {
                        byte = (byte << 1) | binary[k];
                    }
                    buffer.push_back(byte);
                    binary.clear();
                }
            }
            count++; // Increment the counter
            if (count == length)
                break; // If the counter equals the length specified in the metadata, break the loop
        }
        if (count == length)
            break; // Break the outer loop as well
        print_progress(float(i) / bmpPixels.size(), start_time, bmpPixels.size(), "Reconstructing File");
    }
    if (!binary.empty()) {
        char byte = 0;
        for (int i = 0; i < binary.size(); i++) {
            byte = (byte << 1) | binary[i];
        }
        buffer.push_back(byte);
    }

    std::ofstream output_video(output_video_file, std::ios::binary);
    std::copy(buffer.begin(), buffer.end(), std::ostreambuf_iterator<char>(output_video));
    output_video.close();

    std::cout << "\nOriginal file reconstructed successfully." << std::endl;
}
