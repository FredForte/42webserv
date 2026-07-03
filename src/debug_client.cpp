#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h> // so we can have addrinfo struct
// #include <sys/epoll.h>
// #include <sys/socket.h>
// #include <sys/types.h> // to have "accept()"

int main() {

    struct addrinfo hints, *addr_result;
    int socket_fd;

    // first, load up results structs with getaddrinfo():

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo("localhost", "6666", &hints, &addr_result);

    // make a socket:

    socket_fd = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);

    // connect!

    connect(socket_fd, addr_result->ai_addr, addr_result->ai_addrlen);

    const char* our_message = "We came in peace";

    int send_result = send(socket_fd, our_message, std::strlen(our_message), 0);

    if (send_result == -1) {
        std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
        std::exit(1); // temporarily
    }

    return 0;
}
