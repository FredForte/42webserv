#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h> // so we can have addrinfo struct
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> // to have "accept()"
#include <sys/epoll.h>

// socket function call
// int socket(int domain, int type, int protocol);

int main(int argc, char** argv) {

    (void)  argc;
    (void) argv;

    // if (argc == 1) {
    //     std::exit(1);
    // }

    struct addrinfo hints;
    struct addrinfo* result_struct;
    const char* PORT_NUMBER = "6666";

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP, in this case
    hints.ai_flags = AI_PASSIVE;     // AI_PASSIVE results in a IP of the host machine

    getaddrinfo(NULL, // IP or domain name
                PORT_NUMBER,
                &hints,          // you memset to zero so it get filled
                &result_struct); // our result

    int socket_fd =
        socket(result_struct->ai_family, result_struct->ai_socktype, result_struct->ai_protocol);

    if (socket_fd == -1) {
        std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
        std::exit(1); // temporarily
    }

    int yes = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    int bind_result = bind(socket_fd, result_struct->ai_addr, result_struct->ai_addrlen);

    if (bind_result == -1) {
        std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
        std::exit(1); // temporarily
    }

    // int connect_result = connect(socket_fd, result_struct->ai_addr, result_struct->ai_addrlen);

    // if (connect_result == -1) {
    //     std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
    //     std::exit(1); // temporarily
    // }

    int listen_result = listen(socket_fd, 5);

    if (listen_result == -1) {
        std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
        std::exit(1); // temporarily
    }

    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);

    int new_fd = accept(socket_fd, reinterpret_cast<struct sockaddr*>(&their_addr),
                        &addr_size);

    if (new_fd == -1) {
        std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
        std::exit(1); // temporarily
    }


    // Creates an epoll instance, and avoids leaking its instance by using the only valid flag
    // available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to close
    // itself if the process running changes when using the exec() function. If you pass 0, it
    // won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);

    struct epoll_event event;
    event.events = EPOLLIN;    // me avisa quando tiver dado pra ler
    event.data.fd = new_fd;   // quando o evento voltar, quero saber qual fd é
    epoll_ctl(epoll_instance, EPOLL_CTL_ADD, new_fd, &event);

    const unsigned int BUFFER_SIZE = 1024;

    char* our_buffer = new char[BUFFER_SIZE]();

    epoll_event ready_events[64];

    while(true) {
        int n = epoll_wait(epoll_instance, ready_events, 64, -1);

        for (int i = 0; i < n; i++) {

            int fd = ready_events[i].data.fd;

            if (ready_events[i].events & EPOLLIN) {

                memset(our_buffer, 0, 1024);

                int recv_result = recv(fd, our_buffer, BUFFER_SIZE, 0);

                if (recv_result == 0) {
                    std::cout << "The client dropped the connection!" << std::endl;
                    std::exit(0); // temporarily
                }

                if (recv_result == -1) {
                    std::cerr << "We had an error: \"" << std::strerror(errno) << "\"" << std::endl;
                    std::exit(1); // temporarily
                }

                std::cout << our_buffer;
                // std::cout << our_buffer << std::endl;
            }

            // if (ready_events[i].events & EPOLLOUT) {
            //     // fd tem espaço pra escrever
            // }
        }
    }

    delete[] our_buffer;
}
