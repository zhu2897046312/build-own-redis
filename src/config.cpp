#include "config.h"

RedisConfig::RedisConfig() {
    dir = "/tmp/redis-data";
    dbfilename = "dump.rdb";
}

void RedisConfig::parse_args(int argc, char** argv) {
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

RedisConfig config;