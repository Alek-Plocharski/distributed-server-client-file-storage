#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <set>
#include <mutex>

#include "communication.h"

constexpr uint64_t DEFAULT_MAX_SPACE = 52428800;
constexpr uint16_t SERVER_DEFAULT_TIMEOUT_VALUE = 5;
constexpr uint16_t SERVER_MAX_TIMEOUT_VALUE = 300;

struct server_options {

    std::string mcast_addr;
    in_port_t cmd_port;
    uint64_t max_space;
    std::string shrd_fldr;
    uint16_t timeout;

    /*
     * Fills fields in structure according to values passed as parameters.
     */
    void fill_from_arguments(int argc, const char* argv[]);
};

struct file_set {

    std::set<std::string> files_list;
    uint64_t space_taken;
    std::uint64_t max_space;
    std::mutex files_list_mutex;
    std::mutex space_taken_mutex;

    /*
     * Operations for checking and changing what files are in file set.
     */
    bool is_file_in_set(const std::string &file);
    bool add_file_to_set(const std::string &file);
    bool del_file_from_set(const std::string &file);

    /*
     * Operations for checking and changing how much free space is in file set.
     */
    uint64_t get_left_space();
    bool reserve_space(uint64_t number_of_bytes);
    void free_space(uint64_t number_of_bytes);
};

class Server {

private:

    server_options options;
    file_set server_file_set;
    UDP_socket communication_socket;

    /*
     * Handles HELLO request send by client to servers UDP port according to communication protocol specification.
     */
    void handle_hello_request(sockaddr_in addr, uint64_t cmd_seq);

    /*
     * Handles LIST request send by client to servers UDP port according to the communication protocol specification.
     */
    void handle_list_request(sockaddr_in addr, uint64_t cmd_seq, std::string pattern);

    /*
     * Sends a specified file to client using a TCP socket.
     */
    void send_file(TCP_socket &sock, const std::string &file);
    /*
     * Handles GET request send by client to servers UDP port according to the communication protocol specification.
     */
    void handle_get_request(sockaddr_in addr, uint64_t cmd_seq, std::string file);

    /*
     * Download a specific file from client using a TCP socket.
     */
    void download_file(TCP_socket &sock, const std::string &file, uint64_t bytes_to_download);
    /*
     * Handles ADD request send by client to servers UDP port according to the communication protocol specification.
     */
    void handle_add_request(sockaddr_in addr, uint64_t cmd_seq, uint64_t file_size, std::string file);

    /*
     * Handles DEL request send by client to servers UDP port according to the communication protocol specification.
     */
    void handle_delete_request(std::string file);

public:

    explicit Server(const server_options &options);
    void run();
};

#endif //SERVER_H
