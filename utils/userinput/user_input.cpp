#include "user_input.h"
#include "../logger/logger.h"

string Input::ask(string question) {
    string input;
    cerr << "[QUESTION] " << question;
    getline(cin, input);
    Logger::Log(LOG_DEBUG, "Input received: " + input);
    return input;
};