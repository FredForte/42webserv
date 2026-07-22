# Networking (socket lifecycle)
    socket          - creates a raw fd — no address, no behavior, just an endpoint
    bind            - gives the socket an identity (IP + port)
    listen          - tells the kernel to accept connections on this socket and queue them (backlog)
    accept          - pulls the next connection from listen's queue, returns a NEW fd for it
    connect         - connects the socket to a remote address (client-side)
    send            - sends data through the socket
    recv            - receives data from the socket
    setsockopt      - sets socket options (e.g. SO_REUSEADDR to allow immediate port reuse after crash)
    getsockname     - returns the local address bound to the socket (e.g. discover OS-assigned port after bind to port 0)
    socketpair      - creates two anonymous bidirectional sockets already connected to each other (e.g. self-pipe trick to wake epoll from a signal handler)

    The listening socket is a door, not a conversation — it never handles
    client data. accept() exists because the TCP handshake happens in the
    kernel; you need it to retrieve connections that are already established.
    In event-driven servers, register the listening socket in poll/epoll and
    only call accept() when it signals activity.

    Flow: socket → bind → listen → poll/epoll → accept → new client fd

# Networking (resolution/conversion)
    getaddrinfo     - resolves hostname + service into socket address structs
    freeaddrinfo    - frees the linked list returned by getaddrinfo
    getprotobyname  - looks up a protocol number by name (e.g. "tcp" -> 6)
    htons           - converts a 16-bit value from host to network byte order
    htonl           - converts a 32-bit value from host to network byte order
    ntohs           - converts a 16-bit value from network to host byte order
    ntohl           - converts a 32-bit value from network to host byte order

# I/O multiplexing
    select          - monitors multiple fds for readiness, limited to FD_SETSIZE (1024)
    poll            - same purpose as select, no fd limit, uses pollfd array

    epoll variation:
        epoll_create    - creates an epoll instance, returns its fd
        epoll_ctl       - adds, modifies, or removes fds from the epoll watch list
        epoll_wait      - blocks until watched fds have events, returns only the active ones. Accept() comes after this if the socket is a listening one.

## BSD/MacOs specific functions
    kqueue (kqueue, kevent)

# Processes
    fork
    execve
    waitpid
    kill
    signal

# File descriptors (manipulation)
    dup
    dup2
    pipe
    fcntl
    close
    read
    write

# Filesystem
    open
    access
    stat
    chdir
    opendir
    readdir
    closedir

# Errors
    strerror        - returns a string describing an errno error code
    gai_strerror    - returns a string describing a getaddrinfo error code
               - global variable set by syscalls/library functions on failure
