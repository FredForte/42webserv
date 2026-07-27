#include "../../include/utils/main_functions_utils.hpp"
#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"
#include "../../include/response/response_handlers.hpp"
#include "../../include/utils/utils_config_file.hpp"
#include <csignal>
#include <ctime>
#include <map>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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

bool is_this_a_listen_fd(std::map<int, int>& port_to_listening_fd, int this_fd) {
    return port_to_listening_fd.count(this_fd);
}

ServerConfig* get_server_config_instance_based_on_port_and_hostname(
    int this_fd, HttpRequest& http_request, std::map<int, int>& client_fd_to_port,
    std::multimap<int, ServerConfig*>& port_to_server_config_ptr_mmap) {

    // The port the client connected on is the other half of the virtual-host
    // key; it was recorded against the client fd when we accepted it.
    std::map<int, int>::iterator client_fd_port_it = client_fd_to_port.find(this_fd);

    if (client_fd_port_it == client_fd_to_port.end()) {
        fail_and_exit_with_message(-1, "Why this client fd doesn't have an associated port?");
    }

    int client_fd_port = client_fd_port_it->second;
    std::pair<std::multimap<int, ServerConfig*>::iterator,
              std::multimap<int, ServerConfig*>::iterator>
        port_to_server_it = port_to_server_config_ptr_mmap.equal_range(client_fd_port);

    if (port_to_server_it.first == port_to_server_config_ptr_mmap.end()) {
        fail_and_exit_with_message(-1,
                                   "Why this port is not associated to a ServerConfig pointer?");
    }

    // default server for this port if none found (nginx similar)
    ServerConfig* default_server = port_to_server_it.first->second;

    std::map<std::string, std::string>::iterator it = http_request.headers.find("host");

    if (it == http_request.headers.end()) {
        return default_server;
    }

    // the Host header is "name" or "name:port"; match on the name alone.
    std::string host_name = it->second;
    std::string::size_type colon = host_name.find(':');
    if (colon != std::string::npos) {
        host_name = host_name.substr(0, colon);
    }

    // only look at the servers bound to this port (stop at the range end, not
    // the map end, or we'd match a server_name from a different port).
    for (std::multimap<int, ServerConfig*>::iterator i = port_to_server_it.first;
         i != port_to_server_it.second; ++i) {

        if (i->second->server_name == host_name) {
            return i->second;
        }
    }

    return default_server;
}
void resetCgiInstance(cgi_instance_struct& cgi) {
    cgi.cgi_response.clear();
    cgi.client_fd = -1;
    cgi.cgi_fd = -1;
    cgi.cgi_pid = -1;
    cgi.start_time = 0;
    cgi.timeout_seconds = 0;
    cgi.stdout_closed = false;
    cgi.cgi_exit_code = 0;
}

void detach_cgi_fd(int epoll_instance, cgi_instance_struct& cgi, std::map<int, int>& cgi_fd_map,
                   bool kill_child) {
    if (kill_child && cgi.cgi_pid > 0) {
        kill(cgi.cgi_pid, SIGKILL);
        // SIGKILL makes the waitpid a fast process.
        waitpid(cgi.cgi_pid, NULL, 0);
        cgi.cgi_pid = -1;
    }

    if (cgi.cgi_fd >= 0) {
        epoll_ctl(epoll_instance, EPOLL_CTL_DEL, cgi.cgi_fd, NULL);
        close(cgi.cgi_fd);
        cgi_fd_map.erase(cgi.cgi_fd);
        cgi.cgi_fd = -1;
    }
}

void drop_client(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                 std::map<int, int>& cgi_fd_map, int client_fd) {
    std::map<int, client_connection_struct>::iterator it = client_map.find(client_fd);

    if (it != client_map.end()) {
        // reads if client dropped connection,
        // stops and clears the cgi process, because no one is going to receive it.
        detach_cgi_fd(epoll_instance, it->second.cgi_instance, cgi_fd_map, true);
        client_map.erase(it);
    }

    epoll_ctl(epoll_instance, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

void complete_cgi_request(int epoll_instance, client_connection_struct& client,
                          std::map<int, int>& cgi_fd_map, pid_t reaped, int status) {
    detach_cgi_fd(epoll_instance, client.cgi_instance, cgi_fd_map, false);

    // 502 only on confirmed error codes received, if status is not confirmed
    // we serve the output we have.
    bool cgi_failed = (reaped > 0) && (!WIFEXITED(status) || WEXITSTATUS(status) != 0);

    if (cgi_failed) {
        queue_error_response(epoll_instance, client, 502);
    } else {
        HttpResponse response = parseCgiResponse(client.cgi_instance.cgi_response,
                                                 *client.ServerConfig_ptr, client.request_data);
        queue_response(epoll_instance, client, response);
    }

    resetCgiInstance(client.cgi_instance);
}

void reap_timed_out_cgis(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                         std::map<int, int>& cgi_fd_map) {
    time_t now = time(NULL);

    std::vector<int> expired_cgi_fds;
    std::vector<int> awaiting_exit_cgi_fds;

    for (std::map<int, int>::iterator it = cgi_fd_map.begin(); it != cgi_fd_map.end(); ++it) {
        std::map<int, client_connection_struct>::iterator client_it = client_map.find(it->second);
        if (client_it == client_map.end()) {
            continue;
        }
        cgi_instance_struct& cgi = client_it->second.cgi_instance;

        if (cgi.timeout_seconds > 0
            && now - cgi.start_time >= static_cast<time_t>(cgi.timeout_seconds)) {
            expired_cgi_fds.push_back(it->first);
        } else if (cgi.stdout_closed) {
            awaiting_exit_cgi_fds.push_back(it->first);
        }
    }

    for (size_t i = 0; i < expired_cgi_fds.size(); ++i) {
        int cgi_fd = expired_cgi_fds[i];
        client_connection_struct& client = client_map[cgi_fd_map[cgi_fd]];

        detach_cgi_fd(epoll_instance, client.cgi_instance, cgi_fd_map, true);
        queue_error_response(epoll_instance, client, 504);
        resetCgiInstance(client.cgi_instance);
    }

    for (size_t i = 0; i < awaiting_exit_cgi_fds.size(); ++i) {
        int cgi_fd = awaiting_exit_cgi_fds[i];
        client_connection_struct& client = client_map[cgi_fd_map[cgi_fd]];

        int status = 0;
        pid_t reaped = waitpid(client.cgi_instance.cgi_pid, &status, WNOHANG);
        if (reaped == 0) {
            continue; // still running with stdout closed; the timeout will get it
        }

        complete_cgi_request(epoll_instance, client, cgi_fd_map, reaped, status);
    }
}
