#include "value.h"

std::mutex store_mutex;
std::mutex cout_mutex;
std::unordered_map<std::string, ValueWithExpiry> key_value_store;

ValueWithExpiry::ValueWithExpiry() : value(""), has_expiry(false) {}

ValueWithExpiry::ValueWithExpiry(const std::string& v) : value(v), has_expiry(false) {}

ValueWithExpiry::ValueWithExpiry(const std::string& v, std::chrono::steady_clock::time_point exp) 
    : value(v), expiry(exp), has_expiry(true) {}

bool ValueWithExpiry::is_expired() const {
    return has_expiry && std::chrono::steady_clock::now() > expiry;
}