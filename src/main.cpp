#include "../include/client_connection.hpp"
#include "../include/program_flow_utils.hpp"
#include "./parser/HttpRequest.hpp"
#include "./parser/HttpRequestParser.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h> // to set fd to non-blocking
#include <iostream>
#include <map>
#include <netdb.h> // so we can have addrinfo struct
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> // to have "accept()"

// Creates, set its options and bind the socket
int return_a_fully_prepared_socket(const char* PORT_NUMBER_TO_HOST) {
    addrinfo hint_addrinfo_struct;
    addrinfo* result_struct;

    memset(&hint_addrinfo_struct, 0, sizeof(hint_addrinfo_struct));

    hint_addrinfo_struct.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hint_addrinfo_struct.ai_socktype = SOCK_STREAM; // TCP, in this case. Not datagram a.k.a UDP
    hint_addrinfo_struct.ai_flags =
        AI_PASSIVE; // "AI_PASSIVE" as an argument results in the use of host machine IP

    getaddrinfo(NULL,                  // IP or domain name
                PORT_NUMBER_TO_HOST,   // Port number
                &hint_addrinfo_struct, // Base struct memsetted to zero to serve as hint
                &result_struct);       // Result struct generated

    int socket_fd =
        socket(result_struct->ai_family, result_struct->ai_socktype, result_struct->ai_protocol);

    if (socket_fd == -1) {
        fail_and_exit_with_message(1,
                                   std::string("Failed to create socket: ") + std::strerror(errno));
    }

    int option_value = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value))
        == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    if (bind(socket_fd, result_struct->ai_addr, result_struct->ai_addrlen) == -1) {
        fail_and_exit_with_message(1,
                                   std::string("Failed to bind socket: ") + std::strerror(errno));
    }

    return socket_fd;
}

void make_fd_non_blocking(int fd_to_add) {
    int flags = fcntl(fd_to_add, F_GETFL);

    if (flags == -1) {
        fail_and_exit_with_message(-1, std::string("Failed to acquire the file descriptor flags")
                                           + std::strerror(errno));
    }

    if (fcntl(fd_to_add, F_SETFL, flags | O_NONBLOCK) == -1) {
        fail_and_exit_with_message(-1,
                                   std::string("Failed to make the file descriptor non-blocking")
                                       + std::strerror(errno));
    }
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
    HttpRequestParser parser;

    while (true) {
        int n = epoll_wait(epoll_instance, event_poll, 64, -1);

        for (int i = 0; i < n; i++) {

            int this_fd = event_poll[i].data.fd;

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

                
            }

            // if (event_poll[i].events & EPOLLOUT) {
            //     // fd tem espaço pra escrever
            // }
        }
    }

    delete[] our_buffer;
}
