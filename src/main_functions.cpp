#include "../include/client_connection.hpp"
#include "../include/parser/HttpRequestParser.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/response/response_handlers.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/main_functions_utils.hpp"
#include "../include/utils/utils_config_file.hpp"
#include <cerrno>
#include <cstring>
#include <ctime>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>

void new_connections_func(int epoll_instance, epoll_event& event_settings, int this_fd,
                          std::map<int, ServerConfig*>& listen_fd_to_server_config_map,
                          std::map<int, client_connection_struct>& client_map,
                          std::map<int, ServerConfig*>& fd_to_ServerConfig_ptr_map) {
    sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);

    int fd_to_add = accept(this_fd, reinterpret_cast<sockaddr*>(&their_addr), &addr_size);

    if (fd_to_add == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    make_fd_non_blocking(fd_to_add);

    event_settings.events = EPOLLIN;
    event_settings.data.fd = fd_to_add;

    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, fd_to_add, &event_settings) == -1) {
        fail_and_exit_with_message(
            -1, std::string("Failed to modify epoll_instance with \"epoll_ctl()\" function: ")
                    + std::strerror(errno));
    }

    ServerConfig* server_config_ptr =
        get_server_config_instance_based_on_listen_fd(listen_fd_to_server_config_map, this_fd);

    client_connection_struct client_connection;
    client_connection.client_fd = fd_to_add;
    client_connection.ready_to_respond = false;
    client_connection.client_connection_type = STANDARD;
    client_connection.cgi_instance.client_fd = fd_to_add;
    client_connection.cgi_instance.cgi_fd = 0;
    client_connection.cgi_instance.epoll_instance = epoll_instance;

    std::stringstream ss;
    ss << client_connection.client_fd;
    client_connection.cookie_id = ss.str();
    client_connection.ServerConfig_ptr = server_config_ptr;

    client_map.insert(std::make_pair(fd_to_add, client_connection));

    fd_to_ServerConfig_ptr_map.insert(std::make_pair(fd_to_add, server_config_ptr));
}

void standard_connections_func(int this_fd, const unsigned int BUFFER_SIZE, char* our_buffer,
                               int epoll_instance,
                               std::map<int, client_connection_struct>& client_map,
                               std::map<int, int>& cgi_fd_map, char* this_bin_path_from_argv) {

    memset(our_buffer, 0, BUFFER_SIZE);
    int bytes_read = recv(this_fd, our_buffer, BUFFER_SIZE, 0);

    // "0" bytes read means a connection drop
    if (bytes_read == 0) {

        if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL) == -1) {
            fail_and_exit_with_message(
                -1, std::string("Failed to modify epoll_instance with \"epoll_ctl()\" function: ")
                        + std::strerror(errno));
        }

        std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

        if (it == client_map.end()) {
            fail_and_exit_with_message(1, std::string("Why this client fd doesn't have a instance?")
                                              + std::strerror(errno));
        }

        client_map.erase(this_fd);

        std::cout << "The client dropped the connection!\n\n";
        return;
    }

    // Error case
    if (bytes_read == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

    if (it == client_map.end()) {
        fail_and_exit_with_message(1, std::string("Why this client fd doesn't have a instance?")
                                          + std::strerror(errno));
    }

    client_connection_struct& client_connection = it->second;

    std::cout.write(our_buffer, bytes_read);
    client_connection.input_buffer.append(our_buffer, bytes_read);

    HttpRequestParser req_parser;

    size_t length = req_parser.completeRequestLength(client_connection.input_buffer);
    if (length == std::string::npos) {
        return;
    }

    // create and save request structure
    HttpRequest request;
    request = req_parser.parse(client_connection.input_buffer.substr(0, length));
    client_connection.input_buffer.erase(0, length);
    client_connection.request_data = request;

    // find location to determine request type
    LocationConfig* responseLocation =
        findRequestedLocation(*client_connection.ServerConfig_ptr, request);

    // cgi request
    if (responseLocation && !responseLocation->cgi_extensions.empty()) {
        // requested executable
        std::string this_concat = std::string(".") + getFileExtension(request.path);
        std::map<std::string, std::string>::iterator it =
            responseLocation->cgi_extensions.find(this_concat);

        // file extension not configured on server
        // we serve it as a normal request to EPOLLOUT
        // leave it to the write handler to repond it
        // where a NULL locaiton turns into a 404.
        if (it == responseLocation->cgi_extensions.end()) {
            epoll_event event_settings;
            event_settings.events = EPOLLOUT;
            event_settings.data.fd = client_connection.client_fd;
            epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection.client_fd, &event_settings);
            return;
        }

        // binary executable does not use path
        client_connection.cgi_instance.cgi_command.cgi_type = INTERPRETED_LANGUAGE;
        if (it->second.empty()) {
            client_connection.cgi_instance.cgi_command.cgi_type = BINARY;
        }

        // creating argv[0] for execve
        std::string this_bin_path_from_argv_cpp_str = std::string(this_bin_path_from_argv);
        std::string::size_type pos = this_bin_path_from_argv_cpp_str.rfind('/');
        if (pos == std::string::npos) {
            std::cerr << "Could not locate the cgi-bin folder from argv[0]." << std::endl;
            queue_error_response(epoll_instance, client_connection, 500);
            return;
        }
        this_bin_path_from_argv_cpp_str.erase(pos + 1);

        // fills cgi_command block:
        client_connection.cgi_instance.cgi_command.interpreted_language_path = it->second.c_str();

        std::string a_cpp_string = joinPath(this_bin_path_from_argv_cpp_str, request.path);
        // todo: Julio: This part here might lose reference to the created c string on a second call
        // a duplicate would be the best to keep it saved inside the struct
        // or save the std::string and olny convert to c when needed for execution
        client_connection.cgi_instance.cgi_command.path_to_program = a_cpp_string.c_str();

        client_connection.cgi_instance.cgi_command.args.push_back(
            client_connection.cgi_instance.cgi_command.path_to_program);

        client_connection.cgi_instance.cgi_command.envp =
            buildCgiEnv(request, *client_connection.ServerConfig_ptr);

        client_connection.cgi_instance.epoll_instance = epoll_instance;

        // executing cgi
        int cgi_fd = 0;

        try {
            cgi_fd = execute_cgi(client_connection.cgi_instance, request.body);
        } catch (std::exception& e) {
            // a failed cgi answer with a 500 and keep serving.
            std::cerr << e.what() << std::endl;
            queue_error_response(epoll_instance, client_connection, 500);
            return;
        }

        client_connection.cgi_instance.cgi_fd = cgi_fd;
        client_connection.cgi_instance.start_time = time(NULL);
        client_connection.cgi_instance.timeout_seconds = responseLocation->cgi_timeout;
        cgi_fd_map.insert(std::make_pair(cgi_fd, this_fd));

        // return before setting it as ready to respond now, still needs child process
        // to finish sending buffered data and return code.
        return;
    }

    // standard request setup for send
    epoll_event event_settings;
    event_settings.events = EPOLLOUT;
    event_settings.data.fd = client_connection.client_fd;

    epoll_ctl(epoll_instance, EPOLL_CTL_MOD, client_connection.client_fd, &event_settings);

    // // mock code; remove it later
    // int execute_cgi_once = false;

    // // mock code below: cgi case
    // if (execute_cgi_once == true) {

    //     // remove this later
    //     client_connection.client_connection_type = CGI;

    //     // mock content below:
    //     client_connection.cgi_instance.cgi_command.cgi_type = INTERPRETED_LANGUAGE;
    //     client_connection.cgi_instance.cgi_command.interpreted_language_path = "/usr/bin/python";
    //     client_connection.cgi_instance.cgi_command.path_to_program =
    //         "./cgi-bin/sample_python_script.py";
    //     client_connection.cgi_instance.cgi_command.args.push_back("argument number 1");
    //     client_connection.cgi_instance.cgi_command.args.push_back("argument number 2");
    //     client_connection.cgi_instance.cgi_command.args.push_back("argument number 3");

    //     int cgi_fd = 0;

    //     try {
    //         cgi_fd = execute_cgi(client_connection.cgi_instance);
    //     } catch (std::exception& e) {
    //         std::cerr << e.what() << std::endl;
    //         fail_and_exit_with_message(-1, "We had an exception.");
    //     }

    //     cgi_fd_map.insert(std::make_pair(cgi_fd, this_fd));

    //     execute_cgi_once = false;
    // }
}
