#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <vector>
#include <mutex>

std::mutex cout_mutex; // 用于同步输出

// 处理单个客户端连接的函数
void handle_client(int client_fd) {
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "New client thread started for client " << client_fd << std::endl;
    }

    while (true) {
        char buffer[1024] = {0};
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            break;
        }

        // 检查是否是 PING 命令
        if (strstr(buffer, "PING") != nullptr) {
            const char* response = "+PONG\r\n";
            send(client_fd, response, strlen(response), 0);
            
            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Sent PONG to client " << client_fd << std::endl;
            }
        }
    }

    close(client_fd);
    
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Client " << client_fd << " disconnected" << std::endl;
    }
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
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

        // 为每个新客户端创建一个新线程
        client_threads.emplace_back(handle_client, client_fd);
        // 分离线程，让它独立运行
        client_threads.back().detach();
    }
    
    close(server_fd);
    return 0;
}
