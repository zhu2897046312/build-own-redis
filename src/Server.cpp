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
#include <unordered_map>

std::mutex cout_mutex;
std::mutex store_mutex;
std::unordered_map<std::string, std::string> key_value_store;

// 解析 RESP 命令
std::vector<std::string> parse_command(const char* buffer) {
    std::vector<std::string> parts;
    const char* p = buffer;
    
    // 跳过数组长度 (*n\r\n)
    while (*p && *p != '\n') p++;
    if (*p) p++;
    
    while (*p) {
        // 跳过字符串长度标记 ($n\r\n)
        if (*p == '$') {
            while (*p && *p != '\n') p++;
            if (*p) p++;
            
            std::string part;
            // 读取实际内容直到 \r
            while (*p && *p != '\r') {
                part += *p++;
            }
            parts.push_back(part);
            
            // 跳过 \r\n
            if (*p) p += 2;
        } else {
            break;
        }
    }
    
    return parts;
}

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

        auto parts = parse_command(buffer);
        if (parts.empty()) continue;

        std::string cmd = parts[0];
        for (char& c : cmd) c = toupper(c);  // 命令转换为大写

        if (cmd == "PING") {
            const char* response = "+PONG\r\n";
            send(client_fd, response, strlen(response), 0);
        }
        else if (cmd == "ECHO" && parts.size() > 1) {
            std::string response = "$" + std::to_string(parts[1].length()) + "\r\n" + parts[1] + "\r\n";
            send(client_fd, response.c_str(), response.length(), 0);
        }
        else if (cmd == "SET" && parts.size() >= 3) {
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                key_value_store[parts[1]] = parts[2];
            }
            const char* response = "+OK\r\n";
            send(client_fd, response, strlen(response), 0);
        }
        else if (cmd == "GET" && parts.size() >= 2) {
            std::string value;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = key_value_store.find(parts[1]);
                if (it != key_value_store.end()) {
                    value = it->second;
                }
            }
            
            if (!value.empty()) {
                std::string response = "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
                send(client_fd, response.c_str(), response.length(), 0);
            } else {
                const char* response = "$-1\r\n";  // Redis 用 $-1\r\n 表示 nil
                send(client_fd, response, strlen(response), 0);
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

        client_threads.emplace_back(handle_client, client_fd);
        client_threads.back().detach();
    }
    
    close(server_fd);
    return 0;
}
