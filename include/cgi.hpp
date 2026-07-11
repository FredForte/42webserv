#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <vector>

class MalformedCGIStruct : public std::exception {
public:
    virtual const char* what() const throw() {
        return ("Malformed CGI struct");
    }
};

enum cgi_type_enum {
    NOT_DEFINED_YET,
    BINARY,
    INTERPRETED_LANGUAGE,
};

struct cgi_command_struct {
    cgi_type_enum cgi_type;
    const char* interpreted_language_path;
    const char* path_to_program;
    std::vector<std::string> args;

    cgi_command_struct()
        : cgi_type(NOT_DEFINED_YET), interpreted_language_path(NULL), path_to_program(NULL),
          args(NULL) {}
};

struct cgi_instance_struct {
    int client_fd;
    int cgi_fd;
    cgi_command_struct* cgi_command;
    std::string cgi_response;
    int cgi_exit_code;
    int epoll_instance;
};

int execute_cgi(cgi_instance_struct& cgi_instance);

#endif
