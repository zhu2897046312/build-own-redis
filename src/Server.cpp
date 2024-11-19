#include "config.h"
#include "rdb_reader.h"
#include "command_handler.h"
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    config.parse_args(argc, argv);
    
    // 尝试加载 RDB 文件
    std::string rdb_path = config.dir + "/" + config.dbfilename;
    if (RDBReader::read_rdb_file(rdb_path)) {
        std::cout << "Successfully loaded RDB file: " << rdb_path << std::endl;
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);
    
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }
    
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }
    
    std::cout << "Server listening on port 6379...\n";
    
    std::vector<std::thread> client_threads;

    while (true) {
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }

        client_threads.emplace_back(handle_client, client_fd);
        client_threads.back().detach();
    }
    
    close(server_fd);
    return 0;
}