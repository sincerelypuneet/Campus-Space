#pragma once
#include <cstring>
#include <vector>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "models.h"
#include <chrono>
#include <random>

// Simple wrapper for JSON file storage
class Storage {
public:
    // Load a JSON array from file into a vector of T (must be compatible with nlohmann::json)
    template<typename T>
    static std::vector<T> load(const std::string& filename) {
        std::ifstream in(filename);
        if (!in) {
            // If file doesn't exist, return empty vector
            return {};
        }
        nlohmann::json j;
        try {
            in >> j;
        } catch (...) {
            // Corrupted file – treat as empty
            return {};
        }
        if (!j.is_array()) {
            return {};
        }
        return j.get<std::vector<T>>();
    }

    // Save a vector of T to file (overwrites)
    template<typename T>
    static bool save(const std::string& filename, const std::vector<T>& data) {
        std::ofstream out(filename, std::ios::trunc);
        if (!out) {
            std::cerr << "Failed to open " << filename << " for writing" << std::endl;
            return false;
        }
        nlohmann::json j = data;
        out << j.dump(4);
        out.flush();
        if (!out) {
            std::cerr << "Failed to write " << filename << std::endl;
            return false;
        }
        out.close();
        return !out.fail();
    }

    // Append a single record to a JSON array file
    template<typename T>
    static void append(const std::string& filename, const T& record) {
        auto vec = load<T>(filename);
        vec.push_back(record);
        save<T>(filename, vec);
    }

    // Generate a unique ID with a given prefix (e.g., "t-")
    static std::string generateId(const std::string& prefix) {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        return prefix + std::to_string(ms) + std::to_string(dis(gen));
    }
};
