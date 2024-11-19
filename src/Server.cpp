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
#include <fstream>

std::mutex cout_mutex;
std::mutex store_mutex;

// Redis 配置
struct RedisConfig {
    std::string dir;
    std::string dbfilename;

    RedisConfig() {
        dir = "/tmp/redis-data";
        dbfilename = "dump.rdb";
    }

    void parse_args(int argc, char** argv) {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--dir" && i + 1 < argc) {
                dir = argv[++i];
            }
            else if (arg == "--dbfilename" && i + 1 < argc) {
                dbfilename = argv[++i];
            }
        }
    }
};

RedisConfig config;

// 存储键值对和过期时间
struct ValueWithExpiry {
    std::string value;
    std::chrono::steady_clock::time_point expiry;
    bool has_expiry;

    ValueWithExpiry() : value(""), has_expiry(false) {}
    ValueWithExpiry(const std::string& v) : value(v), has_expiry(false) {}
    ValueWithExpiry(const std::string& v, std::chrono::steady_clock::time_point exp) 
        : value(v), expiry(exp), has_expiry(true) {}
    
    bool is_expired() const {
        return has_expiry && std::chrono::steady_clock::now() > expiry;
    }
};

std::unordered_map<std::string, ValueWithExpiry> key_value_store;

// RDB 文件读取类
class RDBReader {
public:
    static bool read_rdb_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open RDB file: " << path << std::endl;
            return false;
        }

        // 读取 REDIS 魔数
        char magic[5];
        file.read(magic, 5);
        if (std::string(magic, 5) != "REDIS") {
            std::cerr << "Invalid RDB file format" << std::endl;
            return false;
        }

        // 读取版本号（4字节）
        char version[4];
        file.read(version, 4);

        // 读取键值对
        while (!file.eof()) {
            uint8_t type;
            file.read(reinterpret_cast<char*>(&type), 1);
            if (file.eof()) break;

            if (type == 0xFF) { // EOF
                break;
            }

            if (type == 0xFE) { // 选择数据库
                uint8_t dbnum;
                file.read(reinterpret_cast<char*>(&dbnum), 1);
                continue;
            }

            if (type == 0xFA) { // 辅助字段
                skip_length_encoded_string(file); // 跳过 key
                skip_length_encoded_string(file); // 跳过 value
                continue;
            }

            if (type == 0xFB) { // RESIZEDB
                skip_length_encoded_string(file); // 跳过 db_size
                skip_length_encoded_string(file); // 跳过 expires_size
                continue;
            }

            // 读取键
            std::string key = read_length_string(file);
            if (key.empty()) {
                continue;
            }

            // 读取值（type == 0 表示字符串类型）
            if (type == 0) {
                std::string value = read_length_string(file);
                if (!value.empty()) {
                    std::cout << "Loaded key: " << key << ", value: " << value << std::endl;
                    key_value_store[key] = ValueWithExpiry(value);
                }
            }
        }

        return true;
    }

private:
    static void skip_length_encoded_string(std::ifstream& file) {
        uint8_t byte;
        file.read(reinterpret_cast<char*>(&byte), 1);
        
        if ((byte & 0xC0) == 0) { // 6 位长度
            file.seekg(byte & 0x3F, std::ios::cur);
        }
        else if ((byte & 0xC0) == 0x40) { // 14 位长度
            uint8_t next_byte;
            file.read(reinterpret_cast<char*>(&next_byte), 1);
            uint16_t length = ((byte & 0x3F) << 8) | next_byte;
            file.seekg(length, std::ios::cur);
        }
    }

    static std::string read_length_string(std::ifstream& file) {
        uint8_t byte;
        file.read(reinterpret_cast<char*>(&byte), 1);
        
        if ((byte & 0xC0) == 0) { // 6 位长度
            uint32_t length = byte & 0x3F;
            std::string result(length, '\0');
            file.read(&result[0], length);
            return result;
        }
        else if ((byte & 0xC0) == 0x40) { // 14 位长度
            uint8_t next_byte;
            file.read(reinterpret_cast<char*>(&next_byte), 1);
            uint16_t length = ((byte & 0x3F) << 8) | next_byte;
            std::string result(length, '\0');
            file.read(&result[0], length);
            return result;
        }
        
        return "";
    }
};

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

// 处理 KEYS 命令
std::string handle_keys_command(const std::string& pattern) {
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(store_mutex);
        for (const auto& pair : key_value_store) {
            if (!pair.second.is_expired()) {
                if (pattern == "*") {
                    keys.push_back(pair.first);
                }
            }
        }
    }

    // 构建 RESP 数组响应
    std::string response = "*" + std::to_string(keys.size()) + "\r\n";
    for (const auto& key : keys) {
        response += "$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
    }
    return response;
}

// 处理 CONFIG GET 命令
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
        // 如果参数不存在，返回空数组
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
            // 处理 ECHO 命令
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
        else if (cmd == "KEYS" && parts.size() >= 2) {
            std::string response = handle_keys_command(parts[1]);
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
    }

    close(client_fd);
}

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
    
    while (true) {
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }

        std::thread(handle_client, client_fd).detach();
    }
    
    close(server_fd);
    return 0;
}
