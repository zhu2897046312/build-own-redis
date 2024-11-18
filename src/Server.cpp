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
#include <chrono>

std::mutex cout_mutex;
std::mutex store_mutex;

// 存储键值对和过期时间
struct ValueWithExpiry {
    std::string value;
    std::chrono::steady_clock::time_point expiry;
    bool has_expiry;

    ValueWithExpiry() : value(""), has_expiry(false) {}
    
    ValueWithExpiry(const std::string& v) 
        : value(v), has_expiry(false) {}
    
    ValueWithExpiry(const std::string& v, std::chrono::steady_clock::time_point exp) 
        : value(v), expiry(exp), has_expiry(true) {}
    
    bool is_expired() const {
        return has_expiry && std::chrono::steady_clock::now() > expiry;
    }
};

std::unordered_map<std::string, ValueWithExpiry> key_value_store;

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
        for (char& c : cmd) c = toupper(c);

        if (cmd == "PING") {
            const char* response = "+PONG\r\n";
            send(client_fd, response, strlen(response), 0);
        }
        else if (cmd == "ECHO" && parts.size() > 1) {
            std::string response = "$" + std::to_string(parts[1].length()) + "\r\n" + parts[1] + "\r\n";
            send(client_fd, response.c_str(), response.length(), 0);
        }
        else if (cmd == "SET" && parts.size() >= 3) {
            std::string key = parts[1];
            std::string value = parts[2];
            
            // 检查是否有 PX 参数
            bool has_px = false;
            long long px_value = 0;
            if (parts.size() >= 5) {
                std::string option = parts[3];
                for (char& c : option) c = toupper(c);
                if (option == "PX" && parts.size() >= 5) {
                    try {
                        px_value = std::stoll(parts[4]);
                        has_px = true;
                    } catch (...) {
                        // 忽略无效的 PX 值
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(store_mutex);
                if (has_px) {
                    auto expiry = std::chrono::steady_clock::now() + 
                                std::chrono::milliseconds(px_value);
                    key_value_store[key] = ValueWithExpiry(value, expiry);
                } else {
                    key_value_store[key] = ValueWithExpiry(value);
                }
            }
            
            const char* response = "+OK\r\n";
            send(client_fd, response, strlen(response), 0);
        }
        else if (cmd == "GET" && parts.size() >= 2) {
            std::string value;
            bool key_exists = false;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = key_value_store.find(parts[1]);
                if (it != key_value_store.end()) {
                    if (!it->second.is_expired()) {
                        value = it->second.value;
                        key_exists = true;
                    } else {
                        // 删除过期的键
                        key_value_store.erase(it);
                    }
                }
            }
            
            if (key_exists) {
                std::string response = "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
                send(client_fd, response.c_str(), response.length(), 0);
            } else {
                const char* response = "$-1\r\n";
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
