#include "../include/cgi.hpp" // to have "accept()"
#include "../include/client_connection.hpp"
#include "../include/main_functions.hpp"
#include "../include/parser/ConfigParser.hpp"
#include "../include/parser/ConfigValidator.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/response/HttpResponse.hpp"
#include "../include/response/response_handlers.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/main_functions_utils.hpp"
#include "../include/utils/utils_config_file.hpp"
#include <cerrno>
#include <csignal>
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
// validate: Fred: Fix content length to whole body HTTP response (/r/n/r/n)
// done: Fred: our server must have default error pages if none are provided.
// done: Fred: Clients must be able to upload files.
// done: Fred: You need at least the GET, POST, and DELETE methods. todo: Both: Stress test your
//             server to ensure it remains available at all times.
// done: Julio: think about having multiple listening fd's ready based on config file configuration
// done: Julio: client instance struct will need to have a pointer to a struct ServerConfig, so it
//              can properly answer
// done: Fred: Clients must be able to upload files. done: Fred: You
//             need at least the GET, POST, and DELETE methods. todo: Both: Stress test your server
//             to ensure it remains available at all times.
// done: Both: Your server must be able to listen to multiple ports to deliver different content
//             (see Configuration file).
// done: Fred: Set the maximum allowed size for client request bodies.
// done: Fred: HTTP redirection.
// done: Fred: Enabling or disabling directory listing.
// done: Fred: Default file to serve when the requested resource is a directory.
// done: Fred: Uploading files from the clients to the server is authorized, and storage location
//             is provided.
// done: Fred: Test chunk sizes are in hexadecimal.
// done: Fred: Test chunk limit read between calls.
// done: Fred: Test if the chuncked content has a "/r/n" and it's still accepted, not treated as a
//             CRLF end line.
// validate: Fred: Set limit to how much we can read.
// ---
// done: Fred: HTTP redirection.
// done: Fred: Enabling or disabling directory listing.
// done: Fred: Default file to serve when the requested resource is a directory.
// done: Fred: Uploading files from the clients to the server is authorized, and storage location
//             is provided.
// done: Fred: Test chunk sizes are in hexadecimal.
// done: Fred: Test chunk limit read between calls.
// done: Fred: Test if the chuncked content has a "/r/n" and it's still accepted, not treated as a
//             CRLF end line.
// todo: Julio: Have a careful look at the environment variables involved in the web server-CGI
//              communication. The full request and arguments provided by the client must be
//              available to the CGI.
// done: Julio: Internal server error. Deal with it
// todo: Julio: (hardcore) If CGI and Post, send data to CGI's stdin
// todo: Julio: CGI returns structured content
// todo: Julio: Test: The CGI should be run in the correct directory for relative path file access.
// todo: Julio: Support cookies and session management (provide simple examples).
// ---
// done: Both: Your server must be able to listen to multiple ports to deliver different content
//             (see Configuration file).
// todo: Both: You must provide configuration files and default files to test and demonstrate that
//             every feature works during the evaluation.
// todo: Both: Test with 42 tester
// todo: Both: Readme.
// Flipped by SIGINT/SIGTERM so the event loop can exit and run its cleanup
// (freeing the read buffer, closing fds) instead of being hard-killed. That
// clean exit is what lets a leak checker like valgrind produce a meaningful,
// noise-free report. epoll_wait always returns EINTR on a signal (it is never
// restarted), so flipping this flag is enough to break out even while idle.
static volatile sig_atomic_t g_stop = 0;

static void handle_stop_signal(int) { g_stop = 1; }

int main(int argc, char** argv) {

    const char* configuration_file_path = "./config/example.conf";

    if (argc >= 2) {
        configuration_file_path = argv[1];
    }

    // readFile returns an empty string for a missing/unreadable file (it can't
    // signal the failure otherwise), so treat empty source as a hard config error
    // rather than silently starting a server with nothing to serve.
    std::string source = readFile(configuration_file_path);
    if (source.empty()) {
        fail_and_exit_with_message(1, std::string("config file is empty or unreadable: ")
                                          + configuration_file_path);
    }

    // Parse and validate up front. A bad config must abort startup with a clear
    // message instead of launching a server that can never answer.
    Config server_config_vec;
    try {
        ConfigParser parser(source);
        server_config_vec = parser.parse();
        ConfigValidator validator;
        validator.validate(server_config_vec);
    } catch (const std::exception& e) {
        fail_and_exit_with_message(1, e.what());
    }

    for (size_t i = 0; i < server_config_vec.size(); i++) {
        printServer(server_config_vec[i]);
    }

    // we only set data.fd, so epoll_event would not end up with uninitialized bytes
    // so memset would initialize all the struct preventing it.
    epoll_event event_settings;
    memset(&event_settings, 0, sizeof(event_settings));
    // Creates an epoll instance, and avoids leaking its instance by using the only valid
    // flag available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to
    // close itself if the process running changes when using the exec() function. If you
    // pass 0, it won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);

    std::map<int, int> listening_fd_to_port;
    std::map<int, int> client_fd_to_port;
    std::multimap<int, ServerConfig*> port_to_server_config_ptr_mmap;

    size_t server_config_vec_size = server_config_vec.size();
    for (size_t i = 0; i < server_config_vec_size; i++) {

        size_t listens_size = server_config_vec[i].listens.size();
        for (size_t j = 0; j < listens_size; j++) {

            if (port_to_server_config_ptr_mmap.count(server_config_vec[i].listens[j].port) == 0) {

                std::stringstream ss_port_value;
                ss_port_value << server_config_vec[i].listens[j].port;

                int listen_fd_instance =
                    return_a_fully_prepared_socket(ss_port_value.str().c_str());

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

                listening_fd_to_port.insert(
                    std::make_pair(listen_fd_instance, server_config_vec[i].listens[j].port));
            }

            // every server that listens on this port is registered once.
            // the listening socket above is created only the first time we see it.
            // the first server registered for a port is that port's default.
            port_to_server_config_ptr_mmap.insert(
                std::make_pair(server_config_vec[i].listens[j].port, &server_config_vec[i]));
        }
    }

    // One shared scratch buffer for every recv() and every CGI pipe read, so its
    // size costs us once, not once per connection. 4KB meant ~25,000 round trips
    // through the event loop for a 100MB upload; this size drains whatever the
    // kernel has queued in far fewer passes. It is deliberately near the socket
    // buffer ceiling (net.core.rmem_max, 4MB here) - past that there is simply
    // never that much waiting to be read.
    const unsigned int BUFFER_SIZE = 2 * 1024 * 1024;
    char* our_buffer = new char[BUFFER_SIZE]();

    epoll_event event_poll[64];

    // std::map<client_fd, client_connection_struct>
    std::map<int, client_connection_struct> client_map;
    // std::map<cgi_fd, client_fd> cgi_fd_map;
    std::map<int, int> cgi_fd_map;
    // std::map<client_fd, ServerConfig> client_fd_to_ServerConfig_ptr;
    // std::map<int, ServerConfig*> client_fd_to_ServerConfig_ptr;

    // Graceful shutdown: on Ctrl-C (SIGINT) or `kill` (SIGTERM) we break the loop
    // below and fall through to the cleanup, rather than dying mid-flight.
    signal(SIGINT, handle_stop_signal);
    signal(SIGTERM, handle_stop_signal);

    while (!g_stop) {
        // when server is idle, poll everysecond while we have a cgi running
        // so we can monitor using reap_timed_out_cgis.
        // 1000 here holds this check when epoll is not signaling the server.
        int wait_timeout = cgi_fd_map.empty() ? -1 : 1000;
        int n = epoll_wait(epoll_instance, event_poll, 64, wait_timeout);

        for (int i = 0; i < n; i++) {

            int this_fd = event_poll[i].data.fd;

            // new connections case
            if (is_this_a_listen_fd(listening_fd_to_port, this_fd)) {

                new_connections_func(epoll_instance, event_settings, this_fd, listening_fd_to_port,
                                     client_fd_to_port);
                continue;
            }

            // standard connections case. Only client connections are accepted
            if (event_poll[i].events & EPOLLIN
                && !is_this_a_listen_fd(listening_fd_to_port, this_fd)
                && !is_this_a_cgi_fd(cgi_fd_map, this_fd)) {

                standard_connections_func(this_fd, BUFFER_SIZE, our_buffer, epoll_instance,
                                          port_to_server_config_ptr_mmap, client_map,
                                          client_fd_to_port, cgi_fd_map);
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

                // No memset: read() reports exactly how many bytes it wrote and we
                // only ever touch that many. Clearing the whole buffer first would
                // cost more than the read itself now that it is megabytes wide.
                int bytes_read = read(this_fd, our_buffer, BUFFER_SIZE);

                // we cant inspect errno after read, so we treat this as failed cgi
                // stop the child, drop and close its fd and answer the client with 502.
                if (bytes_read == -1) {
                    detach_cgi_fd(epoll_instance, client_connection->cgi_instance, cgi_fd_map,
                                  true);
                    queue_error_response(epoll_instance, *client_connection, 502);
                    resetCgiInstance(client_connection->cgi_instance);
                    continue;
                }

                // data still coming: append it and wait for more on a later iteration.
                if (bytes_read > 0) {
                    client_connection->cgi_instance.cgi_response.append(our_buffer, bytes_read);
                    continue;
                }

                // bytes_read == 0: the pipe hit EOF, so the CGI closed stdout and all
                // of its output is now in cgi_response. Only the exit status is missing.
                //
                // Mark it here and stop watching the pipe further down: a pipe whose
                // writer is gone stays readable forever, so leaving it armed would make
                // epoll hand it straight back on every wakeup. The old code did exactly
                // that whenever the child wasn't reapable yet, spinning the loop at full
                // CPU - for a cgi that closes stdout but keeps running, until its
                // cgi_timeout finally expired.
                client_connection->cgi_instance.stdout_closed = true;

                // reap the child WITHOUT blocking (WNOHANG). Virtually always succeeds
                // right here, since stdout closing is the child exiting. If it doesn't,
                // reap_timed_out_cgis picks it up on a later pass (the loop wakes at
                // least once a second while any cgi is registered) instead of us
                // spinning on it, and the timeout still bounds the wait.
                int status = 0;
                pid_t reaped = waitpid(client_connection->cgi_instance.cgi_pid, &status, WNOHANG);
                if (reaped == 0) {
                    epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL);
                    continue; // child not finished yet
                }

                complete_cgi_request(epoll_instance, *client_connection, cgi_fd_map, reaped,
                                     status);
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

                                parseResponseToOutPut(responseMessage,
                                                      client_connection.output_buffer);

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

                                parseResponseToOutPut(responseMessage,
                                                      client_connection.output_buffer);

                            } else {
                                HttpResponse responseMessage = getResponseMessage(
                                    405, client_connection.ServerConfig_ptr, *responseLocation,
                                    client_connection.request_data);
                                parseResponseToOutPut(responseMessage,
                                                      client_connection.output_buffer);
                            }

                            // if response location is not found i.e. NULL
                        } else {
                            HttpResponse responseMessage = getResponseMessage(
                                404, client_connection.ServerConfig_ptr, LocationConfig(),
                                client_connection.request_data);

                            parseResponseToOutPut(responseMessage,
                                                  client_connection.output_buffer);
                        }

                        // every branch above just replaced output_buffer wholesale,
                        // so the drain position starts over at the front
                        client_connection.output_sent = 0;
                        client_connection.ready_to_respond = true;
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        queue_error_response(epoll_instance, client_connection, 500);
                    }
                }

                if (client_connection.ready_to_respond == true) {
                    // Send from where the last one stopped rather than from the
                    // front of a shrinking buffer - see output_sent in
                    // client_connection.hpp for why erasing here was quadratic.
                    ssize_t bytes_send =
                        send(this_fd, client_connection.output_buffer.data()
                                          + client_connection.output_sent,
                             client_connection.output_buffer.size()
                                 - client_connection.output_sent,
                             MSG_NOSIGNAL);

                    if (bytes_send == -1) {
                        // the client is gone (reset / broken pipe): drop this one
                        // connection, taking any cgi it still has running with it
                        drop_client(epoll_instance, client_map, cgi_fd_map, this_fd);
                        continue;
                    }

                    // Echoing every byte we send to the terminal costs as much as the
                    // response itself (a 100MB CGI body = 100MB to stdout) and stalls
                    // the loop on a slow tty. Kept commented for local debugging only.
                    // std::cout.write(client_connection.output_buffer.c_str(), bytes_send);
                    client_connection.output_sent += static_cast<size_t>(bytes_send);

                    if (client_connection.output_sent >= client_connection.output_buffer.size()) {
                        client_connection.output_buffer.clear();
                        client_connection.output_sent = 0;
                    }
                }
                // epollout drain, close or keep alive based on conneciton type from
                // request, or set to close if request body exceeded max size and
                // has been responsed as 413.
                if (client_connection.output_buffer.empty()) {
                    if (client_connection.close_after_response) {
                        drop_client(epoll_instance, client_map, cgi_fd_map,
                                    client_connection.client_fd);
                        continue;
                    }

                    epoll_event event_settings;
                    memset(&event_settings, 0, sizeof(event_settings));
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

    // Graceful-shutdown cleanup (reached when g_stop is set). The STL containers
    // free their own memory when they go out of scope here; we only need to hand
    // back what we own explicitly: the read buffer and every open fd. Closing the
    // fds keeps the shutdown tidy (no lingering CLOSE_WAIT/listening sockets).
    for (std::map<int, client_connection_struct>::iterator it = client_map.begin();
         it != client_map.end(); ++it) {
        close(it->first);
    }
    for (std::map<int, int>::iterator it = cgi_fd_map.begin(); it != cgi_fd_map.end(); ++it) {
        close(it->first);
    }
    for (std::map<int, int>::iterator it = listening_fd_to_port.begin();
         it != listening_fd_to_port.end(); ++it) {
        close(it->first);
    }
    close(epoll_instance);

    delete[] our_buffer;
}
