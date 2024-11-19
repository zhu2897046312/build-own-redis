#include "command_handler.h"
#include "config.h"
#include "value.h"
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring> 

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

std::string handle_config_get(const std::string& param) {
    std::string response;
    if (param == "dir") {
        response = "*2\r\n$3\r\ndir\r\n$" + 
                  std::to_string(config.dir.length()) + "\r\n" + 
                  config.dir + "\r\n";
    }
    else if (param == "dbfilename") {
        response = "*2\r\n$9\r\ndbfilename\r\n$" + 
                  std::to_string(config.dbfilename.length()) + "\r\n" + 
                  config.dbfilename + "\r\n";
    }
    else {
        response = "*0\r\n";
    }
    return response;
}

void handle_client(int client_fd) {
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
        else if (cmd == "CONFIG" && parts.size() >= 3) {
            std::string subcmd = parts[1];
            for (char& c : subcmd) c = toupper(c);
            
            if (subcmd == "GET" && parts.size() >= 3) {
                std::string response = handle_config_get(parts[2]);
                send(client_fd, response.c_str(), response.length(), 0);
            }
        }
        else if (cmd == "GET" && parts.size() >= 2) {
            std::string value;
            bool key_exists = false;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = key_value_store.find(parts[1]);
                if (it != key_value_store.end() && !it->second.is_expired()) {
                    value = it->second.value;
                    key_exists = true;
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
}