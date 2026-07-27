#ifndef CGI_HPP
#define CGI_HPP

#include <ctime>
#include <string>
#include <sys/types.h>
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
    const char* interpreted_language_path; // points into the config (long-lived)
    std::string path_to_program;           // owned, so it can't dangle after setup
    std::vector<std::string> args;
    std::vector<std::string> envp;

    cgi_command_struct() : cgi_type(NOT_DEFINED_YET), interpreted_language_path(NULL) {}
};

// cgi control structure for each connection.
// reset by `resetCgiInstance` after every use to prevent fd missmatches and
// recycled fds being used for the wrong connections.
struct cgi_instance_struct {
    int client_fd;
    int cgi_fd;             // read end of the child's stdout pipe, -1 when idle
    pid_t cgi_pid;          // fork()'s child pid, -1 once reaped
    time_t start_time;      // when the child was launched
    size_t timeout_seconds; // max run time (from the location's cgi_timeout), 0 = no limit
    bool stdout_closed;     // pipe hit EOF; only the child's exit status is still pending
    cgi_command_struct cgi_command;
    std::string cgi_response;
    int cgi_exit_code;
    int epoll_instance;

    cgi_instance_struct()
        : client_fd(-1), cgi_fd(-1), cgi_pid(-1), start_time(0), timeout_seconds(0),
          stdout_closed(false), cgi_exit_code(0), epoll_instance(0) {};
};

int execute_cgi(cgi_instance_struct& cgi_instance, const std::string& request_body);

#endif
