#include <stdexcept>
#include <map>
#include "../../include/client_connection.hpp"
#include "../../include/parser/ConfigParser.hpp"

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

bool is_this_a_listen_fd(std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref,
                         int this_fd) {
    return listen_fd_to_server_config_map_ref.find(this_fd)
           != listen_fd_to_server_config_map_ref.end();
}

// Can throw exception
ServerConfig* get_server_config_instance_based_on_listen_fd(
    std::map<int, ServerConfig*>& listen_fd_to_server_config_map_ref, int this_fd) {

    std::map<int, ServerConfig*>::iterator it = listen_fd_to_server_config_map_ref.find(this_fd);

    if (it == listen_fd_to_server_config_map_ref.end()) {
        throw std::runtime_error("Should't this fd be a listen fd one?!");
    }

    return it->second;
}
