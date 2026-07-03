#include <cstdlib>
#include <sys/epoll.h>
#include <sys/socket.h>
// #include <sys/types.h>
#include <cstring>
#include <netdb.h> // addrinfo

// socket function call
// int socket(int domain, int type, int protocol);

int main(int argc, char** argv) {

    if (argc == 1) {
        std::exit(1);
    }

    struct addrinfo hints;
    struct addrinfo* result_struct;
    const char* PORT_NUMBER = "6666";

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // IP comes externally

    getaddrinfo(NULL, // IP or domain name
                PORT_NUMBER,
                &hints,          // you memset to zero so it get filled
                &result_struct); // our result

    // Creates an epoll instance, and avoids leaking its instance by using the only valid flag
    // available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to close
    // itself if the process running changes when using the exec() function. If you pass 0, it
    // won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);
}
