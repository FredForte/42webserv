#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include "./cgi.hpp"
#include "./parser/ConfigTypes.hpp"
#include "./parser/HttpRequest.hpp"
#include <map>
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
    bool close_after_response;
    client_connection_enum client_connection_type;
    cgi_instance_struct cgi_instance;
    HttpRequest request_data;
    std::string cookie_id;
    std::map<std::string, std::string> cookie_data;
    ServerConfig* ServerConfig_ptr;
};

#endif
