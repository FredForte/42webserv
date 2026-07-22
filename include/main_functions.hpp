#ifndef MAIN_FUNCTIONS_HPP
#define MAIN_FUNCTIONS_HPP

#include "../include/client_connection.hpp"
#include "../include/parser/ConfigParser.hpp"
#include <map>
#include <sys/epoll.h>

void new_connections_func(int epoll_instance, epoll_event& event_settings, int this_fd,
                          std::map<int, int>& listening_fd_to_port,
                          std::map<int, int>& client_fd_to_port);

void standard_connections_func(
    int this_fd, const unsigned int BUFFER_SIZE, char* our_buffer, int epoll_instance,
    std::multimap<int, ServerConfig*>& port_as_int_to_server_config_ptr_mmap,
    std::map<int, client_connection_struct>& client_map, std::map<int, int>& client_fd_to_port,
    std::map<int, int>& cgi_fd_map, char* this_bin_path_from_argv);

#endif
