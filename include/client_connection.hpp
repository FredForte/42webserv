#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include "./parser/HttpRequest.hpp"
#include "cgi.hpp"
#include <string>

enum client_connection_enum {
    STANDARD,
    CGI,
};

struct client_connection_struct {
    int client_fd;
    std::string input_buffer;
    std::string output_buffer;
    bool ready_to_respond;
    client_connection_enum client_connection_type;
    cgi_instance_struct cgi_instance;
    HttpRequest request_data;
    ServerConfig* ServerConfig_ptr;
};

#endif
