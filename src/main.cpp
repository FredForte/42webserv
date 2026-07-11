#include "../include/client_connection.hpp"
#include "../include/program_flow_utils.hpp"
#include "./parser/HttpRequest.hpp"
#include "./parser/HttpRequestParser.hpp"
#include <cerrno>
// #include <cstdlib>
#include <cstring>
// #include <fcntl.h> // to set fd to non-blocking
#include <iostream>
#include <map>
// #include <netdb.h> // so we can have addrinfo struct
#include "../include/cgi.hpp" // to have "accept()"
#include "../include/socket_utils.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> // to have "accept()"

bool is_this_a_cgi_one() {

}

int main(int argc, char** argv) {

    const char* configuration_file_path = "./config/configuration_file";

    if (argc >= 2) {
        configuration_file_path = argv[1];
    }

    static_cast<void>(configuration_file_path);
    // load_config_parameters(configuration_file_path);

    int listen_fd = return_a_fully_prepared_socket("8080");

    if (listen(listen_fd, 64) == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);

    // Creates an epoll instance, and avoids leaking its instance by using the only valid flag
    // available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to close
    // itself if the process running changes when using the exec() function. If you pass 0, it
    // won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);

    epoll_event event_settings;
    event_settings.events = EPOLLIN;    // me avisa quando tiver dado pra ler
    event_settings.data.fd = listen_fd; // quando o evento voltar, quero saber qual fd é

    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, listen_fd, &event_settings) == -1) {
        fail_and_exit_with_message(1, std::string("Failed to add listening socket: ")
                                          + std::strerror(errno));
    }

    const unsigned int BUFFER_SIZE = 4096;
    char* our_buffer = new char[BUFFER_SIZE]();

    epoll_event event_poll[64];

    // std::map<client_fd, cgi_instance_struct>
    std::map<int, client_connection_struct> client_map;
    // std::map<cgi_fd, client_fd> cgi_fd_map;
    std::map<int, int> cgi_fd_map;

    HttpRequestParser parser;

    // mock code; remove it later:
    int execute_cgi_once = 0;

    while (true) {
        int n = epoll_wait(epoll_instance, event_poll, 64, -1);

        for (int i = 0; i < n; i++) {

            int this_fd = event_poll[i].data.fd;

            // new connections case
            if (this_fd == listen_fd) {
                int fd_to_add =
                    accept(listen_fd, reinterpret_cast<sockaddr*>(&their_addr), &addr_size);

                if (fd_to_add == -1) {
                    fail_and_exit_with_message(1, std::strerror(errno));
                }

                make_fd_non_blocking(fd_to_add);

                event_settings.events = EPOLLIN;
                event_settings.data.fd = fd_to_add;

                if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, fd_to_add, &event_settings) == -1) {
                    fail_and_exit_with_message(
                        -1, std::string(
                                "Failed to modify epoll_instance with \"epoll_ctl()\" function: ")
                                + std::strerror(errno));
                }

                client_connection_struct client_connection;
                client_connection.client_fd = fd_to_add;
                client_connection.client_connection_type = STANDARD;

                client_map.insert(std::make_pair(fd_to_add, client_connection));

                continue;
            }

            // standard connections case
            if (event_poll[i].events & EPOLLIN && this_fd != listen_fd) {

                memset(our_buffer, 0, BUFFER_SIZE);
                int bytes_read = recv(this_fd, our_buffer, BUFFER_SIZE, 0);

                // "0" bytes read means a connection drop
                if (bytes_read == 0) {

                    if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL) == -1) {
                        fail_and_exit_with_message(
                            -1,
                            std::string(
                                "Failed to modify epoll_instance with \"epoll_ctl()\" function: ")
                                + std::strerror(errno));
                    }

                    std::cout << "The client dropped the connection!\n\n";
                }

                // Error case
                if (bytes_read == -1) {
                    fail_and_exit_with_message(1, std::strerror(errno));
                }

                std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

                if (it == client_map.end()) {
                    fail_and_exit_with_message(
                        1, std::string("Why this client fd doesn't have a instance?")
                               + std::strerror(errno));
                }

                client_connection_struct& client_connection = it->second;

                std::cout.write(our_buffer, bytes_read);
                client_connection.input_buffer.append(our_buffer, bytes_read);

                size_t length = parser.completeRequestLength(client_connection.input_buffer);
                if (length != std::string::npos) {
                    HttpRequest request;

                    request = parser.parse(client_connection.input_buffer.substr(0, length));
                    client_connection.input_buffer.erase(0, length);

                    std::cout << "method: " << request.method << "\n";
                    std::cout << "path: " << request.path << "\n";
                    std::cout << "query_string: " << request.query_string << "\n";
                    std::cout << "version: " << request.version << "\n";
                    std::cout << "headers:\n";

                    for (std::map<std::string, std::string>::const_iterator it =
                             request.headers.begin();
                         it != request.headers.end(); ++it) {
                        std::cout << "  " << it->first << ": " << it->second << "\n";
                    }

                    std::cout << "body (" << request.body.size() << " bytes): " << request.body
                              << "\n";
                }

                // mock code below: cgi case
                if (execute_cgi_once == 1) {

                    // remove this later
                    client_connection.client_connection_type = CGI;

                    // mock content below:
                    client_connection.cgi_instance.cgi_command->cgi_type = INTERPRETED_LANGUAGE;
                    client_connection.cgi_instance.cgi_command->interpreted_language_path =
                        "/usr/bin/python";
                    client_connection.cgi_instance.cgi_command->path_to_program =
                        "./relevant_files/sample_python_script.py";
                    client_connection.cgi_instance.cgi_command->args.push_back("argument number 1");
                    client_connection.cgi_instance.cgi_command->args.push_back("argument number 2");
                    client_connection.cgi_instance.cgi_command->args.push_back("argument number 3");


                    int cgi_fd = 0;

                    try {
                        cgi_fd = execute_cgi(client_connection.cgi_instance);
                    } catch (std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        fail_and_exit_with_message(-1, "We had an exception.");
                    }

                    cgi_fd_map.insert(std::make_pair(cgi_fd, this_fd));

                    execute_cgi_once == 0;
                }

                // this is a cgi fd and it's done

                std::map<int, int>::iterator cgi_fd_result_it = cgi_fd_map.find(this_fd);

                if (event_poll[i].events & EPOLLHUP && cgi_fd_map.find(this_fd) != cgi_fd_map.end()) {


                    std::map<int, client_connection_struct>::iterator cgi_fd_result_it = client_map.find(cgi_fd_result_it->second);

                    // happy path





                }
            }

            // if (event_poll[i].events & EPOLLOUT) {
            //     // fd tem espaço pra escrever
            // }
        }
    }

    delete[] our_buffer;
}
