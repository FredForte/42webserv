#ifndef SOCKET_UTILS_HPP
#define SOCKET_UTILS_HPP

int return_a_fully_prepared_socket(const char* PORT_NUMBER_TO_HOST);
void make_fd_non_blocking(int fd_to_add);

#endif
