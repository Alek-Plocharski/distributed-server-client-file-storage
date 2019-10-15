#include <random>
#include <chrono>
#include <fstream>
#include <thread>
#include <iostream>
#include <arpa/inet.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "client.h"
#include "communication.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;

uint64_t Client::generate_cmd_seq() {
    return uniform_distribution(generator);
}

static bool compare_cmd(const std::string &expected_cmd, const char *cmd) {

    uint16_t i;
    for (i = 0; i < CMD_MAX_LENGTH && i < expected_cmd.length(); i++) {
        if (expected_cmd[i] != cmd[i]) {
            return false;
        }
    }
    while (i < CMD_MAX_LENGTH) {
        if (cmd[i] != '\0') {
            return false;
        }
        i++;
    }
    return true;
}

static bool compare_data(const std::string &expected_data, const char *data, size_t len) {

    if (expected_data.length() != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (expected_data[i] != data[i]) {
            return false;
        }
    }
    return true;
}

static std::string is_valid_simpl_cmd(simpl_cmd &command, const std::string &cmd, uint64_t cmd_seq, ssize_t len, const std::string &data) {

    if (len < EMPTY_SIMPL_CMD_LENGTH) {
        return "Message too small";
    } if (cmd_seq != be64toh(command.cmd_seq)) {
        return "Wrong cmd_seq";
    } if (!compare_cmd(cmd, command.cmd)) {
        return "Wrong cmd";
    } if (!compare_data(data, command.data, len - EMPTY_SIMPL_CMD_LENGTH)) {
        return "Wrong data";
    }
    return "OK";
}

static std::string is_valid_simpl_cmd(simpl_cmd &command, const std::string &cmd, uint64_t cmd_seq, ssize_t len) {

    if (len < EMPTY_SIMPL_CMD_LENGTH) {
        return "Message too small";
    } if (cmd_seq != be64toh(command.cmd_seq)) {
        return "Wrong cmd_seq";
    } if (!compare_cmd(cmd, command.cmd)) {
        return "Wrong cmd";
    }
    return "OK";
}

static std::string is_valid_cmplx_cmd(cmplx_cmd &command, const std::string &cmd, uint64_t cmd_seq, ssize_t len, const std::string &data) {

    if (len < EMPTY_CMPLX_CMD_LENGTH) {
        return "Message too small";
    } if (cmd_seq != be64toh(command.cmd_seq)) {
        return "Wrong cmd_seq";
    } if (!compare_cmd(cmd, command.cmd)) {
        return "Wrong cmd";
    } if (!compare_data(data, command.data, len - EMPTY_CMPLX_CMD_LENGTH)) {
        return "Wrong data";
    }
    return "OK";
}

static std::string is_valid_cmplx_cmd(cmplx_cmd &command, const std::string &cmd, uint64_t cmd_seq, ssize_t len) {

    if (len < EMPTY_CMPLX_CMD_LENGTH) {
        return "Message too small";
    } if (cmd_seq != be64toh(command.cmd_seq)) {
        return "Wrong cmd_seq";
    } if (!compare_cmd(cmd, command.cmd)) {
        return "Wrong cmd";
    }
    return "OK";
}

void Client::package_skipping(const std::string &ip, uint16_t port, std::string &additional_message) {
    this->output_mutex.lock();
    std::cerr << "[PCKG ERROR]  Skipping invalid package from " << ip << ":" << port << ". "
                << additional_message << std::endl;
    this->output_mutex.unlock();
}


bool Client::send_discover_request(uint64_t *cmd_seq) {

    (*cmd_seq) = this->generate_cmd_seq();
    struct simpl_cmd command(HELLO_REQUEST, htobe64(*cmd_seq), "");
    return this->multicast_socket.send_simpl_cmd_by_ip(command, this->options.mcast_addr, htobe16(this->options.cmd_port),0);
}

std::multimap<uint64_t, sockaddr_in> Client::receive_discover_responses(uint64_t generated_cmd_seq, bool print) {

    std::multimap<uint64_t, sockaddr_in> servers_list;
    cmplx_cmd_wrapper wrapper;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <= std::chrono::seconds(this->options.timeout)) {
        if (this->multicast_socket.set_timeout(this->options.timeout * 1e9 - (std::chrono::steady_clock::now() - start).count())
            && this->multicast_socket.receive_cmplx_cmd(&wrapper)) {
            cmplx_cmd command = wrapper.command;
            ssize_t len = wrapper.length;
            struct sockaddr_in addr = wrapper.address;
            std::string message;
            if ((message = is_valid_cmplx_cmd(command, HELLO_RESPONSE, generated_cmd_seq, len)) != "OK") {
                this->package_skipping(inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), message);
                continue;
            }
            servers_list.insert({be64toh(command.param), addr});
            if (print) {
                this->output_mutex.lock();
                std::cout << "Found " << inet_ntoa(addr.sin_addr) << " (" << command.data
                          << ") with free space " << be64toh(command.param) << std::endl;
                this->output_mutex.unlock();
            }
        }
    }
    return servers_list;
}

void Client::discover() {

    uint64_t cmd_seq;
    if (this->send_discover_request(&cmd_seq)) {
        this->receive_discover_responses(cmd_seq, true);
    }
}


void Client::receive_search_responses(uint64_t generated_cmd_seq) {

    simpl_cmd_wrapper wrapper;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <= std::chrono::seconds(this->options.timeout)) {
        if (this->multicast_socket.set_timeout(this->options.timeout * 1e9 - (std::chrono::steady_clock::now() - start).count())
            && this->multicast_socket.receive_simpl_cmd(&wrapper)) {
            simpl_cmd command = wrapper.command;
            ssize_t len = wrapper.length;
            struct sockaddr_in addr = wrapper.address;
            std::string message;
            if ((message = is_valid_simpl_cmd(command, LIST_RESPONSE, generated_cmd_seq, len)) != "OK") {
                this->package_skipping(inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), message);
                continue;
            }
            std::vector<std::string> new_files;
            boost::split(new_files, command.data, boost::is_any_of("\n"));
            this->output_mutex.lock();
            for (auto &file : new_files) {
                std::cout << file << " (" << inet_ntoa(addr.sin_addr) << ")" << std::endl;
                this->files_list.insert({file, addr});
            }
            this->output_mutex.unlock();
        }
    }
}

bool Client::send_search_request(std::string &pattern, uint64_t *cmd_seq) {

    (*cmd_seq) = this->generate_cmd_seq();
    simpl_cmd command(LIST_REQUEST, htobe64(*cmd_seq), pattern.c_str());
    return this->multicast_socket.send_simpl_cmd_by_ip(command, this->options.mcast_addr,
                                                       be16toh(this->options.cmd_port), pattern.length());
}

void Client::search(std::string &pattern) {

    uint64_t cmd_seq;
    if (this->send_search_request(pattern, &cmd_seq)) {
        this->files_list.clear();
        this->receive_search_responses(cmd_seq);
    }
}


void Client::print_fetch_failure(const std::string &file, const std::string &ip, in_port_t port, const std::string &message) {
    this->output_mutex.lock();
    std::cout << "File " << file << " downloading failed (" << ip << ":" << port << ") " << message << std::endl;
    this->output_mutex.unlock();
}

void Client::print_fetch_success(const std::string &file, const std::string &ip, in_port_t port) {
    this->output_mutex.lock();
    std::cout << "File " << file << " downloaded (" << ip << ":" << port << ")" << std::endl;
    this->output_mutex.unlock();
}

bool Client::send_fetch_request(struct UDP_socket &socket, const std::string &file, sockaddr_in addr, uint64_t *cmd_seq) {

    (*cmd_seq) = this->generate_cmd_seq();
    struct simpl_cmd command(GET_REQUEST, htobe64(*cmd_seq), file.c_str());
    if (socket.init_standard_socket() && socket.send_simpl_cmd_by_ip(command, inet_ntoa(addr.sin_addr), htobe16(this->options.cmd_port), file.length())) {
        return true;
    }
    this->output_mutex.lock();
    std::cout << "File " << file << " downloading failed (:) Error while sending fetch request" << std::endl;
    this->output_mutex.unlock();
    return false;
}

in_port_t Client::receive_fetch_response(struct UDP_socket &socket, const std::string &file, uint64_t cmd_seq, sockaddr_in addr) {

    struct cmplx_cmd_wrapper wrapper;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <= std::chrono::seconds(this->options.timeout)) {
        if (socket.set_timeout(this->options.timeout * 1e9 - (std::chrono::steady_clock::now() - start).count())
            && socket.receive_cmplx_cmd(&wrapper)) {
            struct cmplx_cmd command = wrapper.command;
            ssize_t len = wrapper.length;
            std::string message;
            if ((message = is_valid_cmplx_cmd(command, GET_RESPONSE, cmd_seq, len, file)) != "OK") {
                this->package_skipping(inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), message);
                continue;
            }
            return be64toh(command.param);
        }
    }
    this->output_mutex.lock();
    std::cout << "File " << file << " downloading failed (:) Timeout" << std::endl;
    this->output_mutex.unlock();
    return 0;
}

void Client::download_file(const std::string &file, in_port_t port, sockaddr_in addr) {

    struct TCP_socket socket;
    if (!socket.init_socket()) {
        this->print_fetch_failure(file, inet_ntoa(addr.sin_addr), port, "Error creating TCP socket");
        return;
    }
    if (!socket.connect_to_socket(inet_ntoa(addr.sin_addr), htobe16(port))) {
        this->print_fetch_failure(file, inet_ntoa(addr.sin_addr), port, "Error connecting to TCP socket");
    }
    fs::ofstream file_stream(this->options.out_fldr + file, std::ofstream::binary);
    if (file_stream.is_open()) {
        char buffer[BUFFER_SIZE];
        ssize_t len;
        while ((len = read(socket.socket_number, buffer, sizeof(buffer))) > 0) {
            file_stream.write(buffer, len);
        }
        if (len < 0) {
            this->print_fetch_failure(file, inet_ntoa(addr.sin_addr), port, "Read error");
            return;
        }
    } else {
        this->print_fetch_failure(file, inet_ntoa(addr.sin_addr), port, "Failed to open file");
        return;
    }
    this->print_fetch_success(file, inet_ntoa(addr.sin_addr), port);
}

void Client::fetch(const std::string &file) {

    if (this->files_list.find(file) == this->files_list.end()) {
        this->output_mutex.lock();
        std::cout << "File wasn't in last search result" << std::endl;
        this->output_mutex.unlock();
        return;
    }
    struct UDP_socket socket;
    in_port_t port;
    uint64_t cmd_seq;
    if (this->send_fetch_request(socket, file, this->files_list[file], &cmd_seq)
        && (port = this->receive_fetch_response(socket, file, cmd_seq, this->files_list[file])) != 0) {
        this->download_file(file, port, this->files_list[file]);
    }
}


void Client::print_upload_failure(const std::string &file, const std::string &ip, in_port_t port, const std::string &message) {
    this->output_mutex.lock();
    std::cout << "File " << file << " uploading failed (" << ip << ":" << port << ") " << message << std::endl;
    this->output_mutex.unlock();
}

void Client::print_upload_success(const std::string &file, const std::string &ip, in_port_t port) {
    this->output_mutex.lock();
    std::cout << "File " << file << " uploaded (" << ip << ":" << port << ")" << std::endl;
    this->output_mutex.unlock();
}

bool Client::send_upload_request(UDP_socket &sock, uint64_t file_size, std::string &file, sockaddr_in &addr, uint64_t *cmd_seq) {

    (*cmd_seq) = this->generate_cmd_seq();
    cmplx_cmd command(ADD_REQUEST, htobe64(*cmd_seq), htobe64(file_size), file.c_str());
    return sock.send_cmplx_cmd_by_ip(command, inet_ntoa(addr.sin_addr), htobe16(this->options.cmd_port), file.length());
}

static bool is_accept_response(const char *cmd) {

    for (int i = 0; i < CMD_MAX_LENGTH; i++) {
        if (cmd[i] != ADD_ACCEPTED_RESPONSE[i]) {
            return false;
        }
    }
    return true;
}

bool Client::receive_upload_response(UDP_socket &sock, in_port_t *port, uint64_t cmd_seq, std::string &filename) {

    cmplx_cmd_wrapper wrapper;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <= std::chrono::seconds(this->options.timeout)) {
        if (sock.set_timeout(this->options.timeout * 1e9 - (std::chrono::steady_clock::now() - start).count())
            && sock.receive_cmplx_cmd(&wrapper)) {
            struct cmplx_cmd command = wrapper.command;
            ssize_t len = wrapper.length;
            struct sockaddr_in addr = wrapper.address;
            std::string message = "";
            if (is_accept_response(command.cmd)) {
                if ((message = is_valid_cmplx_cmd(command, ADD_ACCEPTED_RESPONSE, cmd_seq, len, "")) !=
                    "OK") {
                    this->package_skipping(inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), message);
                    continue;
                }
                (*port) = be64toh(command.param);
                return true;
            } else {
                simpl_cmd *simpl_command = (simpl_cmd *) &command;
                if ((message = is_valid_simpl_cmd(*simpl_command, ADD_DENIED_RESPONSE, cmd_seq, len, filename)) !=
                    "OK") {
                    this->package_skipping(inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), message);
                    continue;
                }
                return false;
            }
        }
    }
    return false;
}

void Client::send_file(fs::path &file, uint64_t file_size, in_port_t port, sockaddr_in addr) {

    std::string filename = file.filename().string();
    ssize_t len;
    uint64_t to_upload = file_size;
    TCP_socket sock;
    if (!sock.init_socket()) {
        this->print_upload_failure(filename, inet_ntoa(addr.sin_addr), port, "Error creating TCP socket");
        return;
    }
    if (!sock.connect_to_socket(inet_ntoa(addr.sin_addr), htobe16(port))) {
        this->print_upload_failure(filename, inet_ntoa(addr.sin_addr), port, "Error connecting to socket");
        return;
    }
    std::ifstream file_stream(file.c_str(), std::ios::binary);
    if (file_stream.is_open()) {
        char buffer[BUFFER_SIZE];
        while (file_stream) {
            file_stream.read(buffer, BUFFER_SIZE);
            len = file_stream.gcount();
            to_upload -= len;
            if (write(sock.socket_number, buffer, len) != len) {
                this->print_upload_failure(filename, inet_ntoa(addr.sin_addr), port, "Error while writing to socket");
                return;
            }
        }
        if (to_upload != 0) {
            this->print_upload_failure(filename, inet_ntoa(addr.sin_addr), port, "Didn't finish uploading");
            return;
        }
    } else {
        this->print_upload_failure(filename, inet_ntoa(addr.sin_addr), port, "Error opening file");
        return;
    }
    sock.close_socket();
    this->print_upload_success(filename, inet_ntoa(addr.sin_addr), port);
}

void Client::upload(const std::string &file) {

    fs::path filepath = file;
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        this->output_mutex.lock();
        std::cout << "File " << filepath.filename().string() << " does not exist" << std::endl;
        this->output_mutex.unlock();
        return;
    }
    in_port_t port;
    uint64_t cmd_seq;
    uintmax_t file_size = fs::file_size(filepath);
    std::string filename = filepath.filename().string();
    this->send_discover_request(&cmd_seq);
    std::multimap<uint64_t, sockaddr_in> servers_list = this->receive_discover_responses(cmd_seq, false);
    std::multimap<uint64_t, sockaddr_in>::reverse_iterator rit = servers_list.rbegin();
    if (servers_list.begin() == servers_list.end() || rit->first < file_size) {
        this->output_mutex.lock();
        std::cout << "File " << filepath.filename().string() << " too big" << std::endl;
        this->output_mutex.unlock();
        return;
    }
    UDP_socket sock;
    if (!sock.init_standard_socket()) {
        this->output_mutex.lock();
        std::cout << "File " << file << " uploading failed (:) Error while creating UDP socket" << std::endl;
        this->output_mutex.unlock();
        return;
    }
    for (; rit != servers_list.rend() && rit->first >= file_size; rit++) {
        this->send_upload_request(sock, file_size, filename, rit->second, &cmd_seq);
        if (this->receive_upload_response(sock, &port, cmd_seq, filename)) {
            this->send_file(filepath, file_size, port, rit->second);
            return;
        }
    }
    std::cout << "File " << filename << " too big" << std::endl;
}


void Client::remove(const std::string &file) {

    uint64_t cmd_seq = this->generate_cmd_seq();
    simpl_cmd command(DELETE_REQUEST ,cmd_seq, file.c_str());
    this->multicast_socket.send_simpl_cmd_by_ip(command, this->options.mcast_addr, htobe16(this->options.cmd_port), file.length());
}


void client_options::fill_from_arguments(int argc, const char **argv) {

    po::options_description description("Client options");
    description.add_options()
            (",g", po::value<std::string>(&(this->mcast_addr))->required(), "Multicast address")
            (",p", po::value<in_port_t >(&(this->cmd_port))->required()->notifier([description](int64_t p) {
                if (p == 0) {
                    std::cerr << "CMD_PORT can't be equal to 0" << std::endl;
                    exit(1);
                }
            }), "Servers command port")
            (",o", po::value<std::string>(&(this->out_fldr))->required(), "Folder for saving files")
            (",t", po::value<uint16_t>(&(this->timeout))->default_value(CLIENT_DEFAULT_TIMEOUT_VALUE)->notifier([description](int64_t t) {
                if (t > CLIENT_MAX_TIMEOUT_VALUE) {
                    std::cerr << "TIMEOUT option has to be less or equal to 300" << std::endl;
                    exit(1);
                }
            }), "Client timeout");
    po::variables_map var_map;
    try {
        po::store(po::parse_command_line(argc, argv, description), var_map);
        po::notify(var_map);
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        std::cout << description << std::endl;
        exit(1);
    }
}

static std::vector<std::string> read_user_command() {

    std::string input;
    std::vector<std::string> split_input(2);
    getline(std::cin, input);
    size_t i = input.find_first_of(' ');
    if (i != std::string::npos) {
        split_input[0] = input.substr(0, i);
        split_input[1] = input.substr(i + 1, std::string::npos);
        boost::algorithm::to_upper(split_input[0]);
        if ((split_input[0] != "SEARCH" && split_input[0] != "FETCH" && split_input[0] != "UPLOAD"
             && split_input[0] != "REMOVE") || split_input[1].empty()) {
            split_input[0] = "INVALID";
        }
    } else {
        split_input[0] = input;
        split_input[1] = "";
        boost::algorithm::to_upper(split_input[0]);
        if (split_input[0] != "DISCOVER" && split_input[0] != "SEARCH" && split_input[0] != "EXIT") {
            split_input[0] = "INVALID";
        }
    }
    boost::algorithm::to_upper(split_input[0]);
    return split_input;
}

void Client::run() {

    bool exit = false;
    while (!exit) {
        std::vector<std::string> split_input = read_user_command();
        if (split_input[0] == "DISCOVER") {
            this->discover();
        } else if (split_input[0] == "SEARCH") {
            this->search(split_input[1]);
        } else if (split_input[0] == "FETCH") {
            std::thread t(&Client::fetch, this, split_input[1]);
            t.detach();
        } else if (split_input[0] == "UPLOAD") {
            std::thread t(&Client::upload, this, split_input[1]);
            t.detach();
        } else if (split_input[0] == "REMOVE") {
            this->remove(split_input[1]);
        } else if (split_input[0] == "EXIT") {
            exit = true;
        }
    }
}

Client::Client(client_options options) : options(options) {
    if (*(this->options.out_fldr.rend()) != '/') {
        this->options.out_fldr += '/';
    }
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, std::llround(std::pow(2,64)));
    this->generator = gen;
    this->uniform_distribution = dist;
    if (this->options.out_fldr != "./" && this->options.out_fldr != "../") {
        try {
            boost::filesystem::create_directories(this->options.out_fldr);
        } catch(boost::filesystem::filesystem_error &e) {
            std::cerr << "Invalid OUT_FLDR argument" << std::endl;
            exit(1);
        }
    }
    if (!multicast_socket.init_multicast_socket()) {
        std::cerr << "Failed to create multicast socket" << std::endl;
        exit(1);
    }
}
