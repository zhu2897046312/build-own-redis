#pragma once
#include <string>
#include <chrono>
#include <unordered_map>
#include <mutex>

struct ValueWithExpiry {
    std::string value;
    std::chrono::steady_clock::time_point expiry;
    bool has_expiry;

    ValueWithExpiry();
    ValueWithExpiry(const std::string& v);
    ValueWithExpiry(const std::string& v, std::chrono::steady_clock::time_point exp);
    bool is_expired() const;
};

extern std::mutex store_mutex;
extern std::mutex cout_mutex;
extern std::unordered_map<std::string, ValueWithExpiry> key_value_store;