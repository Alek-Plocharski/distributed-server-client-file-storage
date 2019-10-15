#ifndef CLIENT_H
#define CLIENT_H

#include <unordered_map>
#include <map>
#include <mutex>
#include <random>
#include <netinet/in.h>
#include <boost/filesystem/path.hpp>

#include "communication.h"

constexpr uint16_t CLIENT_DEFAULT_TIMEOUT_VALUE = 5;
constexpr uint16_t CLIENT_MAX_TIMEOUT_VALUE = 300;

struct client_options {

    std::string mcast_addr;
    in_port_t cmd_port;
    std::string out_fldr;
    uint16_t timeout;

    /*
     * Fills fields in structure according to values passed as parameters.
     */
    void fill_from_arguments(int argc, const char* argv[]);
};

class Client {

private:

    client_options options;
    std::unordered_map<std::string, sockaddr_in> files_list;
    UDP_socket multicast_socket;
    std::mutex output_mutex;

    std::mt19937_64 generator;
    std::uniform_int_distribution<uint64_t> uniform_distribution;

    /*
     * Generates a random cmd_seq for protocol command.
     */
    uint64_t generate_cmd_seq();
    /*
     * Prints a package skipping message in format according to task.
     */
    void package_skipping(const std::string &ip, uint16_t port, std::string &additional_message);

    /*
     * Sends HELLO requests to all servers.
     */
    bool send_discover_request(uint64_t *cmd_seq);
    /*
     * Receives responses to the HELLO request from server and returns their info.
     * If print is set to true it also prints the data of servers.
     */
    std::multimap<uint64_t, sockaddr_in> receive_discover_responses(uint64_t generated_cmd_seq, bool print);
    /*
     * Sends HELLO request to all servers and prints data received from them.
     */
    void discover();

    /*
     * Sends LIST requests to all servers.
     */
    bool send_search_request(std::string &pattern, uint64_t *cmd_seq);
    /*
     * Receives responses to the LIST request and prints them for the user to see.
     */
    void receive_search_responses(uint64_t generated_cmd_seq);
    /*
     * Sends LIST request to all servers and prints data received from them.
     */
    void search(std::string &pattern);

    /*
     * Prints a message indicating that the fetch was not successful with the reason of failure.
     */
    void print_fetch_failure(const std::string &file, const std::string &ip, in_port_t port, const std::string &message);
    /*
     * Prints a message indicating that the fetch was successful.
     */
    void print_fetch_success(const std::string &file, const std::string &ip, in_port_t port);
    /*
     * Sends a GET request to the server that currently stores requested file.
     */
    bool send_fetch_request(struct UDP_socket &socket, const std::string &file, sockaddr_in addr, uint64_t *cmd_seq);
    /*
     * Receives a response to a GET request from a server.
     */
    in_port_t receive_fetch_response(struct UDP_socket &socket, const std::string &file, uint64_t cmd_seq, sockaddr_in addr);
    /*
     * Downloads specified file from server using TCP socket.
     */
    void download_file(const std::string &file, in_port_t port, sockaddr_in addr);
    /*
     * Sends GET request to a server storing specified file.
     * After getting confirmation that the server has requested file downloads the file from server.
     */
    void fetch(const std::string &file);

    /*
     * Prints a message indicating that the upload was not successful with the reason of failure.
     */
    void print_upload_failure(const std::string &file, const std::string &ip, in_port_t port, const std::string &message);
    /*
     * Prints a message indicating that the upload was successful.
     */
    void print_upload_success(const std::string &file, const std::string &ip, in_port_t port);
    /*
     * Sends ADD request to a specified server.
     */
    bool send_upload_request(UDP_socket &sock, uint64_t file_size, std::string &file, sockaddr_in &addr, uint64_t *cmd_seq);
    /*
     * Receives a response to an ADD request from a server.
     */
    bool receive_upload_response(UDP_socket &sock, in_port_t *port, uint64_t cmd_seq, std::string &filename);
    /*
     * Sends specified file to server using a TCP socket.
     */
    void send_file(boost::filesystem::path &file, uint64_t file_size, in_port_t port, sockaddr_in addr);
    /*
     * Sends ADD request to server with most free space, if the request is denied continues with other servers.
     * After getting accepted send the file to server.
     */
    void upload(const std::string &file);

    /*
     * Sends requests to all servers to remove specified file.
     */
    void remove(const std::string &file);

public:

    explicit Client(client_options options);
    void run();
};

#endif //CLIENT_H
