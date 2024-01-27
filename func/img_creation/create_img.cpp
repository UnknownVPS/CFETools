#include "create_img.h"
#include "../../utils/progressbar/progressbar.h"
#include "../../utils/bmp/writer/bmp_writer.h"  // Include the BMP writer header file

void create_img(const string& file_path, const string& save_path) {
    string file_name = filesystem::path(file_path).filename().stem().string();
    string output_image_file = save_path + "/" + file_name + ".bmp";
    string metadata_file = save_path + "/metadata_" + file_name + ".json";

    // Read the file into a string
    std::ifstream file(file_path, std::ios::binary);
    std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Convert the string to binary
    std::string binary_str;
    auto start_timeB = std::chrono::steady_clock::now();
    int i = 0;
    for (char& c : buffer) {
        binary_str += std::bitset<8>(c).to_string();
        if (i % 1000 == 0) {
            print_progress((float)i / buffer.size(), start_timeB, buffer.size(), "Converting to binary");
        }
        i++;
    }

    int side = ceil(sqrt(binary_str.length()));

    // Create a 2D vector of bools for the pixel data
    std::vector<std::vector<bool>> pixels(side, std::vector<bool>(side, false));
    for(int i = 0; i < side; i++) {
        for(int j = 0; j < side; j++) {
            int index = i * side + j;
            if(index < binary_str.length()) {
                pixels[i][j] = binary_str[index] == '1' ? true : false;
            }
        }
        print_progress(float(i) / side, start_timeB, side, "Image Creation");
    }

    // Write the image to a BMP file
    writeBMP(output_image_file, pixels);

    // Create JSON object
    Json::Value metadata;
    metadata["original_file_name"] = filesystem::path(file_path).filename().string();
    metadata["binary_length"] = ceil(binary_str.length());

    // Write JSON metadata to file
    ofstream metadata_file_stream(metadata_file);
    metadata_file_stream << metadata;
    metadata_file_stream.close();

    cout << "Image metadata written to " << metadata_file << "." << endl;
}
