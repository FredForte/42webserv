#include "../include/cgi.hpp" // to have "accept()"
#include "../include/client_connection.hpp"
#include "../include/main_functions.hpp"
#include "../include/parser/ConfigParser.hpp"
#include "../include/parser/HttpRequest.hpp"
#include "../include/parser/HttpRequestParser.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/response/HttpResponse.hpp"
#include "../include/response/response_handlers.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/main_functions_utils.hpp"
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
#include <sys/wait.h>
#include <unistd.h>

// done: Fred: If cgi doesn't have a space, it all gets fucked, tokenizer was consuming the ';'
// todo: Fred: Fix content length to whole body HTTP response (/r/n/r/n)
// done: Fred: Fill in all HTTP response status codes.
// done: Fred: our server must have default error pages if none are provided.
// done: Fred: Clients must be able to upload files.
// done: Fred: You need at least the GET, POST, and DELETE methods. todo: Both: Stress test your
//             server to ensure it remains available at all times.
// todo: Julio: think about having multiple listening fd's ready based on config file configuration
// todo: Julio: client instance struct will need to have a pointer to a struct ServerConfig, so it
// can properly answer done: Fred: Fix content length to whole body HTTP response (/r/n/r/n) done:
// Fred: Fill in all HTTP response status codes. done: Fred: our server must have default error
// pages if none are provided. done: Fred: Clients must be able to upload files. done: Fred: You
// need at least the GET, POST, and DELETE methods. todo: Both: Stress test your server to ensure it
// remains available at all times. todo: Both: Your server must be able to listen to multiple ports
// to deliver different content (see
//              Configuration file).
// todo: Fred: Set the maximum allowed size for client request bodies.
// done: Fred: HTTP redirection.
// done: Fred: Enabling or disabling directory listing.
// done: Fred: Default file to serve when the requested resource is a directory.
// done: Fred: Uploading files from the clients to the server is authorized, and storage location
//             is provided.
// done: Fred: Test chunk sizes are in hexadecimal.
// done: Fred: Test chunk limit read between calls.
// done: Fred: Test if the chuncked content has a "/r/n" and it's still accepted, not treated as a
//             CRLF end line.
// todo: Fred: Set limit to how much we can read.
// ---
// done: Fred: HTTP redirection.
// done: Fred: Enabling or disabling directory listing.
// done: Fred: Default file to serve when the requested resource is a directory.
// done: Fred: Uploading files from the clients to the server is authorized, and storage location
//              is provided.
// done: Fred: Test chunk sizes are in hexadecimal.
// done: Fred: Test chunk limit read between calls.
// done: Fred: Test if the chuncked content has a "/r/n" and it's still accepted, not treated as a
// CRLF end line. todo: Fred: Set limit to how much we can read. todo: Julio: Have a careful look at
// the environment variables involved in the web server-CGI
//              communication. The full request and arguments provided by the client must be
//              available to the CGI.
// done: Julio: Internal server error. Deal with it
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
    for (size_t i = 0; i < server_config_vec.size(); i++) {
        printServer(server_config_vec[i]);
    }

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

    while (true) {
        // when server is idle, poll everysecond while we have a cgi running
        // so we can monitor using reap_timed_out_cgis.
        // 1000 here holds this check when epoll is not signaling the server.
        int wait_timeout = cgi_fd_map.empty() ? -1 : 1000;
        int n = epoll_wait(epoll_instance, event_poll, 64, wait_timeout);

        for (int i = 0; i < n; i++) {

            int this_fd = event_poll[i].data.fd;

            // new connections case
            if (is_this_a_listen_fd(listen_fd_to_server_config_map, this_fd)) {

                new_connections_func(epoll_instance, event_settings, this_fd,
                                     listen_fd_to_server_config_map, client_map,
                                     fd_to_ServerConfig_ptr_map);
                continue;
            }

            // standard connections case. Only client connections are accepted
            if (event_poll[i].events & EPOLLIN
                && is_this_a_listen_fd(listen_fd_to_server_config_map, this_fd) == false
                && is_this_a_cgi_fd(cgi_fd_map, this_fd) == false) {

                standard_connections_func(this_fd, BUFFER_SIZE, our_buffer, epoll_instance,
                                          client_map, cgi_fd_map, argv[0]);
                continue;
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

                // error case
                if (bytes_read == -1) {
                    fail_and_exit_with_message(1, std::strerror(errno));
                }

                // data still coming: append it and wait for more on a later iteration.
                if (bytes_read > 0) {
                    client_connection->cgi_instance.cgi_response.append(our_buffer, bytes_read);
                    continue;
                }

                // bytes_read == 0: the pipe hit EOF, so the CGI closed stdout and all
                // of its output is now in cgi_response.
                // reap the child WITHOUT blocking (WNOHANG).
                // if the CGI somehow hasn't exited yet, we leave the fd registered
                // and re-check on the next epoll wakeup to prevent blocking the server.
                // a cgi that closes stdout but keeps running would run here until the cgi_timeout
                // kills it.
                int status = 0;
                pid_t reaped = waitpid(client_connection->cgi_instance.cgi_pid, &status, WNOHANG);
                if (reaped == 0) {
                    continue; // child not finished yet
                }

                cgi_fd_map.erase(client_connection->cgi_instance.cgi_fd);
                if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, client_connection->cgi_instance.cgi_fd,
                              NULL)
                    == -1) {
                    fail_and_exit_with_message(-1,
                                               std::string("Failed to modify epoll_instance with "
                                                           "\"epoll_ctl()\" function: ")
                                                   + std::strerror(errno));
                }

                // only send 502 on a positive failure signal (non-zero exit or killed by
                // a signal). if we couldn't confirm the status, serve the output we have.
                bool cgi_failed = (reaped > 0) && (!WIFEXITED(status) || WEXITSTATUS(status) != 0);

                if (cgi_failed) {
                    queue_error_response(epoll_instance, *client_connection, 502);
                } else {
                    HttpResponse cgi_response = parseCgiResponse(
                        client_connection->cgi_instance.cgi_response,
                        *client_connection->ServerConfig_ptr, client_connection->request_data);
                    queue_response(epoll_instance, *client_connection, cgi_response);
                }
                continue;
            }

            // this is a normal response
            if (event_poll[i].events & EPOLLOUT) {
                std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);
                if (it == client_map.end()) {
                    // no client state for this fd to respond with: drop it from epoll
                    // and close it rather than taking down the whole server.
                    epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL);
                    close(this_fd);
                    continue;
                }

                client_connection_struct& client_connection = it->second;

                if (client_connection.ready_to_respond == false) {
                    // any failure while building a standard response becomes a 500 for this client
                    try {
                        LocationConfig* responseLocation = findRequestedLocation(
                            *client_connection.ServerConfig_ptr, client_connection.request_data);

                        if (responseLocation) {

                            // redirection case
                            if (responseLocation->redirect_code != 0) {

                                // A "return" location answers every method the same way, so this is
                                // checked ahead of the methods list instead of going through it.
                                HttpResponse responseMessage = buildRedirectResponse(
                                    *client_connection.ServerConfig_ptr, *responseLocation,
                                    client_connection.request_data);

                                client_connection.output_buffer =
                                    parseResponseToOutPut(responseMessage);

                                // method allowed check
                            } else if (findStringOnVector(responseLocation->methods,
                                                          client_connection.request_data.method)) {
                                // if (client_connection.request_data.method == "GET") {
                                //     // create response for get method
                                //     HttpResponse responseMessage = getResponseMessage(
                                //         200, client_connection.ServerConfig_ptr,
                                //         *responseLocation, client_connection.request_data);

                                //     client_connection.output_buffer =
                                //         parseResponseToOutPut(responseMessage);
                                // }

                                // getResponseMessage dispatches on request_data.method
                                // internally (GET/POST/DELETE each have their own handler
                                // in response_handlers.cpp).
                                HttpResponse responseMessage = getResponseMessage(
                                    200, client_connection.ServerConfig_ptr, *responseLocation,
                                    client_connection.request_data);

                                client_connection.output_buffer =
                                    parseResponseToOutPut(responseMessage);

                            } else {
                                HttpResponse responseMessage = getResponseMessage(
                                    405, client_connection.ServerConfig_ptr, *responseLocation,
                                    client_connection.request_data);
                                client_connection.output_buffer =
                                    parseResponseToOutPut(responseMessage);
                            }

                            // if response location is not found i.e. NULL
                        } else {
                            HttpResponse responseMessage = getResponseMessage(
                                404, client_connection.ServerConfig_ptr, LocationConfig(),
                                client_connection.request_data);

                            client_connection.output_buffer =
                                parseResponseToOutPut(responseMessage);
                        }

                        client_connection.ready_to_respond = true;
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        queue_error_response(epoll_instance, client_connection, 500);
                    }
                }

                if (client_connection.ready_to_respond == true) {
                    ssize_t bytes_send =
                        send(this_fd, client_connection.output_buffer.c_str(),
                             client_connection.output_buffer.length(), MSG_NOSIGNAL);

                    if (bytes_send == -1) {
                        // the client is gone (reset / broken pipe): drop this one connection
                        epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL);
                        close(this_fd);
                        client_map.erase(this_fd);
                        continue;
                    }

                    client_connection.output_buffer.erase(0, bytes_send);
                }
                // todo: Decide to close or keep alive based on connection.type
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

        // After handling this batch of events, retire any CGI that ran too long.
        reap_timed_out_cgis(epoll_instance, client_map, cgi_fd_map);
    }

    delete[] our_buffer;
}
