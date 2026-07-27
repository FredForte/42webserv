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

bool is_this_a_listen_fd(std::map<int, int>& port_to_listening_fd, int this_fd);

// Can hard-crash
ServerConfig* get_server_config_instance_based_on_port_and_hostname(
    int this_fd, HttpRequest& http_request, std::map<int, int>& client_fd_to_port,
    std::multimap<int, ServerConfig*>& port_to_server_config_ptr_mmap);

// fully resets a cgi instance, always use with detach_cgi_fd().
void resetCgiInstance(cgi_instance_struct& cgi);

// fully removes and reaps a running CGI from the event loop.
void detach_cgi_fd(int epoll_instance, cgi_instance_struct& cgi, std::map<int, int>& cgi_fd_map,
                   bool kill_child);

// fully terminate a client, kills any cgi, then unregisters, closes and clear socket.
void drop_client(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                 std::map<int, int>& cgi_fd_map, int client_fd);

// turns a finished cgi run into a clients response,
// 502 when cgi reported failure.
// detaches and resets the slot in all cases.
void complete_cgi_request(int epoll_instance, client_connection_struct& client,
                          std::map<int, int>& cgi_fd_map, pid_t reaped, int status);

// run once per event-loop wakeup:
//  - kill, reap and answer 504 for over cgi_timeout;
//  - finish runs whose stdout already closed but whose child kept hanging.
void reap_timed_out_cgis(int epoll_instance, std::map<int, client_connection_struct>& client_map,
                         std::map<int, int>& cgi_fd_map);

#endif
