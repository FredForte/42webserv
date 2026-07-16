#include "../include/socket_utils.hpp"
#include "../include/program_flow_utils.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netdb.h> // so we can have addrinfo struct
#include <string>

#include <cerrno>
#include <fcntl.h> // to set fd to non-blocking

// Creates, set its options and bind the socket
int return_a_fully_prepared_socket(const char* PORT_NUMBER_TO_HOST) {
    addrinfo hint_addrinfo_struct;
    addrinfo* result_struct;

    memset(&hint_addrinfo_struct, 0, sizeof(hint_addrinfo_struct));

    hint_addrinfo_struct.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hint_addrinfo_struct.ai_socktype = SOCK_STREAM; // TCP, in this case. Not datagram a.k.a UDP
    hint_addrinfo_struct.ai_flags =
        AI_PASSIVE; // "AI_PASSIVE" as an argument results in the use of host machine IP

    getaddrinfo(NULL,                  // IP or domain name
                PORT_NUMBER_TO_HOST,   // Port number
                &hint_addrinfo_struct, // Base struct memsetted to zero to serve as hint
                &result_struct);       // Result struct generated

    int socket_fd =
        socket(result_struct->ai_family, result_struct->ai_socktype, result_struct->ai_protocol);

    if (socket_fd == -1) {
        fail_and_exit_with_message(1,
                                   std::string("Failed to create socket: ") + std::strerror(errno));
    }

    int option_value = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value))
        == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    if (bind(socket_fd, result_struct->ai_addr, result_struct->ai_addrlen) == -1) {
        fail_and_exit_with_message(1,
                                   std::string("Failed to bind socket: ") + std::strerror(errno));
    }

    return socket_fd;
}

void make_fd_non_blocking(int fd_to_add) {
    int flags = fcntl(fd_to_add, F_GETFL);

    if (flags == -1) {
        fail_and_exit_with_message(-1, std::string("Failed to acquire the file descriptor flags")
                                           + std::strerror(errno));
    }

    if (fcntl(fd_to_add, F_SETFL, flags | O_NONBLOCK) == -1) {
        fail_and_exit_with_message(-1,
                                   std::string("Failed to make the file descriptor non-blocking")
                                       + std::strerror(errno));
    }
}
