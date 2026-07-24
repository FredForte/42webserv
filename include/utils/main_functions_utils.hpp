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

bool is_this_a_listen_fd(std::map<int, int>& port_to_listening_fd, int this_fd);

// Can hard-crash
ServerConfig* get_server_config_instance_based_on_port_and_hostname(
    int this_fd, HttpRequest& http_request, std::map<int, int>& client_fd_to_port,
    std::multimap<int, ServerConfig*>& port_to_server_config_ptr_mmap);

// kills, reaps, and responds 504 to any running cgi that has exceeded its
// location's cgi_timeout.
// called once per event-loop wakeup so a cgi that never
// produces output (or never exits) can't hang its client or run forever.
void reap_timed_out_cgis(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                         std::map<int, int>& cgi_fd_map);

#endif
