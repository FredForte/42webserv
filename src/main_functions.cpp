#include "../include/client_connection.hpp"
#include "../include/program_flow_utils.hpp"
#include "../include/socket_utils.hpp"
#include "../include/utils/main_functions_utils.hpp"
#include <cerrno>
#include <cstring>
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
                    + std::strerrod MAINr(errno));
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
    client_connection.ServerConfig_ptr = server_config_ptr;

    client_map.insert(std::make_pair(fd_to_add, client_connection));

    fd_to_ServerConfig_ptr_map.insert(std::make_pair(fd_to_add, server_config_ptr));
}
