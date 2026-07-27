#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include "./cgi.hpp"
#include "./parser/ConfigTypes.hpp"
#include "./parser/HttpRequest.hpp"
#include "./parser/HttpRequestParser.hpp"
#include <map>
#include <string>

struct client_connection_struct {
    int client_fd;
    std::string input_buffer;
    RequestFraming framing; // saved state of the receivng end.
    std::string output_buffer;
    size_t output_sent; // controls the ammount already sent as an iterator.
    bool ready_to_respond;
    bool close_after_response;
    cgi_instance_struct cgi_instance;
    HttpRequest request_data;
    ServerConfig* ServerConfig_ptr;
};

#endif
