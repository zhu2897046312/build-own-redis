#pragma once
#include <string>
#include <vector>

std::vector<std::string> parse_command(const char* buffer);
std::string handle_keys_command(const std::string& pattern);
std::string handle_config_get(const std::string& param);
void handle_client(int client_fd);