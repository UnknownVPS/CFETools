#include "func/img_creation/create_img.h"
#include "func/file_creation/create_file.h"
#include "utils/userinput/user_input.h"

int main(int argc, char* argv[]) {
    string input;
    string file_path;
    string save_path;
    const char* home;

    #ifdef _WIN32
        home = std::getenv("USERPROFILE");
    #else
        home = std::getenv("HOME");
    #endif

    if (home != nullptr) {
        std::filesystem::path tool_dir(home);
        tool_dir /= "CFET-Tools"; // Append "CFET-Tools" to the path

        if (!std::filesystem::exists(tool_dir)) {
            std::cout << "Directory does not exist. Creating now...\n";
            std::filesystem::create_directory(tool_dir);
        }
        save_path = tool_dir.string();
    } else {
        std::cout << "Cannot get home directory.\n";
        return 1;
    }

    if (argc > 1) {
        input = argv[1];
        if (input.substr(0, 6) == "--img=") {
            file_path = input.substr(6);
            input = "img";
        } else if (input.substr(0, 7) == "--file=") {
            file_path = input.substr(7);
            input = "file";
        }
    }

    if (input.empty()) {
        input = get_user_input();
    }

    if (input == "img") {
        if (file_path.empty()) {
            cout << "Please enter the file to be encoded path: ";
            getline(cin, file_path);
        }
        create_img(file_path, save_path);
    } else if (input == "file") {
        if (file_path.empty()) {
            cout << "Please enter the image to be decoded path: ";
            getline(cin, file_path);
        }
        create_file(file_path, save_path);
    } else {
        cout << "Invalid Input!" << endl;
    }

    return 0;
}
