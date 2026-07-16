#include "../include/cgi.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/socket_utils.hpp"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/epoll.h>
#include <sys/types.h> // waitpid() includes
#include <sys/wait.h>  // waitpid() includes
#include <unistd.h>
#include <vector>
#include <cerrno>

// Can throw exception
int execute_cgi(cgi_instance_struct& cgi_instance) {

    cgi_command_struct& cgi_command = cgi_instance.cgi_command;

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

        std::vector<std::string>::iterator it;

        if (cgi_command.cgi_type == INTERPRETED_LANGUAGE) {
            argv_vector.push_back(cgi_command.interpreted_language_path);
            argv_vector.push_back(cgi_command.path_to_program);
        }

        for (it = cgi_command.args.begin(); it != cgi_command.args.end(); it++) {
            argv_vector.push_back(it->c_str());
        }
        argv_vector.push_back(NULL);

        // for (long unsigned int i = 0; i < argv_vector.size(); i++) {
        //     std::cout << argv_vector[i] << std::endl;
        // }

        if (execve(bin_path, const_cast<char* const*>(&argv_vector[0]), NULL) == -1) {
            fail_and_exit_with_message(
                -1, std::string(
                        "Failed to execute CGI process on an execve call to execute CGI command: ")
                        + std::strerror(errno));
        }
    }

    close(file_descriptors[1]);

    make_fd_non_blocking(file_descriptors[0]);

    epoll_event event_settings;
    event_settings.events = EPOLLIN;
    event_settings.data.fd = file_descriptors[0];

    if (epoll_ctl(cgi_instance.epoll_instance, EPOLL_CTL_ADD, file_descriptors[0], &event_settings)
        == -1) {
        fail_and_exit_with_message(1, std::string("Failed to add CGI socket: ")
                                          + std::strerror(errno));
    }

    cgi_instance.cgi_fd = file_descriptors[0];

    return cgi_instance.cgi_fd;
}

// old code:

// close(file_descriptors[1]);

// std::string cgi_output;
// char read_buffer[4096];
// ssize_t bytes_read;

// while ((bytes_read = read(file_descriptors[0], read_buffer, sizeof(read_buffer))) > 0) {
//     cgi_output.append(read_buffer, bytes_read);
// }

// close(file_descriptors[0]);

// int status;
// int exit_code;

// waitpid(process_id, &status, 0);
// if (WIFEXITED(status)) {
//     int exit_code = WEXITSTATUS(status);
// }

// cgi_command.cgi_type

// return 0;
