#include "../include/program_flow_utils.hpp"
#include <cstddef>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>  // waitpid() includes
#include <sys/types.h> // waitpid() includes

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
    const char** args;

    cgi_command_struct()
        : cgi_type(NOT_DEFINED_YET), interpreted_language_path(NULL), path_to_program(NULL),
          args(NULL) {}
};

// Can throw exception
int execute_cgi(cgi_command_struct& cgi_command) {

    const char* bin_path = NULL;

    switch (cgi_command.cgi_type) {
        case NOT_DEFINED_YET:
            throw MalformedCGIStruct();
            break;

        case INTERPRETED_LANGUAGE:
            bin_path = cgi_command.interpreted_language_path;
            break;

        case BINARY:
            bin_path = cgi_command.path_to_program;
            break;

        default:
            throw MalformedCGIStruct();
            break;
    }

    if (bin_path == NULL) {
        throw MalformedCGIStruct();
    }

    int file_descriptors[2];
    if (pipe(file_descriptors) == -1) {
        fail_and_exit_with_message(
            -1, "Failed to create a file descriptor to comunicate with a CGI process.");
    }

    dup2()

    int process_id = fork();

    if (process_id == -1) {
        fail_and_exit_with_message(
            -1, std::string("Failed to create a a new process to execute CGI command: ")
                    + std::strerror(errno));
    }

    // int execve(const char *path, char *const _Nullable argv[], char *const _Nullable envp[]);

    if (process_id == 0) {

        close(file_descriptors[0]);

        dup2(file_descriptors[1], STDOUT_FILENO);



        std::vector<const char*> argv_vector;

        argv_vector.push_back(cgi_command.interpreted_language_path);
        for (const char** arg_ptr = cgi_command.args; arg_ptr != NULL; arg_ptr++) {
            argv_vector.push_back(*arg_ptr);
        }
        argv_vector.push_back(NULL);

        if (execve(bin_path, const_cast<char* const*>(&argv_vector[0]), NULL) == -1) {
            fail_and_exit_with_message(
                -1, std::string(
                        "Failed to execute CGI process on an execve call to execute CGI command: ")
                        + std::strerror(errno));
        }
    }

    close(file_descriptors[1]);

    std::string	cgi_output;
    char read_buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(file_descriptors[0], read_buffer, sizeof(read_buffer))) > 0) {
        cgi_output.append(read_buffer, bytes_read);
    }

    close(file_descriptors[0]);

    int	status;
    int exit_code;

    waitpid(process_id, &status, 0);
    if (WIFEXITED(status)) {
	    int exit_code = WEXITSTATUS(status);

    }


    cgi_command.cgi_type

        return 0;
}
