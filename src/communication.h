#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <string>
#include <netinet/in.h>

constexpr int CMD_MAX_LENGTH = 10;
constexpr int MAX_UDP_MESSAGE_SIZE = 65507;
constexpr int EMPTY_SIMPL_CMD_LENGTH = 18;
constexpr int EMPTY_CMPLX_CMD_LENGTH = 26;
constexpr int SIMPL_CMD_MAX_DATA_LENGTH = MAX_UDP_MESSAGE_SIZE - EMPTY_SIMPL_CMD_LENGTH;
constexpr int CMPLX_CMD_MAX_DATA_LENGTH = MAX_UDP_MESSAGE_SIZE - EMPTY_CMPLX_CMD_LENGTH;
constexpr int BUFFER_SIZE = 65535;
constexpr int TTL = 5;
constexpr int QUEUE_LENGTH = 5;
const std::string HELLO_REQUEST = "HELLO";
const std::string HELLO_RESPONSE = "GOOD_DAY";
const std::string LIST_REQUEST = "LIST";
const std::string LIST_RESPONSE = "MY_LIST";
const std::string GET_REQUEST = "GET";
const std::string GET_RESPONSE = "CONNECT_ME";
const std::string DELETE_REQUEST = "DEL";
const std::string ADD_REQUEST = "ADD";
const std::string ADD_DENIED_RESPONSE = "NO_WAY";
const std::string ADD_ACCEPTED_RESPONSE = "CAN_ADD";


struct __attribute__((__packed__)) simpl_cmd {

    char cmd[CMD_MAX_LENGTH];
    uint64_t cmd_seq;
    char data[SIMPL_CMD_MAX_DATA_LENGTH + 1];

    simpl_cmd();
    simpl_cmd(const std::string &cmd, uint64_t cmd_seq, const char *data);
};

struct __attribute__((__packed__)) cmplx_cmd {

    char cmd[CMD_MAX_LENGTH];
    uint64_t cmd_seq;
    uint64_t param;
    char data[CMPLX_CMD_MAX_DATA_LENGTH + 1];

    cmplx_cmd();
    cmplx_cmd(const std::string &cmd, uint64_t cmd_seq, uint64_t param, const char *data);
};

struct simpl_cmd_wrapper {

    simpl_cmd command;
    uint64_t length;
    sockaddr_in address;
};

struct cmplx_cmd_wrapper {

    cmplx_cmd command;
    uint64_t length;
    sockaddr_in address;
};

struct UDP_socket {

    int32_t socket_number;
    bool closed = true;

    UDP_socket() = default;
    ~UDP_socket();

    bool init_standard_socket();
    bool init_multicast_socket();
    bool bind_to_specific_port(in_port_t port);
    bool set_reuse_address();
    bool set_timeout(uint64_t nanosec);

    /*
     * Sends a simpl_cmd on multicast.
     */
    bool send_simpl_cmd(const simpl_cmd& command, const struct sockaddr_in& addr, uint16_t data_len);
    /*
     * Sends a simpl_cmd to specific ip address.
     */
    bool send_simpl_cmd_by_ip(const simpl_cmd& command, const std::string& ip, in_port_t port, uint16_t data_len);
    /*
     * Sends a cmplx_cmd on multicast.
     */
    bool send_cmplx_cmd(const cmplx_cmd& command, const struct sockaddr_in& addr, uint16_t data_len);
    /*
     * Sends a cmplx_cmd to specific ip address.
     */
    bool send_cmplx_cmd_by_ip(const cmplx_cmd& command, const std::string& ip, in_port_t port, uint16_t data_len);
    /*
     * Receives a single cmplx_cmd send to socket.
     */
    bool receive_cmplx_cmd(cmplx_cmd_wrapper *wrapper);
    /*
     * Receives a single simpl_cmd send to socket.
     */
    bool receive_simpl_cmd(simpl_cmd_wrapper *wrapper);
};

struct TCP_socket {

    int32_t socket_number;
    in_port_t port_number;
    bool closed = true;

    TCP_socket() = default;
    ~TCP_socket();

    bool init_socket();
    bool bind_to_random_port();
    bool connect_to_socket(const std::string &ip, in_port_t port);
    int32_t accept_connection();
    int select(uint64_t seconds);
    void close_socket();
};

#endif //COMMUNICATION_H
