#pragma once
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

struct SessionInfo {
    std::string user_id;
    std::string role;
    std::chrono::steady_clock::time_point expiry;
};

class AuthManager {
public:
    // Generate a token for a given user and role, valid for `duration_seconds`
    static std::string createToken(const std::string& user_id, const std::string& role, int duration_seconds = 3600);

    // Validate token; returns SessionInfo if valid, empty optional otherwise
    static std::optional<SessionInfo> validateToken(const std::string& token);

private:
    static std::unordered_map<std::string, SessionInfo> sessions;
    static std::mutex mtx;
    static std::string secret; // server secret for token signing (simple Base64 for demo)
    static std::string sign(const std::string& data);
    static bool verify(const std::string& data, const std::string& signature);
};

