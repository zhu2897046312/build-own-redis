#pragma once
#include <string>
#include <fstream>

class RDBReader {
public:
    static bool read_rdb_file(const std::string& path);

private:
    static void skip_length_encoded_string(std::ifstream& file);
    static std::string read_length_string(std::ifstream& file);
};