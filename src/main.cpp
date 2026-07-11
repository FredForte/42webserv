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
#include <sstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> // to have "accept()"
#include <unistd.h>

bool is_this_a_cgi_fd(const std::map<int, int>& cgi_fd_map, int this_fd) {
    return cgi_fd_map.find(this_fd) != cgi_fd_map.end();
}

// Can throw exception
client_connection_struct*
get_client_instance_based_on_cgi_fd(const std::map<int, int>& cgi_fd_map,
                                    std::map<int, client_connection_struct>& client_map,
                                    int this_fd) {

    std::map<int, int>::const_iterator cgi_fd_result_it = cgi_fd_map.find(this_fd);

    if (cgi_fd_result_it == cgi_fd_map.end()) {
        throw std::runtime_error("Should't this fd be a CGI one?!");
    }

    std::map<int, client_connection_struct>::iterator client_map_result_it =
        client_map.find(cgi_fd_result_it->second);

    if (client_map_result_it == client_map.end()) {
        throw std::runtime_error("Why this CGI fd doesn't have a related client fd?!");
    }

    return &client_map_result_it->second;
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
    int execute_cgi_once = 1;

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
                client_connection.cgi_instance = cgi_instance_struct();

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

                    std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

                    if (it == client_map.end()) {
                        fail_and_exit_with_message(
                            1, std::string("Why this client fd doesn't have a instance?")
                                   + std::strerror(errno));
                    }

                    client_map.erase(this_fd);

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
                if (execute_cgi_once == true) {

                    // remove this later
                    client_connection.client_connection_type = CGI;

                    // mock content below:
                    client_connection.cgi_instance.cgi_command.cgi_type = INTERPRETED_LANGUAGE;
                    client_connection.cgi_instance.cgi_command.interpreted_language_path =
                        "/usr/bin/python";
                    client_connection.cgi_instance.cgi_command.path_to_program =
                        "./relevant_files/sample_python_script.py";
                    client_connection.cgi_instance.cgi_command.args.push_back("argument number 1");
                    client_connection.cgi_instance.cgi_command.args.push_back("argument number 2");
                    client_connection.cgi_instance.cgi_command.args.push_back("argument number 3");

                    int cgi_fd = 0;

                    try {
                        cgi_fd = execute_cgi(client_connection.cgi_instance);
                    } catch (std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        fail_and_exit_with_message(-1, "We had an exception.");
                    }

                    cgi_fd_map.insert(std::make_pair(cgi_fd, this_fd));

                    execute_cgi_once = false;
                }

                // this is a cgi fd
                if ((event_poll[i].events & EPOLLIN || event_poll[i].events & EPOLLHUP)
                    && is_this_a_cgi_fd(cgi_fd_map, this_fd)) {

                    client_connection_struct* client_connection;
                    try {
                        client_connection =
                            get_client_instance_based_on_cgi_fd(cgi_fd_map, client_map, this_fd);
                    } catch (const std::exception& e) {
                        fail_and_exit_with_message(-1, e.what());
                    }

                    memset(our_buffer, 0, BUFFER_SIZE);
                    int bytes_read = read(this_fd, our_buffer, BUFFER_SIZE);

                    // Error case
                    if (bytes_read == -1) {
                        fail_and_exit_with_message(1, std::strerror(errno));
                    }

                    // "0" bytes read means EOF
                    if (bytes_read == 0 && event_poll[i].events & EPOLLHUP) {
                        cgi_fd_map.erase(client_connection->cgi_instance.cgi_fd);

                        if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL,
                                      client_connection->cgi_instance.cgi_fd, NULL)
                            == -1) {

                            fail_and_exit_with_message(
                                -1, std::string("Failed to modify epoll_instance with "
                                                "\"epoll_ctl()\" function: ")
                                        + std::strerror(errno));
                        }

                        std::stringstream ss_http_response;

                        ss_http_response << "HTTP/1.1 200 OK\r\n"
                                            "Content-Type: text/html\r\n";

                        ss_http_response << "Content-Length: "
                                         << client_connection->cgi_instance.cgi_response.length()
                                         << "\r\n"
                                            "\r\n"
                                         << client_connection->cgi_instance.cgi_response;

                        client_connection->output_buffer.append(ss_http_response.str());

                        epoll_event event_settings;
                        event_settings.events = EPOLLOUT;
                        event_settings.data.fd = client_connection->client_fd;

                        epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection->client_fd,
                                  &event_settings);
                    }

                    client_connection->cgi_instance.cgi_response.append(our_buffer, bytes_read);
                    continue;
                }
            }

            if (event_poll[i].events & EPOLLOUT) {
                std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

                if (it == client_map.end()) {
                    fail_and_exit_with_message(
                        1, std::string("Why this client fd doesn't have a instance?")
                               + std::strerror(errno));
                }

                client_connection_struct& client_connection = it->second;

                if (client_connection.output_buffer.empty()) {
                    epoll_event event_settings;
                    event_settings.events = EPOLLIN;
                    event_settings.data.fd = client_connection.client_fd;

                    epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection.client_fd,
                              &event_settings);
                    continue;
                };

                ssize_t bytes_send = send(this_fd, client_connection.output_buffer.c_str(),
                                          client_connection.output_buffer.length(), MSG_NOSIGNAL);

                if (bytes_send == -1) {
                    fail_and_exit_with_message(-1, "Why send failed?");
                }

                client_connection.output_buffer.erase(0, bytes_send);
            }
        }
    }

    delete[] our_buffer;
}
