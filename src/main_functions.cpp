#include "../include/client_connection.hpp"
#include "../include/parser/HttpRequestParser.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/response/response_handlers.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/main_functions_utils.hpp"
#include "../include/utils/utils_config_file.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
}

// 413 Payload Too Large, mark the connection to close once it's sent.
static void reject_with_413(int epoll_instance, client_connection_struct& client) {
    // Flagged before the response is built, so it goes out carrying
    // "Connection: close" - we are rejecting mid-body and the rest of that body
    // is still on its way, so this connection cannot be reused.
    client.close_after_response = true;
    queue_error_response(epoll_instance, client, 413);
    // nothing more will be framed out of what's buffered; drop the half-read
    // request's state so it can't bleed into anything read afterwards
    client.input_buffer.clear();
    client.framing.reset();
}

// Pulls the request target out of a raw, still-incomplete request so the body
// limit can be resolved per location before the body has finished arriving.
// The request line is "METHOD SP TARGET SP VERSION", and the query string is
// dropped because location matching only looks at the path.
// Returns "" when the request line isn't fully buffered yet.
static std::string peekRequestPath(const std::string& buffer) {
    size_t line_end = buffer.find("\r\n");
    if (line_end == std::string::npos) {
        return "";
    }

    size_t path_start = buffer.find(' ');
    if (path_start == std::string::npos || path_start > line_end) {
        return "";
    }
    path_start++;

    size_t path_end = buffer.find(' ', path_start);
    if (path_end == std::string::npos || path_end > line_end) {
        return "";
    }

    std::string path = buffer.substr(path_start, path_end - path_start);

    size_t query = path.find('?');
    if (query != std::string::npos) {
        path = path.substr(0, query);
    }

    return path;
}

void standard_connections_func(int this_fd, const unsigned int BUFFER_SIZE, char* our_buffer,
                               int epoll_instance,
                               std::multimap<int, ServerConfig*>& port_to_server_config_ptr_mmap,
                               std::map<int, client_connection_struct>& client_map,
                               std::map<int, int>& client_fd_to_port,
                               std::map<int, int>& cgi_fd_map) {

    // No memset: recv() reports exactly how many bytes it wrote and only that
    // many are ever appended below. Zeroing megabytes before each read would
    // cost more than the read.
    int bytes_read = recv(this_fd, our_buffer, BUFFER_SIZE, 0);

    // client closed connection. Anything it had in flight goes with it - most
    // importantly a running cgi, whose child and pipe would otherwise outlive the
    // only connection interested in the result.
    if (bytes_read <= 0) {

        drop_client(epoll_instance, client_map, cgi_fd_map, this_fd);

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
        a_client_connection.output_sent = 0;
        a_client_connection.cgi_instance.epoll_instance = epoll_instance;
        // the constructor already parks the slot at "nothing running" (-1s); leave
        // it that way rather than writing 0, which is a real descriptor
        resetCgiInstance(a_client_connection.cgi_instance);

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

    // std::cout.write(our_buffer, bytes_read);
    client_connection.input_buffer.append(our_buffer, bytes_read);

    HttpRequestParser req_parser;

	// use the host of the first found instance of port in order to use this
	// max body size at this stage.
	// the location can already be matched off the (partial) request line, so a
	// location-level override applies even while the body is still streaming in.
	// both get re-resolved against the real server block once the request is complete.
    HttpRequest partial_request;
    partial_request.path = peekRequestPath(client_connection.input_buffer);
    size_t max_body = resolveMaxBodySize(
        *client_connection.ServerConfig_ptr,
        partial_request.path.empty()
            ? NULL
            : findRequestedLocation(*client_connection.ServerConfig_ptr, partial_request));

    size_t length = req_parser.completeRequestLength(client_connection.input_buffer,
                                                     client_connection.framing);
    if (length == std::string::npos) {
        if (max_body > 0
            && client_connection.input_buffer.find("\r\n\r\n") != std::string::npos) {
            size_t cl_pos = client_connection.input_buffer.find("Content-Length:");
            if (cl_pos == std::string::npos)
                cl_pos = client_connection.input_buffer.find("content-length:");
            if (cl_pos != std::string::npos) {
                size_t val_start = cl_pos + 15;
                size_t declared =
                    static_cast<size_t>(std::atol(
                        client_connection.input_buffer.c_str() + val_start));
                if (declared > max_body) {
                    reject_with_413(epoll_instance, client_connection);
                    return;
                }
            }
        }
        const size_t HEADER_ALLOWANCE = 16384;
        if (max_body > 0 && client_connection.input_buffer.size() > max_body + HEADER_ALLOWANCE) {
            reject_with_413(epoll_instance, client_connection);
        }
        return;
    }

    // Parse straight into the connection's own request object, and refer to it
    // by reference from here on. The old form - parse into a local, then assign
    // that local into the connection - copied the entire body twice on top of the
    // copy substr() had already made of the whole buffer. For a 100MB upload that
    // was 300MB of memcpy before the request had even been dispatched.
    // No substr() either: the body is bounded by Content-Length (or by the
    // terminating chunk), so anything after this request is never picked up.
    HttpRequest& request = client_connection.request_data;
    req_parser.parseInto(client_connection.input_buffer, request);

	// with the request parsed, we resolve which server block on this port should serve it
	// does not return null, it falls back to the default server port
    client_connection.ServerConfig_ptr = get_server_config_instance_based_on_port_and_hostname(
        this_fd, request, client_fd_to_port, port_to_server_config_ptr_mmap);

    // The body has been copied out, so hand the read buffer's memory back rather
    // than just resetting its length - clear() would keep a 100MB allocation
    // reserved for as long as this connection stays open. Small buffers are left
    // alone so ordinary keep-alive traffic keeps reusing one allocation.
    if (client_connection.input_buffer.capacity() > 64 * 1024) {
        std::string().swap(client_connection.input_buffer);
    } else {
        client_connection.input_buffer.clear();
    }
    // this request is now fully sliced off, so the next one on this keep-alive
    // connection has to be framed from scratch
    client_connection.framing.reset();

    // get connection type from parsed request, defaults by the HTTP version standards
    // if not defined on the request
    client_connection.close_after_response = isCloseConnection(determineConnection(request));

    // find location to determine request type
    LocationConfig* responseLocation =
        findRequestedLocation(*client_connection.ServerConfig_ptr, request);

	// now with the full request values, we can reassign the final max body size:
	// the matched location's override wins over the resolved server block's value.
    max_body = resolveMaxBodySize(*client_connection.ServerConfig_ptr, responseLocation);

    // Exact body-size enforcement now that the full request is decoded.
    if (max_body > 0 && request.body.size() > max_body) {
        reject_with_413(epoll_instance, client_connection);
        return;
    }

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

        // cgi_instance lives for the whole connection, so a keep-alive client
        // reuses the same struct for every request it makes. The collected output
        // of the previous run is still in there; without this reset the next run
        // appends to it and the client gets the old body (plus the new run's raw
        // CGI header block) glued in front of its response.
        client_connection.cgi_instance.cgi_response.clear();

        // binary executable does not use path
        client_connection.cgi_instance.cgi_command.cgi_type = INTERPRETED_LANGUAGE;
        if (it->second.empty()) {
            client_connection.cgi_instance.cgi_command.cgi_type = BINARY;
        }

        // fills cgi_command block:
        client_connection.cgi_instance.cgi_command.interpreted_language_path = it->second.c_str();

		// resolve script path, stripping the locaton prefix and joining the remainder with
		// the location's set root.
        client_connection.cgi_instance.cgi_command.path_to_program =
            resolveLocalPath(*responseLocation, request.path);

        // same resolved on-disk path used to exec the script, so PATH_INFO /
        // SCRIPT_FILENAME / PATH_TRANSLATED point at the real file location.
        client_connection.cgi_instance.cgi_command.envp =
            buildCgiEnv(request, *client_connection.ServerConfig_ptr,
                        client_connection.cgi_instance.cgi_command.path_to_program);

        client_connection.cgi_instance.epoll_instance = epoll_instance;

        // executing cgi
        int cgi_fd = 0;

        try {
            cgi_fd = execute_cgi(client_connection.cgi_instance, request.body);
        } catch (std::exception& e) {
            // a failed cgi answer with a 500 and keep serving. execute_cgi may have
            // got as far as forking before it threw, so park the slot back at
            // "nothing running" instead of leaving a pid it already reaped.
            std::cerr << e.what() << std::endl;
            resetCgiInstance(client_connection.cgi_instance);
            queue_error_response(epoll_instance, client_connection, 500);
            return;
        }

        // execute_cgi has staged the body into the child's stdin temp file, so the
        // in-memory copy is dead weight from here on - and it would otherwise sit
        // there for the whole run, alongside the output the child is producing.
        std::string().swap(request.body);

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
}
