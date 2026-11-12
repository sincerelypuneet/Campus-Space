#include <httplib.h>
#include <nlohmann/json.hpp>
#include "auth.h"
#include "storage.h"
#include "models.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h> // for HMAC SHA256 (if available)

using json = nlohmann::json;

// Initialize static members
std::unordered_map<std::string, SessionInfo> AuthManager::sessions;
std::mutex AuthManager::mtx;
std::string AuthManager::secret = "SuperSecretKeyChangeMe"; // In production, load from env

static std::string base64Encode(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(tbl[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::string base64Decode(const std::string& in) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    std::string out;
    std::vector<int> val(4);
    int valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        valb += 6;
        val[valb/8] = (val[valb/8] << 6) | T[c];
        if (valb >= 0) {
            out.push_back(char((val[0] >> 16) & 0xFF));
            out.push_back(char((val[0] >> 8) & 0xFF));
            out.push_back(char(val[0] & 0xFF));
            valb = -8;
        }
    }
    return out;
}

static std::string hmacSha256(const std::string& data, const std::string& key) {
    unsigned char* result;
    unsigned int len = 32;
    result = HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
                  reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), nullptr, nullptr);
    return std::string(reinterpret_cast<char*>(result), len);
}

std::string AuthManager::sign(const std::string& data) {
    std::string sig = hmacSha256(data, secret);
    return base64Encode(sig);
}

bool AuthManager::verify(const std::string& data, const std::string& signature) {
    std::string expected = sign(data);
    return expected == signature;
}

std::string AuthManager::createToken(const std::string& user_id, const std::string& role, int duration_seconds) {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(duration_seconds);
    std::time_t exp_time = std::chrono::system_clock::to_time_t(exp);
    std::ostringstream payload;
    payload << user_id << "|" << role << "|" << exp_time;
    std::string data = payload.str();
    std::string signature = sign(data);
    std::string token = base64Encode(data) + "." + signature;
    // Store session for quick validation (optional)
    {
        std::lock_guard<std::mutex> lock(mtx);
        sessions[token] = {user_id, role, std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds)};
    }
    return token;
}

std::optional<SessionInfo> AuthManager::validateToken(const std::string& token) {
    auto pos = token.find('.');
    if (pos == std::string::npos) return {};
    std::string encData = token.substr(0, pos);
    std::string signature = token.substr(pos + 1);
    std::string data = base64Decode(encData);
    if (!verify(data, signature)) return {};
    // parse data
    std::istringstream ss(data);
    std::string uid, role, expStr;
    if (!std::getline(ss, uid, '|')) return {};
    if (!std::getline(ss, role, '|')) return {};
    if (!std::getline(ss, expStr, '|')) return {};
    std::time_t expTime = std::stoll(expStr);
    if (std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) > expTime) return {};
    // optional session map check (cleanup expired)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = sessions.find(token);
        if (it != sessions.end()) {
            if (std::chrono::steady_clock::now() > it->second.expiry) {
                sessions.erase(it);
                return {};
            }
            return it->second;
        }
    }
    // If not in map, still consider valid
    return SessionInfo{uid, role, std::chrono::steady_clock::now() + std::chrono::seconds(3600)};
}

