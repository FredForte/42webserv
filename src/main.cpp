#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h> // to set fd to non-blocking
#include <iostream>
#include <netdb.h> // so we can have addrinfo struct
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h> // to have "accept()"

void fail_and_exit(int error_code) {
    std::exit(error_code);
}

void fail_and_exit_with_message(int error_code, const std::string& message) {
    std::cerr << "Error: " << message << std::endl;
    fail_and_exit(error_code);
}

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
    int setsockopt_return_value =
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value));

    if (setsockopt_return_value == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    int bind_result = bind(socket_fd, result_struct->ai_addr, result_struct->ai_addrlen);

    if (bind_result == -1) {
        fail_and_exit_with_message(1,
                                   std::string("Failed to bind socket: ") + std::strerror(errno));
    }

    return socket_fd;
}

int main(int argc, char** argv) {

    (void) argc;
    (void) argv;

    // if (argc == 1) {
    //     std::exit(1);
    // }

    int listen_fd = return_a_fully_prepared_socket("8080");

    int listen_result = listen(listen_fd, 5);

    if (listen_result == -1) {
        fail_and_exit_with_message(1, std::strerror(errno));
    }

    sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);

    // Creates an epoll instance, and avoids leaking its instance by using the only valid flag
    // available: "EPOLL_CLOEXEC". This flags instructs the closure of this instance to close
    // itself if the process running changes when using the exec() function. If you pass 0, it
    // won't close.
    int epoll_instance = epoll_create1(EPOLL_CLOEXEC);

    epoll_event event_settings;
    event_settings.events = EPOLLIN;    // me avisa quando tiver dado pra ler
    event_settings.data.fd = listen_fd; // quando o evento voltar, quero saber qual fd é

    epoll_ctl(epoll_instance, EPOLL_CTL_ADD, listen_fd, &event_settings);

    const unsigned int BUFFER_SIZE = 8;
    char* our_buffer = new char[BUFFER_SIZE]();
    int debug_how_many_read_calls_necessary = 0;

    epoll_event ready_events[64];

    while (true) {
        int n = epoll_wait(epoll_instance, ready_events, 64, -1);

        for (int i = 0; i < n; i++) {

            int this_fd = ready_events[i].data.fd;

            if (this_fd == listen_fd) {
                int fd_to_add =
                    accept(listen_fd, reinterpret_cast<sockaddr*>(&their_addr), &addr_size);

                if (fd_to_add == -1) {
                    fail_and_exit_with_message(1, std::strerror(errno));
                }

                int flags = fcntl(fd_to_add, F_GETFL);
                fcntl(fd_to_add, F_SETFL, flags | O_NONBLOCK);

                epoll_event event_settings;
                event_settings.events = EPOLLIN;
                event_settings.data.fd = fd_to_add;

                epoll_ctl(epoll_instance, EPOLL_CTL_ADD, fd_to_add, &event_settings);
                // this_fd = fd_to_add;
                continue;
            }

            if (ready_events[i].events & EPOLLIN && this_fd != listen_fd) {

                memset(our_buffer, 0, BUFFER_SIZE);
                int recv_result = recv(this_fd, our_buffer, BUFFER_SIZE, 0);

                if (recv_result == 0) {

                    epoll_ctl(epoll_instance, EPOLL_CTL_DEL, this_fd, NULL);
                    std::cout << "The client dropped the connection!\n\n";
                    std::cout << "Total bytes read: " << debug_how_many_read_calls_necessary
                              << std::endl;
                    debug_how_many_read_calls_necessary = 0;
                }

                if (recv_result == -1) {
                    fail_and_exit_with_message(1, std::strerror(errno));
                }

                debug_how_many_read_calls_necessary += recv_result;

                std::cout << our_buffer;
            }

            // if (ready_events[i].events & EPOLLOUT) {
            //     // fd tem espaço pra escrever
            // }
        }
    }

    delete[] our_buffer;
}
