#include <stdexcept>
#include <map>
#include <vector>
#include <csignal>
#include <ctime>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"
#include "../../include/response/response_handlers.hpp"
#include "../../include/utils/main_functions_utils.hpp"

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

bool is_this_a_listen_fd(std::set<int>& listen_fds_created, int this_fd) {
    return listen_fds_created.count(this_fd);
}

ServerConfig* get_server_config_instance_based_on_port_and_hostname(
    std::vector<ServerConfig>& server_config_vec, int this_fd, HttpRequest http_request) {

    std::map<std::string, std::string>::iterator it = http_request.headers.find("HOST");

    if (it == http_request.headers.end()) {
        return NULL;
    }

    std::map<std::string, std::string>::iterator it = http_request.headers.

    if (it == http_request.headers.end()) {
        return NULL;
    }

    std::string& host_name = it->second;

    bool port_found = false;
    size_t server_config_vec_size = server_config_vec.size();
    for (size_t i = 0; i < server_config_vec_size; i++) {

        size_t listens_size = server_config_vec[i].listens.size();
        for (size_t j = 0; j < listens_size; j++) {

            if (ports_created.count(server_config_vec[i].listens[j].port)) {
                continue;
            }

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

            ports_created.insert(server_config_vec[i].listens[j].port);
            listen_fds_created.insert(listen_fd_instance);
        }
    }
}
// collect the timedout cgi processess
// then kill and readp them using SIGKILL.
// waitpid shouldnt lock the server because the process is done
// after killing it.
// then queue an error response with code 504 for request timedout.
void reap_timed_out_cgis(int epoll_instance,
                         std::map<int, client_connection_struct>& client_map,
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
