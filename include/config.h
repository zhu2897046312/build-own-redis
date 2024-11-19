#pragma once
#include <string>

struct RedisConfig {
    std::string dir;
    std::string dbfilename;

    RedisConfig();
    void parse_args(int argc, char** argv);
};

extern RedisConfig config;