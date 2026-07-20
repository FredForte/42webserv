#ifndef MAIN_FUNCTIONS_UTILS_HPP
#define MAIN_FUNCTIONS_UTILS_HPP

#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"
#include "../../include/program_flow_utils.hpp"
#include <map>
#include <set>

bool is_this_a_cgi_fd(const std::map<int, int>& cgi_fd_map, int this_fd);

// Can throw exception
client_connection_struct*
get_client_instance_based_on_cgi_fd(const std::map<int, int>& cgi_fd_map,
                                    std::map<int, client_connection_struct>& client_map,
                                    int this_fd);

bool is_this_a_listen_fd(std::set<int>& listen_fds_created, int this_fd);

// Can throw exception
ServerConfig* get_server_config_instance_based_on_port_and_hostname(
    std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref, int this_fd);

// kills, reaps, and responds 504 to any running cgi that has exceeded its
// location's cgi_timeout.
// called once per event-loop wakeup so a cgi that never
// produces output (or never exits) can't hang its client or run forever.
void reap_timed_out_cgis(int epoll_instance,
                         std::map<int, client_connection_struct>& client_map,
                         std::map<int, int>& cgi_fd_map);

#endif
