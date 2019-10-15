#include <vector>
#include <thread>
#include <csignal>
#include <iostream>
#include <arpa/inet.h>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "server.h"
#include "communication.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;


void server_options::fill_from_arguments(int argc, const char **argv) {

    po::options_description description("Server options");
    description.add_options()
            (",g", po::value<std::string>(&(this->mcast_addr))->required(), "Multicast address")
            (",p", po::value<in_port_t >(&(this->cmd_port))->required()->notifier([description](int64_t p) {
                if (p == 0) {
                    std::cerr << "CMD_PORT can't be equal to 0" << std::endl;
                    exit(1);
                }
            }), "Command port for communication")
            (",b", po::value<uint64_t>(&(this->max_space))->default_value(DEFAULT_MAX_SPACE), "Max space available to server")
            (",f", po::value<std::string>(&(this->shrd_fldr))->required(), "Folder for files stored in server")
            (",t", po::value<uint16_t>(&(this->timeout))->default_value(SERVER_DEFAULT_TIMEOUT_VALUE)->notifier([description](int64_t t) {
                if (t > SERVER_MAX_TIMEOUT_VALUE) {
                    std::cerr << "TIMEOUT option has to be less or equal to 300" << std::endl;
                    exit(1);
                }
            }), "Client timeout")
            ;
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


bool file_set::add_file_to_set(const std::string& file) {

    files_list_mutex.lock();
    bool res = files_list.insert(file).second;
    files_list_mutex.unlock();
    return res;
}

bool file_set::del_file_from_set(const std::string &file) {

    files_list_mutex.lock();
    int res = files_list.erase(file);
    files_list_mutex.unlock();
    return (res > 0);
}

bool file_set::is_file_in_set(const std::string &file) {

    files_list_mutex.lock();
    bool res = files_list.find(file) != files_list.end();
    files_list_mutex.unlock();
    return res;
}

uint64_t file_set::get_left_space() {

    uint64_t snapshot = this->space_taken;
    return snapshot <= this->max_space ? this->max_space - snapshot : 0;
}

bool file_set::reserve_space(uint64_t number_of_bytes) {

    space_taken_mutex.lock();
    bool can_reserve = (this->max_space >= number_of_bytes && space_taken <= this->max_space - number_of_bytes);
    if (can_reserve) {
        space_taken += number_of_bytes;
    }
    space_taken_mutex.unlock();
    return can_reserve;
}

void file_set::free_space(uint64_t number_of_bytes) {

    space_taken_mutex.lock();
    space_taken -= number_of_bytes;
    space_taken_mutex.unlock();
}


void Server::handle_hello_request(const sockaddr_in addr, uint64_t cmd_seq) {

    cmplx_cmd command(HELLO_RESPONSE, htobe64(cmd_seq), htobe64(this->server_file_set.get_left_space()),
                            this->options.mcast_addr.c_str());
    this->communication_socket.send_cmplx_cmd(command, addr, this->options.mcast_addr.length());
}

void Server::handle_list_request(sockaddr_in addr, uint64_t cmd_seq, std::string pattern) {

    this->server_file_set.files_list_mutex.lock();
    simpl_cmd command(LIST_RESPONSE, htobe64(cmd_seq), "");
    uint64_t data_len = 0;
    std::set<std::string>::iterator it = this->server_file_set.files_list.begin();
    while (it != this->server_file_set.files_list.end()) {
        if ((*it).find(pattern) != std::string::npos) {
            if ((*it).length() + data_len >= SIMPL_CMD_MAX_DATA_LENGTH) {
                this->communication_socket.send_simpl_cmd(command, addr, data_len);
                memset(&command.data, '\0', sizeof(command.data));
                data_len = 0;
            } else {
                if (data_len > 0) {
                    command.data[data_len] = '\n';
                    data_len++;
                }
                strcpy(command.data + data_len, (*it).c_str());
                data_len += (*it).length();
            }
        }
        it++;
    }
    if (data_len > 0) {
        this->communication_socket.send_simpl_cmd(command, addr, data_len);
    }
    this->server_file_set.files_list_mutex.unlock();
}

void Server::send_file(TCP_socket &sock, const std::string &file) {

    int32_t socket_number;
    if (sock.select(this->options.timeout) != -1) {
        if ((socket_number = sock.accept_connection()) < 0) {
            return;
        }
        std::ifstream file_stream((this->options.shrd_fldr + file).c_str(), std::ios::binary);
        if (file_stream.is_open()) {
            char buffer[BUFFER_SIZE];
            memset(buffer, '\0', BUFFER_SIZE);
            ssize_t len;
            while (file_stream) {
                file_stream.read(buffer, BUFFER_SIZE);
                len = file_stream.gcount();
                if (write(socket_number, buffer, len) != len) {
                    break;
                }
            }
        }
        close(socket_number);
    }
}

void Server::handle_get_request(sockaddr_in addr, uint64_t cmd_seq, std::string file) {

    TCP_socket tcp_sock;
    if (!tcp_sock.init_socket() || !tcp_sock.bind_to_random_port() || (listen(tcp_sock.socket_number, QUEUE_LENGTH) < 0)) {
        return;
    }
    cmplx_cmd command(GET_RESPONSE, htobe64(cmd_seq), htobe64(be16toh(tcp_sock.port_number)), file.c_str());
    if (!communication_socket.send_cmplx_cmd(command, addr, file.length())) {
        return;
    }
    send_file(tcp_sock, file);
}

void Server::download_file(TCP_socket &sock, const std::string &file, uint64_t bytes_to_download) {

    int32_t socket_number;
    uint64_t to_download = bytes_to_download;
    ssize_t len = 0;
    if (sock.select(this->options.timeout) == -1 || (socket_number = sock.accept_connection()) < 0) {
        this->server_file_set.del_file_from_set(file);
        this->server_file_set.free_space(bytes_to_download);
        if (fs::exists(this->options.shrd_fldr + file)) {
            fs::remove(this->options.shrd_fldr + file);
        }
        return;
    } else {
        bool open;
        fs::ofstream file_stream(this->options.shrd_fldr + file, std::ofstream::binary);
        if ((open = file_stream.is_open())) {
            char buffer[BUFFER_SIZE];
            while ((len = read(socket_number, buffer, std::min(to_download, sizeof(buffer)))) > 0) {
                file_stream.write(buffer, len);
                to_download -= len;
            }
        }
        if (!open || to_download != 0 || len < 0) {
            this->server_file_set.del_file_from_set(file);
            this->server_file_set.free_space(bytes_to_download);
            if (fs::exists(this->options.shrd_fldr + file)) {
                fs::remove(this->options.shrd_fldr + file);
            }
        }
    }
}

void Server::handle_add_request(sockaddr_in addr, uint64_t cmd_seq, uint64_t file_size, std::string file) {

    if (!file.empty() && file.find('/') == std::string::npos) {
        if (!this->server_file_set.reserve_space(file_size)) {
            simpl_cmd command(ADD_DENIED_RESPONSE, htobe64(cmd_seq), file.c_str());
            this->communication_socket.send_simpl_cmd(command, addr, file.length());
            return;
        }
        if (!this->server_file_set.add_file_to_set(file)) {
            this->server_file_set.free_space(file_size);
            simpl_cmd command(ADD_DENIED_RESPONSE, htobe64(cmd_seq), file.c_str());
            this->communication_socket.send_simpl_cmd(command, addr, file.length());
            return;
        }
        TCP_socket tcp_sock;
        if (!tcp_sock.init_socket() || !tcp_sock.bind_to_random_port() || (listen(tcp_sock.socket_number, QUEUE_LENGTH) < 0)) {
            this->server_file_set.free_space(file_size);
            this->server_file_set.del_file_from_set(file);
            return;
        }
        cmplx_cmd command(ADD_ACCEPTED_RESPONSE, htobe64(cmd_seq), htobe64(be16toh(tcp_sock.port_number)), "");
        if (!communication_socket.send_cmplx_cmd(command, addr, 0)) {
            this->server_file_set.free_space(file_size);
            this->server_file_set.del_file_from_set(file);
            return;
        }
        download_file(tcp_sock, file, file_size);
    }
}

void Server::handle_delete_request(std::string file) {

    if (this->server_file_set.del_file_from_set(file)) {
        try {
            this->server_file_set.free_space(fs::file_size(this->options.shrd_fldr + file));
            fs::remove(this->options.shrd_fldr + file);
        } catch(const boost::filesystem::filesystem_error &e) {}
    }
}


static bool compare_cmd(const char *cmd, const std::string &expected_cmd) {

    for (uint16_t i = 0; i < CMD_MAX_LENGTH; i++) {
        if (i < expected_cmd.length()) {
            if (cmd[i] != expected_cmd[i]) {
                return false;
            }
        } else {
            if (cmd[i] != '\0') {
                return false;
            }
        }
    }
    return true;
}

static std::string is_valid_package(const cmplx_cmd& command, size_t len) {

    if (len < EMPTY_SIMPL_CMD_LENGTH) {
        return "command too short";
    }
    if (compare_cmd(command.cmd, HELLO_RESPONSE)) {
        if (len != EMPTY_SIMPL_CMD_LENGTH) {
            return "hello command too long";
        }
    }
    if (compare_cmd(command.cmd, DELETE_REQUEST)) {
        if (len == EMPTY_SIMPL_CMD_LENGTH) {
            return "file to delete not specified";
        }
    }
    if (compare_cmd(command.cmd, DELETE_REQUEST)) {
        if (len == EMPTY_SIMPL_CMD_LENGTH) {
            return "file to delete not specified";
        }
    }
    if (compare_cmd(command.cmd, GET_REQUEST)) {
        if (len == EMPTY_SIMPL_CMD_LENGTH) {
            return "file to send not specified";
        }
    }
    if (compare_cmd(command.cmd, ADD_REQUEST)) {
        if (len < EMPTY_CMPLX_CMD_LENGTH) {
            return "command too short";
        }
        if (len == EMPTY_CMPLX_CMD_LENGTH) {
            return "file to save on server not specified";
        }
    }
    return "ok";
}

static void package_skipping(const std::string &ip, in_port_t port, std::string &message) {
    std::cerr << "[PCKG ERROR] Skipping invalid package from "<< ip <<":"<< port <<". " << message << std::endl;
}


void Server::run() {

    std::string ip;
    in_port_t port;
    cmplx_cmd_wrapper wrapper;

    for (;;) {

        if (!communication_socket.receive_cmplx_cmd(&wrapper)) {
            continue;
        }
        cmplx_cmd command = wrapper.command;
        ssize_t len = wrapper.length;
        sockaddr_in addr = wrapper.address;

        ip = inet_ntoa(addr.sin_addr);
        port = be16toh(addr.sin_port);
        std::string message;
        if ((message = is_valid_package(command, len)) != "ok") {
            package_skipping(ip, port, message);
            continue;
        }

        if (compare_cmd(command.cmd, HELLO_REQUEST)) {
            simpl_cmd *simpl_command = (simpl_cmd*)&command;
            std::thread t(&Server::handle_hello_request, this, std::ref(addr), be64toh(simpl_command->cmd_seq));
            t.detach();
        } else if (compare_cmd(command.cmd, LIST_REQUEST)) {
            simpl_cmd *simpl_command = (simpl_cmd*)&command;
            std::thread t(&Server::handle_list_request, this, std::ref(addr), be64toh(simpl_command->cmd_seq), simpl_command->data);
            t.detach();
        } else if (compare_cmd(command.cmd, GET_REQUEST)) {
            simpl_cmd *simpl_command = (simpl_cmd*)&command;
            if (!this->server_file_set.is_file_in_set(simpl_command->data)) {
                message = "server does not have the requested file";
                package_skipping(ip, port, message);
                continue;
            }
            std::thread t(&Server::handle_get_request, this, std::ref(addr), be64toh(simpl_command->cmd_seq), simpl_command->data);
            t.detach();
        } else if (compare_cmd(command.cmd, DELETE_REQUEST)) {
            simpl_cmd *simpl_command = (simpl_cmd*)&command;
            std::string file(simpl_command->data);
            handle_delete_request(file);
        } else if (compare_cmd(command.cmd, ADD_REQUEST)) {
            std::thread t(&Server::handle_add_request, this, std::ref(addr), be64toh(command.cmd_seq), be64toh(command.param), command.data);
            t.detach();
        }
    }
}

Server::Server(const server_options& options) : options(options) {

    this->server_file_set.max_space = options.max_space;
    this->server_file_set.space_taken = 0;
    if ((*(options.shrd_fldr.rend())) != '/') {
        this->options.shrd_fldr += '/';
    }
    if (this->options.shrd_fldr != "./" && this->options.shrd_fldr != "../") {
        fs::create_directories(this->options.shrd_fldr);
    }
    if (!fs::exists(this->options.shrd_fldr) || !fs::is_directory(this->options.shrd_fldr)) {
        std::cerr << "SHRD_FLDR directory doesn't exist" << std::endl;
        exit(1);
    }
    for (auto& file: fs::directory_iterator(this->options.shrd_fldr)) {
        if (fs::is_regular_file(file)) {
            this->server_file_set.space_taken += fs::file_size(file.path());
            this->server_file_set.files_list.insert(file.path().filename().string());
        }
    }
    signal(SIGPIPE, SIG_IGN);
    if (!this->communication_socket.init_standard_socket()) {
        std::cout << "Error while creating communication socket" << std::endl;
        exit(1);
    }
    ip_mreq ip_mreq{};
    ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (!this->communication_socket.set_reuse_address() ||
        inet_aton(this->options.mcast_addr.c_str(), &ip_mreq.imr_multiaddr) == 0 ||
        setsockopt(this->communication_socket.socket_number, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) &ip_mreq, sizeof ip_mreq) < 0 ||
        !this->communication_socket.bind_to_specific_port(htons(this->options.cmd_port))) {
        std::cout << "Error while setting up communication socket" << std::endl;
        exit(1);
    }
}
