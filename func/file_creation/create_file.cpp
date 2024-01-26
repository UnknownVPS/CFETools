#include "create_file.h"
#include "../../utils/progressbar/progressbar.h"

void create_file(const string& file_path, const string& save_path) {
    string file_name = filesystem::path(file_path).filename().stem().string();
    string metadata_file = filesystem::path(file_path).parent_path().string() + "/metadata_" + file_name + ".json";
    string output_image_file = file_path;

    // Read metadata from JSON file
    ifstream metadata_stream(metadata_file);
    Json::Value metadata;
    metadata_stream >> metadata;
    metadata_stream.close();

    string output_video_file = save_path + "/" +  metadata["original_file_name"].asString();
    int length = metadata["binary_length"].asInt();

    Mat image = imread(output_image_file, IMREAD_GRAYSCALE);

    cout << "Reconstructing binary:" << endl;
    deque<int> binary;
    vector<char> buffer;
    int count = 0; // Add a counter to keep track of the number of pixels processed
    auto start_time = std::chrono::steady_clock::now();
    for(int i = 0; i < image.rows; i++) {
        for(int j = 0; j < image.cols; j++) {
            binary.push_back(image.at<uchar>(i, j) > 128 ? 1 : 0);
            if(binary.size() == 8) {
                char byte = 0;
                for(int k = 0; k < 8; k++) {
                    byte = (byte << 1) | binary[k];
                }
                buffer.push_back(byte);
                binary.clear();
            }
            count++; // Increment the counter
            if(count == length) break; // If the counter equals the length specified in the metadata, break the loop
        }
        if(count == length) break; // Break the outer loop as well
        print_progress(float(i) / image.rows, start_time, image.rows, "Reconstructing File");
    }
    string buffer_str(buffer.begin(), buffer.end());
    if(!binary.empty()) {
        char byte = 0;
        for(int i = 0; i < binary.size(); i++) {
            byte = (byte << 1) | binary[i];
        }
        buffer.push_back(byte);
    }

    ofstream output_video(output_video_file, ios::binary);
    copy(buffer.begin(), buffer.end(), ostreambuf_iterator<char>(output_video));
    output_video.close();

    cout << "\nOriginal file reconstructed successfully." << endl;
}
