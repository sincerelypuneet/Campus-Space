cmake_minimum_required(VERSION 3.15)
project(CampusSpaceBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch header‑only dependencies
include(FetchContent)

# cpp‑httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.17.2
)
FetchContent_MakeAvailable(httplib)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# bcrypt is header-only
FetchContent_Declare(
    bcrypt
    GIT_REPOSITORY https://github.com/rg3/bcrypt.git
    GIT_TAG master
)
FetchContent_MakeAvailable(bcrypt)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${bcrypt_SOURCE_DIR})

file(GLOB_RECURSE SRC_FILES src/*.cpp src/services/*.cpp)
# Build the user-management utility separately from the server.
list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/user_add.cpp)
add_executable(UserAdd src/user_add.cpp)
# Link JSON to UserAdd
target_link_libraries(UserAdd PRIVATE nlohmann_json::nlohmann_json)
add_executable(CampusSpaceBackend ${SRC_FILES})

# Link the fetched libraries to the main server
target_link_libraries(CampusSpaceBackend PRIVATE httplib nlohmann_json::nlohmann_json)
