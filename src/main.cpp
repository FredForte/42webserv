#include "../include/cgi.hpp" // to have "accept()"
#include "../include/client_connection.hpp"
#include "../include/main_functions.hpp"
#include "../include/parser/ConfigParser.hpp"
#include "../include/parser/HttpRequest.hpp"
#include "../include/parser/HttpRequestParser.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/response/HttpResponse.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/utils_config_file.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
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

bool is_this_a_listen_fd(std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref,
                         int this_fd) {
    return listen_fd_to_server_config_map_ref.find(this_fd)
           != listen_fd_to_server_config_map_ref.end();
}

// Can throw exception
ServerConfig* get_server_config_instance_based_on_listen_fd(
    std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref, int this_fd) {

    std::map<int, ServerConfig*>::iterator it = listen_fd_to_server_config_map_ref.find(this_fd);

    if (it == listen_fd_to_server_config_map_ref.end()) {
        throw std::runtime_error("Should't this fd be a listen fd one?!");
    }

    return it->second;
}

// todo: Fred: Fix content length to whole body HTTP response (/r/n/r/n)
// todo: Fred: Fill in all HTTP response status codes. todo: Fred: our server must have default
//             error pages if none are provided. todo: Fred: Clients must be able to upload files.
// todo: Fred: You need at least the GET, POST, and DELETE methods. todo: Both: Stress test your
//             server to ensure it remains available at all times.
// todo: Fred: Set the maximum allowed size for client request bodies.
// todo: Fred: HTTP redirection.
// todo: Fred: Enabling or disabling directory listing.
// todo: Fred: Default file to serve when the requested resource is a directory.
// todo: Fred: Uploading files from the clients to the server is authorized, and storage location
//             is provided.
// todo: Fred: Test chunk sizes are in hexadecimal.
// todo: Fred: Test chunk limit read between calls.
// todo: Fred: Test if the chuncked content has a "/r/n" and it's still accepted, not treated as a
//             CRLF end line. todo: Fred: Set limit to how much we can read.
// ---
// todo: Julio: Have a careful look at the environment variables involved in the web server-CGI
//              communication. The full request and arguments provided by the client must be
//              available to the CGI.
// todo: Julio: (hardcore) If CGI and Post, send data to CGI's stdin
// todo: Julio: CGI returns structured content
// todo: Julio: Test: The CGI should be run in the correct directory for relative path file access.
// todo: Julio: Support cookies and session management (provide simple examples).
// ---
// todo: Both: Your server must be able to listen to multiple ports to deliver different content
//             (see Configuration file).
// todo: Both: You must provide configuration files and default files to test and demonstrate that
//             every feature works during the evaluation.
// todo: Both: Test with 42 tester
// todo: Both: Readme.
int main(int argc, char** argv) {

    const char* configuration_file_path = "./config/example.conf";

    if (argc >= 2) {
        configuration_file_path = argv[1];
    }

    std::string source = readFile(configuration_file_path);
    ConfigParser parser(source);
    Config server_config_vec = parser.parse();
    // for (size_t i = 0; i < config_vec.size(); i++) {
    //     printServer(config_vec[i]);
    // }

    epoll_event event_settings;
    // Creates an epoll instance, and avoids leaking its instance by using the only valid
    // flag available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to
    // close itself if the process running changes when using the exec() function. If you
    // pass 0, it won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);

    // std::map<listen_fd, ServerConfig*>
    std::map<int, ServerConfig*> listen_fd_to_server_config_map;

    size_t server_config_vec_size = server_config_vec.size();
    for (size_t i = 0; i < server_config_vec_size; i++) {

        size_t listens_size = server_config_vec[i].listens.size();
        for (size_t j = 0; j < listens_size; j++) {

            std::stringstream ss_port_value;
            ss_port_value << server_config_vec[i].listens[j].port;

            int listen_fd_instance = return_a_fully_prepared_socket(ss_port_value.str().c_str());

            if (listen(listen_fd_instance, 64) == -1) {
                fail_and_exit_with_message(1, std::strerror(errno));
            }

            event_settings.events = EPOLLIN; // me avisa quando tiver dado pra ler
            event_settings.data.fd =
                listen_fd_instance; // quando o evento voltar, quero saber qual fd é

            if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, listen_fd_instance, &event_settings)
                == -1) {
                fail_and_exit_with_message(1, std::string("Failed to add listening socket: ")
                                                  + std::strerror(errno));
            }

            listen_fd_to_server_config_map.insert(
                std::make_pair(listen_fd_instance, &server_config_vec[i]));
        }
    }

    const unsigned int BUFFER_SIZE = 4096;
    char* our_buffer = new char[BUFFER_SIZE]();

    epoll_event event_poll[64];

    // std::map<client_fd, client_connection_struct>
    std::map<int, client_connection_struct> client_map;
    // std::map<cgi_fd, client_fd> cgi_fd_map;
    std::map<int, int> cgi_fd_map;
    // std::map<client_fd, ServerConfig> fd_to_ServerConfig_ptr_map;
    std::map<int, ServerConfig*> fd_to_ServerConfig_ptr_map;

    HttpRequestParser req_parser;

    // mock code; remove it later:
    int execute_cgi_once = false;

    while (true) {
        int n = epoll_wait(epoll_instance, event_poll, 64, -1);

        for (int i = 0; i < n; i++) {

            int this_fd = event_poll[i].data.fd;

            // new connections case
            if (is_this_a_listen_fd(listen_fd_to_server_config_map, this_fd)) {
                new_connections_func(epoll_instance, event_settings, this_fd,
                                     listen_fd_to_server_config_map, client_map,
                                     fd_to_ServerConfig_ptr_map);
                continue;
            }

            // standard connections case. Only client connections accepted
            if (event_poll[i].events & EPOLLIN
                && is_this_a_listen_fd(listen_fd_to_server_config_map, this_fd) == false
                && is_this_a_cgi_fd(cgi_fd_map, this_fd) == false) {

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
                    continue;
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

                size_t length = req_parser.completeRequestLength(client_connection.input_buffer);
                if (length != std::string::npos) {
                    HttpRequest request;

                    request = req_parser.parse(client_connection.input_buffer.substr(0, length));
                    client_connection.input_buffer.erase(0, length);

                    // std::cout << "method: " << request.method << "\n";
                    // std::cout << "path: " << request.path << "\n";
                    // std::cout << "query_string: " << request.query_string << "\n";
                    // std::cout << "version: " << request.version << "\n";
                    // std::cout << "headers:\n";

                    // for (std::map<std::string, std::string>::const_iterator it =
                    //          request.headers.begin();
                    //      it != request.headers.end(); ++it) {
                    //     std::cout << "  " << it->first << ": " << it->second << "\n";
                    // }

                    // std::cout << "body (" << request.body.size() << " bytes): " << request.body
                    //           << "\n";

                    client_connection.request_data = request;

                    epoll_event event_settings;
                    event_settings.events = EPOLLOUT;
                    event_settings.data.fd = client_connection.client_fd;

                    epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection.client_fd,
                              &event_settings);
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
                    continue;
                }
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

            // this is a normal response
            if (event_poll[i].events & EPOLLOUT) {
                std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);
                if (it == client_map.end()) {
                    fail_and_exit_with_message(
                        1, std::string("Why this client fd doesn't have an instance?")
                               + std::strerror(errno));
                }

                client_connection_struct& client_connection = it->second;

                if (client_connection.ready_to_respond == false) {
                    LocationConfig* responseLocation =
                        findRequestedLocation(server_config_vec[0], client_connection.request_data);

                    if (responseLocation) {
                        // std::cout << "found the request location" << std::endl;
                        // std::cout << "Request method check on location" << std::endl;
                        // check request method and location method

                        if (findStringOnVector(responseLocation->methods,
                                               client_connection.request_data.method)) {
                            // std::cout << "Found requested method: "
                            //           << client_connection.request_data.method << " on found
                            //           location"
                            //           << std::endl;

                            if (client_connection.request_data.method == "GET") {
                                // create response for get method
                                HttpResponse responseMessage = getResponseMessage(
                                    200, server_config_vec[0], *responseLocation);
                                client_connection.output_buffer =
                                    parseResponseToOutPut(responseMessage);
                            }
                        } else {
                            // return error page 500 or something
                            std::cerr << "Method requested not allowed on location" << std::endl;
                        }
                    } else {
                        // return error page 500 or something
                        std::cerr << "request location not found" << std::endl;
                    }

                    client_connection.ready_to_respond = true;
                }

                if (client_connection.ready_to_respond == true) {
                    ssize_t bytes_send =
                        send(this_fd, client_connection.output_buffer.c_str(),
                             client_connection.output_buffer.length(), MSG_NOSIGNAL);

                    if (bytes_send == -1) {
                        fail_and_exit_with_message(-1, "Why send failed?");
                    }

                    client_connection.output_buffer.erase(0, bytes_send);
                }

                if (client_connection.output_buffer.empty()) {
                    epoll_event event_settings;
                    event_settings.events = EPOLLIN;
                    event_settings.data.fd = client_connection.client_fd;

                    epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection.client_fd,
                              &event_settings);

                    client_connection.ready_to_respond = false;
                    continue;
                };
            }
        }
    }

    delete[] our_buffer;
}
