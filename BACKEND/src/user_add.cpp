#include <iostream>
#include <string>
#include <bcrypt/BCrypt.hpp>
#include "storage.h"
#include "models.h"

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: user_add <username> <password> <role>\n";
        return 1;
    }
    std::string username = argv[1];
    std::string password = argv[2];
    std::string role = argv[3]; // "admin" or "teacher" or "student"

    // Generate bcrypt hash (cost=12)
    std::string hash = BCrypt::generateHash(password);
    // Load existing users
    auto users = Storage::load<User>("data/users.json");
    // Ensure unique username
    for (const auto& u : users) {
        if (u.username == username) {
            std::cerr << "User with that username already exists.\n";
            return 1;
        }
    }
    // Create new user entry
    User newUser;
    newUser.id = Storage::generateId("u-");
    newUser.username = username;
    newUser.password_hash = hash;
    newUser.role = role;
    // Append to file
    Storage::append<User>("data/users.json", newUser);
    std::cout << "User added successfully. ID=" << newUser.id << std::endl;
    return 0;
}

