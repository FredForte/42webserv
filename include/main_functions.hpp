#ifndef MAIN_FUNCTIONS_HPP
#define MAIN_FUNCTIONS_HPP

#include "../include/client_connection.hpp"
#include "../include/parser/ConfigParser.hpp"
#include <map>
#include <sys/epoll.h>

void new_connections_func(int epoll_instance, epoll_event& event_settings, int this_fd,
                          std::map<int, ServerConfig*>& listen_fd_to_server_config_map,
                          std::map<int, client_connection_struct>& client_map,
                          std::map<int, ServerConfig*>& fd_to_ServerConfig_ptr_map);

#endif
