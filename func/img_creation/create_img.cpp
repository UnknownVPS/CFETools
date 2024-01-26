#include "create_img.h"
#include "../../utils/progressbar/progressbar.h"

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

    int width = ceil(sqrt(binary_str.length()));
    int height = width;

    Mat image(height, width, CV_8UC1);
    cout << endl;
    auto start_time = std::chrono::steady_clock::now();
    for(int i = 0; i < height; i++) {
        for(int j = 0; j < width; j++) {
            int index = i * width + j;
            if(index < binary_str.length()) {
                image.at<uchar>(i, j) = binary_str[index] == '1' ? 255 : 0;
            }
        }
        print_progress(float(i) / height, start_time, height, "Image Creation");
    }
    imwrite(output_image_file, image);

    Py_Initialize();
    std::string python_code =
        "from PIL import Image\n"
        "import os\n"
        "Image.MAX_IMAGE_PIXELS = None\n"
        "with Image.open('" + output_image_file + "') as img:\n"
        "    img = img.convert('1')\n"
        "    img.save('" + output_image_file + "')\n";
    PyRun_SimpleString(python_code.c_str());

    Py_Finalize();
    cout << "\nImage creation complete." << endl;

    // Create JSON object
    Json::Value metadata;
    metadata["original_file_name"] = filesystem::path(file_path).filename().string();
    metadata["binary_length"] = binary_str.length();

    // Write JSON metadata to file
    ofstream metadata_file_stream(metadata_file);
    metadata_file_stream << metadata;
    metadata_file_stream.close();

    cout << "Image metadata written to " << metadata_file << "." << endl;
}