#include "../../include/utils/main_functions_utils.hpp"
#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"
#include "../../include/response/response_handlers.hpp"
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

    std::map<std::string, std::string>::iterator it = http_request.headers.find("HOST");

    if (it == http_request.headers.end()) {
        return NULL;
    }

    std::string& host_name = it->second;

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

    std::multimap<int, ServerConfig*>::iterator i = port_to_server_it.first;

    for (; i != port_to_server_config_ptr_mmap.end(); i++) {

        if (i->second->server_name == host_name) {
            return i->second;
        }
    }

    return NULL;
}
// collect the timedout cgi processess
// then kill and readp them using SIGKILL.
// waitpid shouldnt lock the server because the process is done
// after killing it.
// then queue an error response with code 504 for request timedout.
void reap_timed_out_cgis(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                         std::map<int, int>& cgi_fd_map) {
    time_t now = time(NULL);

    std::vector<int> expired_cgi_fds;
    for (std::map<int, int>::iterator it = cgi_fd_map.begin(); it != cgi_fd_map.end(); ++it) {
        std::map<int, client_connection_struct>::iterator client_it = client_map.find(it->second);
        if (client_it == client_map.end()) {
            continue;
        }
        cgi_instance_struct& cgi = client_it->second.cgi_instance;
        if (cgi.timeout_seconds > 0
            && now - cgi.start_time >= static_cast<time_t>(cgi.timeout_seconds)) {
            expired_cgi_fds.push_back(it->first);
        }
    }

    for (size_t i = 0; i < expired_cgi_fds.size(); ++i) {
        int cgi_fd = expired_cgi_fds[i];
        client_connection_struct& client = client_map[cgi_fd_map[cgi_fd]];

        kill(client.cgi_instance.cgi_pid, SIGKILL);
        waitpid(client.cgi_instance.cgi_pid, NULL, 0);

        epoll_ctl(epoll_instance, EPOLL_CTL_DEL, cgi_fd, NULL);
        close(cgi_fd);
        cgi_fd_map.erase(cgi_fd);

        queue_error_response(epoll_instance, client, 504);
    }
}
