#include <cstdlib>
#include <sys/epoll.h>
#include <sys/socket.h>

// socket function call
// int socket(int domain, int type, int protocol);

int main(int argc, char** argv) {

    if (argc == 1) {
        std::exit(1);
    }

    // Creates an epoll instance, and avoids leaking its instance by using the only valid flag
    // available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to close itself
    // if the process running changes when using the exec() function. If you pass 0, it won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);
}
