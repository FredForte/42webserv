#include "../include/cgi.hpp"
#include "../include/parser/ConfigTypes.hpp"
#include "../include/parser/HttpRequest.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/socket_utils.hpp"
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Can throw exception
int execute_cgi(cgi_instance_struct& cgi_instance, const std::string& request_body) {

    cgi_command_struct& cgi_command = cgi_instance.cgi_command;

    switch (cgi_command.cgi_type) {
        case BINARY:
            if (cgi_command.path_to_program.empty())
                throw MalformedCGIStruct();
            break;

        case INTERPRETED_LANGUAGE:
            if (cgi_command.interpreted_language_path == NULL || cgi_command.path_to_program.empty())
                throw MalformedCGIStruct();
            break;

        default:
            throw MalformedCGIStruct();
    }

    // request.body is the complete body, we need to make it into a file so the
    // stdin can receive it and read it fully, because a pipe kernel buffer is 64KB max by default.
    int stdin_fd = -1;

    if (request_body.empty()) {
        stdin_fd = open("/dev/null", O_RDONLY);
        if (stdin_fd == -1) {
            throw std::runtime_error(std::string("Failed to open /dev/null for CGI stdin: ")
                                     + std::strerror(errno));
        }
    }

    // create a file name with internal counter.
    // O_EXCL makes open fail if the name already exists,
    // retry with the next number on collision.
    static unsigned long counter = 0;
    std::string path;
    for (int tries = 0; tries < 100 && stdin_fd == -1; ++tries) {
        std::stringstream ss;
        ss << "/tmp/webserv_cgi_stdin_" << counter++;
        path = ss.str();
        stdin_fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    }

    if (stdin_fd == -1) {
        throw std::runtime_error(std::string("Failed to create CGI stdin temp file: ")
                                 + std::strerror(errno));
    }
    unlink(path.c_str()); // the open fd keeps it alive; removed once all fds close

    ssize_t total_written = 0;
    while (total_written < request_body.size()) {
        ssize_t written_on_loop = write(stdin_fd, request_body.data() + total_written,
                                        request_body.size() - total_written);

        if (written_on_loop == -1) {
            close(stdin_fd);
            throw std::runtime_error(std::string("Failed to write CGI stdin temp file: ")
                                     + std::strerror(errno));
        }

        total_written += written_on_loop;
    }

    lseek(stdin_fd, 0, SEEK_SET);

    int file_descriptors[2];
    if (pipe(file_descriptors) == -1) {
        close(stdin_fd);
        throw std::runtime_error(std::string("Failed to create CGI pipe: ") + std::strerror(errno));
    }

    int process_id = fork();
    if (process_id == -1) {
        close(stdin_fd);
        close(file_descriptors[0]);
        close(file_descriptors[1]);
        throw std::runtime_error(std::string("Failed to fork for CGI: ") + std::strerror(errno));
    }

    if (process_id == 0) {

        // wire the child's stdin from the staged file and stdout to the pipe,
        // then drop the now-duplicated fds before exec.
        dup2(stdin_fd, STDIN_FILENO);
        dup2(file_descriptors[1], STDOUT_FILENO);
        close(stdin_fd);
        close(file_descriptors[0]);
        close(file_descriptors[1]);

		// run the cgi program from its own directory, so it can open/access files by
		// relative path. chdir into the scripts dir, if valid,
		// reference it by basename so execv uses it as cwd.
        std::string script_ref = cgi_command.path_to_program;
        std::string::size_type slash = cgi_command.path_to_program.rfind('/');
        if (slash != std::string::npos) {
            std::string script_dir = cgi_command.path_to_program.substr(0, slash);
            if (chdir(script_dir.c_str()) == -1) {
                std::cerr << "CGI chdir failed: " << std::strerror(errno) << std::endl;
                _exit(1);
            }
            script_ref = cgi_command.path_to_program.substr(slash + 1);
        }

        std::vector<const char*> argv_vector;
        std::vector<std::string>::iterator it;
        const char* exec_path;

        if (cgi_command.cgi_type == INTERPRETED_LANGUAGE) {
            exec_path = cgi_command.interpreted_language_path;
            argv_vector.push_back(cgi_command.interpreted_language_path);
            argv_vector.push_back(script_ref.c_str());
        } else { // BINARY: the script itself is the program, argv[0]
            exec_path = script_ref.c_str();
            argv_vector.push_back(script_ref.c_str());
        }

        for (it = cgi_command.args.begin(); it != cgi_command.args.end(); it++) {
            argv_vector.push_back(it->c_str());
        }
        argv_vector.push_back(NULL);

        std::vector<const char*> envp_vector;
        for (it = cgi_command.envp.begin(); it != cgi_command.envp.end(); it++) {
            envp_vector.push_back(it->c_str());
        }
        envp_vector.push_back(NULL);

		// cant throw on execve, it would become another webserv instance
		// _exit(1) to get rid of all data and prevent unwanted bytes from
		// being written to buffer, also prevent double executions that
		// can be cause by std::exit()
        if (execve(exec_path, const_cast<char* const*>(&argv_vector[0]),
                   const_cast<char* const*>(&envp_vector[0]))
            == -1) {
            std::cerr << "Failed to execve CGI process: " << std::strerror(errno) << std::endl;
            _exit(1);
        }
    }

    close(file_descriptors[1]);
    close(stdin_fd);

    cgi_instance.cgi_pid = process_id;

    make_fd_non_blocking(file_descriptors[0]);

    epoll_event event_settings;
    event_settings.events = EPOLLIN;
    event_settings.data.fd = file_descriptors[0];

    if (epoll_ctl(cgi_instance.epoll_instance, EPOLL_CTL_ADD, file_descriptors[0], &event_settings)
        == -1) {
        close(file_descriptors[0]);
        // abandoning this request, so the child has no reader, kill it and
        // reap it now with SIGKILL, immediate, so it can't leak or zombie.
        kill(process_id, SIGKILL);
        waitpid(process_id, NULL, 0);
        throw std::runtime_error(std::string("Failed to register CGI fd with epoll: ")
                                 + std::strerror(errno));
    }

    cgi_instance.cgi_fd = file_descriptors[0];

    return cgi_instance.cgi_fd;
}
