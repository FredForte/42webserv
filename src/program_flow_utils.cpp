#include <iostream>

void fail_and_exit(int error_code) {
    std::exit(error_code);
}

void fail_and_exit_with_message(int error_code, const std::string& message) {
    std::cerr << "Error: " << message << std::endl;
    fail_and_exit(error_code);
}
