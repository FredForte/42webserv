#ifndef PROGRAM_FLOW_UTILS_HPP
#define PROGRAM_FLOW_UTILS_HPP

#include <iostream>

void fail_and_exit(int error_code);
void fail_and_exit_with_message(int error_code, const std::string& message);

#endif
