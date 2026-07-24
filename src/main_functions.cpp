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
#include <unistd.h>

void new_connections_func(int epoll_instance, epoll_event& event_settings, int this_fd,
                          std::map<int, int>& listening_fd_to_port,
                          std::map<int, int>& client_fd_to_port) {

    sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);

    int new_client_fd = accept(this_fd, reinterpret_cast<sockaddr*>(&their_addr), &addr_size);

    if (new_client_fd == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    std::map<int, int>::iterator this_client_fd_origin_port = listening_fd_to_port.find(this_fd);

    if (this_client_fd_origin_port == listening_fd_to_port.end()) {
        fail_and_exit_with_message(
            -1,
            std::string("Why the hell this listening fd doesn't have a port associated to it?"));
    }

    // to guarantee we are overriding the fd's recycled by the kernel
    // we override if a stale fd was inside our control.
    client_fd_to_port[new_client_fd] = this_client_fd_origin_port->second;

    make_fd_non_blocking(new_client_fd);

    event_settings.events = EPOLLIN;
    event_settings.data.fd = new_client_fd;

    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, new_client_fd, &event_settings) == -1) {
        fail_and_exit_with_message(
            -1, std::string("Failed to modify epoll_instance with \"epoll_ctl()\" function: ")
                    + std::strerror(errno));
    }

    // isso mova de lugar
    // ServerConfig* server_config_ptr =
    //     get_server_config_instance_based_on_port_and_hostname(server_config_vec, this_fd);

    // client_connection_struct client_connection;
    // client_connection.client_fd = fd_to_add;
    // client_connection.ready_to_respond = false;
    // client_connection.close_after_response = false;
    // client_connection.client_connection_type = STANDARD;
    // client_connection.cgi_instance.client_fd = fd_to_add;
    // client_connection.cgi_instance.cgi_fd = 0;
    // client_connection.cgi_instance.epoll_instance = epoll_instance;

    // std::stringstream ss;
    // ss << client_connection.client_fd;
    // client_connection.cookie_id = ss.str();
    // client_connection.ServerConfig_ptr = server_config_ptr; // isso mova de lugar

    // client_map.insert(std::make_pair(fd_to_add, client_connection));

    // fd_to_ServerConfig_ptr_map.insert(std::make_pair(fd_to_add, server_config_ptr));
}

// 413 Payload Too Large, mark the connection to close once it's sent.
static void reject_with_413(int epoll_instance, client_connection_struct& client) {
    queue_error_response(epoll_instance, client, 413);
    client.close_after_response = true;
}

void standard_connections_func(int this_fd, const unsigned int BUFFER_SIZE, char* our_buffer,
                               int epoll_instance,
                               std::multimap<int, ServerConfig*>& port_to_server_config_ptr_mmap,
                               std::map<int, client_connection_struct>& client_map,
                               std::map<int, int>& client_fd_to_port,
                               std::map<int, int>& cgi_fd_map, char* this_bin_path_from_argv) {

    memset(our_buffer, 0, BUFFER_SIZE);
    int bytes_read = recv(this_fd, our_buffer, BUFFER_SIZE, 0);

    // client closed connection
    if (bytes_read <= 0) {

        epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL);
        client_map.erase(this_fd);
        close(this_fd);

        std::cout << "The client dropped the connection!\n\n";
        return;
    }

    std::map<int, client_connection_struct>::iterator it = client_map.find(this_fd);

    client_connection_struct a_client_connection;
    client_connection_struct* client_connection_ptr = NULL;

    if (it == client_map.end()) {
        a_client_connection.client_fd = this_fd;
        a_client_connection.ready_to_respond = false;
        a_client_connection.close_after_response = false;
        a_client_connection.client_connection_type = STANDARD;
        a_client_connection.cgi_instance.client_fd = this_fd;
        a_client_connection.cgi_instance.cgi_fd = 0;
        a_client_connection.cgi_instance.epoll_instance = epoll_instance;

        std::stringstream ss;
        ss << a_client_connection.client_fd;
        a_client_connection.cookie_id = ss.str();

		// pre cache the client_max_body_size from this port's default server
		// will re-resolve further down the line when a full request is present.
        std::map<int, int>::iterator port_it = client_fd_to_port.find(this_fd);
        if (port_it != client_fd_to_port.end()) {
            std::multimap<int, ServerConfig*>::iterator srv_it =
                port_to_server_config_ptr_mmap.find(port_it->second);
            if (srv_it != port_to_server_config_ptr_mmap.end()) {
                a_client_connection.ServerConfig_ptr = srv_it->second;
            }
        }

        std::pair<std::map<int, client_connection_struct>::iterator, bool> result_pair =
            client_map.insert(std::make_pair(this_fd, a_client_connection));

        if (result_pair.second == false) {
            fail_and_exit_with_message(-1, "Why inserting a client to a map failed?");
        }

        client_connection_ptr = &result_pair.first->second;
    }

    if (client_connection_ptr == NULL) {
        client_connection_ptr = &it->second;
    }

    client_connection_struct& client_connection = *client_connection_ptr;

    std::cout.write(our_buffer, bytes_read);
    client_connection.input_buffer.append(our_buffer, bytes_read);

    HttpRequestParser req_parser;

	// use the host of the first found instance of port in order to use this
	// max body size at this stage. 
	// reassigned to the resolved value when full request is parsed.
    size_t max_body = client_connection.ServerConfig_ptr->client_max_body_size;

    size_t length = req_parser.completeRequestLength(client_connection.input_buffer);
    if (length == std::string::npos) {
        // check the receivd body size, preventing it from exceeding whats set on
        // conf file, also adds a header size allowance with a macro.
        const size_t HEADER_ALLOWANCE = 16384;
        if (max_body > 0 && client_connection.input_buffer.size() > max_body + HEADER_ALLOWANCE) {
            reject_with_413(epoll_instance, client_connection);
        }
        return;
    }

    // create and save request structure
    HttpRequest request;
    request = req_parser.parse(client_connection.input_buffer.substr(0, length));

	// with the request parsed, we resolve which server block on this port should serve it
	// does not return null, it falls back to the default server port
    client_connection.ServerConfig_ptr = get_server_config_instance_based_on_port_and_hostname(
        this_fd, request, client_fd_to_port, port_to_server_config_ptr_mmap);

	// now with the full request values, we can reassign the final
	// max body size for this server config.
    max_body = client_connection.ServerConfig_ptr->client_max_body_size;

    client_connection.input_buffer.clear();
    client_connection.request_data = request;

    // get connection type from parsed request, defaults by the HTTP version standards
    // if not defined on the request
    client_connection.close_after_response = isCloseConnection(determineConnection(request));

    // Exact body-size enforcement now that the full request is decoded.
    if (max_body > 0 && request.body.size() > max_body) {
        reject_with_413(epoll_instance, client_connection);
        return;
    }

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
            memset(&event_settings, 0, sizeof(event_settings));
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

        // path_to_program is an owned std::string now, so it stays valid for the
        // life of the connection instead of dangling after this function returns.
        client_connection.cgi_instance.cgi_command.path_to_program =
            joinPath(this_bin_path_from_argv_cpp_str, request.path);

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
    memset(&event_settings, 0, sizeof(event_settings));
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
