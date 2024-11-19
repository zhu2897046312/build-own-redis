#include "rdb_reader.h"
#include "value.h"
#include <iostream>

bool RDBReader::read_rdb_file(const std::string& path) {
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
            skip_length_encoded_string(file);
            skip_length_encoded_string(file);
            continue;
        }

        if (type == 0xFB) { // RESIZEDB
            skip_length_encoded_string(file);
            skip_length_encoded_string(file);
            continue;
        }

        bool has_expiry = false;
        std::chrono::steady_clock::time_point expiry;

        if (type == 0xFC || type == 0xFD) { // 过期时间
            uint64_t expire_time;
            file.read(reinterpret_cast<char*>(&expire_time), 8);

            // 获取当前时间
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ).count();

            // 计算过期时间（毫秒）
            uint64_t expire_ms = (type == 0xFC) ? expire_time * 1000 : expire_time;

            // 设置过期时间
            if (expire_ms > now_ms) {
                auto duration = std::chrono::milliseconds(expire_ms - now_ms);
                expiry = std::chrono::steady_clock::now() + duration;
                has_expiry = true;
            }

            // 读取实际的值类型
            file.read(reinterpret_cast<char*>(&type), 1);
        }

        // 读取键
        std::string key = read_length_string(file);
        if (key.empty()) continue;

        // 读取值
        if (type == 0) {
            std::string value = read_length_string(file);
            if (!value.empty()) {
                if (has_expiry) {
                    key_value_store[key] = ValueWithExpiry(value, expiry);
                } else {
                    key_value_store[key] = ValueWithExpiry(value);
                }
                std::cout << "Loaded key: " << key << ", value: " << value 
                          << (has_expiry ? " (with expiry)" : "") << std::endl;
            }
        }
    }

    return true;
}

void RDBReader::skip_length_encoded_string(std::ifstream& file) {
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

std::string RDBReader::read_length_string(std::ifstream& file) {
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