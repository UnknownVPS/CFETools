#include "user_input.h"

string get_user_input() {
    string input;
    cout << "Please enter a command (img/file): ";
    getline(cin, input);
    return input;
}
