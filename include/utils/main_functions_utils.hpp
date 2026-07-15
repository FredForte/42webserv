#ifndef MAIN_FUNCTIONS_UTILS_HPP
#define MAIN_FUNCTIONS_UTILS_HPP

#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"
#include "../../include/program_flow_utils.hpp"
#include <map>

bool is_this_a_cgi_fd(const std::map<int, int>& cgi_fd_map, int this_fd);

// Can throw exception
client_connection_struct*
get_client_instance_based_on_cgi_fd(const std::map<int, int>& cgi_fd_map,
                                    std::map<int, client_connection_struct>& client_map,
                                    int this_fd);

bool is_this_a_listen_fd(std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref,
                         int this_fd);

// Can throw exception
ServerConfig* get_server_config_instance_based_on_listen_fd(
    std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref, int this_fd);

#endif
