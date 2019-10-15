#include <cstring>
#include <unistd.h>
#include <stdint.h>
#include <cmath>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "communication.h"

static void copy_cmd_and_fill(char *cmd, const std::string &str) {

    for (uint16_t i = 0; i < CMD_MAX_LENGTH; i++) {
        if (i < str.length()) {
            cmd[i] = str[i];
        } else {
            cmd[i] = '\0';
        }
    }
}

simpl_cmd::simpl_cmd() {

    memset(&(this->cmd), '\0', CMD_MAX_LENGTH);
    this->cmd_seq = 0;
    memset(&(this->data), '\0', SIMPL_CMD_MAX_DATA_LENGTH + 1);
}

simpl_cmd::simpl_cmd(const std::string &cmd, uint64_t cmd_seq, const char *data) {

    copy_cmd_and_fill(this->cmd, cmd);
    this->cmd_seq = cmd_seq;
    strncpy(this->data, data, SIMPL_CMD_MAX_DATA_LENGTH);
}

cmplx_cmd::cmplx_cmd() {

    memset(&(this->cmd), '\0', CMD_MAX_LENGTH);
    this->cmd_seq = 0;
    this->param = 0;
    memset(&(this->data), '\0', CMPLX_CMD_MAX_DATA_LENGTH + 1);
}

cmplx_cmd::cmplx_cmd(const std::string &cmd, uint64_t cmd_seq, uint64_t param, const char *data) {

    copy_cmd_and_fill(this->cmd, cmd);
    this->cmd_seq = cmd_seq;
    this->param = param;
    strncpy(this->data, data, CMPLX_CMD_MAX_DATA_LENGTH);
}

bool UDP_socket::init_standard_socket() {

    if ((this->socket_number = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return false;
    }
    sockaddr_in local_addr {};
    socklen_t len = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr));
    if (getsockname(this->socket_number, (sockaddr*)&local_addr, &len) < 0) {
        return false;
    }
    this->closed = false;
    return true;
}

bool UDP_socket::init_multicast_socket() {

    if ((this->socket_number = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return false;
    }
    int optval = 1;
    if (setsockopt(this->socket_number, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof(optval)) < 0) {
        return false;
    }
    optval = TTL;
    this->closed = false;
    return (setsockopt(this->socket_number, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &optval, sizeof(optval)) >= 0);
}

bool UDP_socket::bind_to_specific_port(in_port_t port) {

    sockaddr_in addr{};
    addr.sin_port = port;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    return !(bind(this->socket_number, (sockaddr *)&addr, sizeof(addr)) < 0);
}

bool UDP_socket::send_simpl_cmd(const simpl_cmd& command, const sockaddr_in &addr, uint16_t data_len) {

    return (sendto(this->socket_number, &command, EMPTY_SIMPL_CMD_LENGTH + data_len, 0,
                  (sockaddr*)&addr, sizeof(addr)) == EMPTY_SIMPL_CMD_LENGTH + data_len);
}

bool UDP_socket::send_simpl_cmd_by_ip(const simpl_cmd &command, const std::string &ip, in_port_t port, uint16_t data_len) {

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    return !((inet_aton(ip.c_str(), &addr.sin_addr) == 0) ||
             (sendto(this->socket_number, &command, EMPTY_SIMPL_CMD_LENGTH + data_len, 0, (sockaddr*)&addr,
                     sizeof(addr)) != EMPTY_SIMPL_CMD_LENGTH + data_len));
}

bool UDP_socket::send_cmplx_cmd(const cmplx_cmd &command, const sockaddr_in &addr, uint16_t data_len) {

    return (sendto(this->socket_number, &command, EMPTY_CMPLX_CMD_LENGTH + data_len, 0, (sockaddr*)&addr,
                  sizeof(addr)) == EMPTY_CMPLX_CMD_LENGTH + data_len);
}

bool UDP_socket::send_cmplx_cmd_by_ip(const cmplx_cmd& command, const std::string& ip, in_port_t port, uint16_t data_len) {

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    return !((inet_aton(ip.c_str(), &addr.sin_addr) == 0) ||
             (sendto(this->socket_number, &command, EMPTY_CMPLX_CMD_LENGTH + data_len, 0, (sockaddr*)&addr,
                     sizeof(addr)) != EMPTY_CMPLX_CMD_LENGTH + data_len));
}

bool UDP_socket::receive_cmplx_cmd(cmplx_cmd_wrapper *wrapper) {

    cmplx_cmd command;
    ssize_t len;
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(sockaddr_in);
    if ((len = recvfrom(this->socket_number, &command, sizeof(command), MSG_WAITALL, (sockaddr*)&addr, &addr_len)) < 0) {
        return false;
    }
    wrapper->command = command;
    wrapper->length = len;
    wrapper->address = addr;
    return true;
}

bool UDP_socket::receive_simpl_cmd(simpl_cmd_wrapper *wrapper) {

    simpl_cmd command;
    ssize_t len;
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(sockaddr_in);
    if ((len = recvfrom(this->socket_number, &command, sizeof(command), MSG_WAITALL, (sockaddr*)&addr, &addr_len)) < 0) {
        return false;
    }
    wrapper->command = command;
    wrapper->length = len;
    wrapper->address = addr;
    return true;
}

bool UDP_socket::set_timeout(uint64_t nanosec) {

    timeval timeval{};
    timeval.tv_sec = floor(nanosec / 1e9);
    timeval.tv_usec = (nanosec % (uint64_t)1e9) / 1000;
    int res = setsockopt(this->socket_number, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeval, sizeof(timeval));
    return (res >= 0);
}

bool UDP_socket::set_reuse_address() {

    int reuse = 1;
    return !(setsockopt(this->socket_number, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0);
}

UDP_socket::~UDP_socket() {
    if (!closed) {
        close(this->socket_number);
    }
}

bool TCP_socket::init_socket() {

    if ((this->socket_number = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        return false;
    }
    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    if (getsockname(this->socket_number, (sockaddr*)&addr, &len) < 0) {
        return false;
    }
    this->port_number = addr.sin_port;
    this->closed = false;
    return true;
}

bool TCP_socket::bind_to_random_port() {

    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    if (bind(this->socket_number, (sockaddr*)&addr, sizeof(addr)) < 0) {
        return false;
    }
    memset(&addr, 0, sizeof(addr));
    if (getsockname(this->socket_number, (sockaddr*) &addr, &len) < 0) {
        return false;
    }
    this->port_number = addr.sin_port;
    return true;
}

bool TCP_socket::connect_to_socket(const std::string &ip, in_port_t port) {

    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    return  !((inet_aton(ip.c_str(), &addr.sin_addr) == 0) ||
              (connect(this->socket_number, (sockaddr*)&addr, sizeof(addr)) < 0));
}

int32_t TCP_socket::accept_connection() {

    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    return accept(this->socket_number, (sockaddr*)&addr, &len);
}

int TCP_socket::select(uint64_t seconds) {

    timeval timeval{};
    timeval.tv_sec = seconds;
    timeval.tv_usec = 0;
    fd_set fd_set;
    FD_ZERO(&fd_set);
    FD_SET(this->socket_number, &fd_set);
    return ::select(this->socket_number + 1, &fd_set, nullptr, nullptr, &timeval);
}

void TCP_socket::close_socket() {

    close(this->socket_number);
    this->closed = true;
}

TCP_socket::~TCP_socket() {
    if (!closed) {
        close(this->socket_number);
    }
}
