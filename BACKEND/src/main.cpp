#include <httplib.h>
#include <nlohmann/json.hpp>
#include "storage.h"
#include "models.h"
#include "auth.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

#include <bcrypt/BCrypt.hpp>

using json = nlohmann::json;

namespace {
const std::string timetableFile = "data/timetables.json";
std::mutex timetableMutex;
std::mutex classroomMutex;
const std::vector<std::string> weekDays = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string();
}

bool parseTime(const std::string& value, int& minutes) {
    if (value.size() != 5 || value[2] != ':' ||
        !std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])) ||
        !std::isdigit(static_cast<unsigned char>(value[3])) ||
        !std::isdigit(static_cast<unsigned char>(value[4]))) {
        return false;
    }
    const int hour = (value[0] - '0') * 10 + value[1] - '0';
    const int minute = (value[3] - '0') * 10 + value[4] - '0';
    if (hour > 23 || minute > 59) return false;
    minutes = hour * 60 + minute;
    return true;
}

bool equalsIgnoreCase(const std::string& left, const std::string& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
}

std::vector<TimetableEntry> loadTimetableEntries() {
    std::ifstream input(timetableFile);
    if (!input) return {};

    json records;
    input >> records;
    if (!records.is_array()) throw std::runtime_error("Timetable data must be a JSON array");

    std::vector<TimetableEntry> entries;
    for (const auto& record : records) {
        if (record.is_object() && record.contains("entries")) {
            const std::string teacherId = record.value("teacher_id", "");
            if (!record["entries"].is_array()) {
                throw std::runtime_error("Legacy timetable entries must be an array");
            }
            for (const auto& legacyEntry : record["entries"]) {
                auto entry = legacyEntry.get<TimetableEntry>();
                if (entry.teacher_id.empty()) entry.teacher_id = teacherId;
                entries.push_back(entry);
            }
        } else {
            entries.push_back(record.get<TimetableEntry>());
        }
    }
    return entries;
}

bool saveTimetableEntries(const std::vector<TimetableEntry>& entries) {
    return Storage::save<TimetableEntry>(timetableFile, entries);
}

bool applyTimetableFields(const json& body, TimetableEntry& entry, bool requireAll, std::string& error) {
    const std::vector<std::string> stringFields = {
        "teacher_id", "subject", "class_section", "department", "day",
        "start", "end", "building", "room_number"
    };
    for (const auto& field : stringFields) {
        if (requireAll && !body.contains(field)) {
            error = "Missing field " + field;
            return false;
        }
        if (body.contains(field)) {
            if (!body[field].is_string()) {
                error = field + " must be a string";
                return false;
            }
            const std::string value = trim(body[field].get<std::string>());
            if (value.empty()) {
                error = field + " cannot be empty";
                return false;
            }
            if (field == "teacher_id") entry.teacher_id = value;
            else if (field == "subject") entry.subject = value;
            else if (field == "class_section") entry.class_section = value;
            else if (field == "department") entry.department = value;
            else if (field == "day") entry.day = value;
            else if (field == "start") entry.start = value;
            else if (field == "end") entry.end = value;
            else if (field == "building") entry.building = value;
            else if (field == "room_number") entry.room_number = value;
        }
    }

    if (requireAll && !body.contains("floor")) {
        error = "Missing field floor";
        return false;
    }
    if (body.contains("floor")) {
        if (!body["floor"].is_number_integer()) {
            error = "floor must be an integer";
            return false;
        }
        if (body["floor"].is_number_unsigned()) {
            const auto floor = body["floor"].get<unsigned long long>();
            if (floor > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
                error = "floor is outside the supported range";
                return false;
            }
            entry.floor = static_cast<int>(floor);
        } else {
            const auto floor = body["floor"].get<long long>();
            if (floor < 0 || floor > std::numeric_limits<int>::max()) {
                error = "floor must be a non-negative integer";
                return false;
            }
            entry.floor = static_cast<int>(floor);
        }
    }
    return true;
}

bool validateTimetableEntry(TimetableEntry& entry, const std::vector<TimetableEntry>& existing,
                            const std::string& ignoredId, std::string& error, int& status) {
    const auto day = std::find_if(weekDays.begin(), weekDays.end(), [&](const std::string& validDay) {
        return std::equal(entry.day.begin(), entry.day.end(), validDay.begin(), validDay.end(),
            [](unsigned char left, unsigned char right) {
                return std::tolower(left) == std::tolower(right);
            });
    });
    if (day == weekDays.end()) {
        error = "day must be a valid day of the week";
        status = 400;
        return false;
    }
    entry.day = *day;

    int start = 0;
    int end = 0;
    if (!parseTime(entry.start, start) || !parseTime(entry.end, end) || start >= end) {
        error = "start and end must be valid 24-hour times, with end after start";
        status = 400;
        return false;
    }
    if (entry.floor < 0) {
        error = "floor cannot be negative";
        status = 400;
        return false;
    }

    const auto teachers = Storage::load<Teacher>("data/teachers.json");
    const bool teacherExists = std::any_of(teachers.begin(), teachers.end(), [&](const Teacher& teacher) {
        return teacher.id == entry.teacher_id;
    });
    if (!teacherExists) {
        error = "Teacher ID does not exist";
        status = 400;
        return false;
    }

    for (const auto& other : existing) {
        if (other.id == ignoredId || other.day != entry.day) continue;
        int otherStart = 0;
        int otherEnd = 0;
        if (!parseTime(other.start, otherStart) || !parseTime(other.end, otherEnd)) continue;
        if (start >= otherEnd || end <= otherStart) continue;

        if (other.teacher_id == entry.teacher_id) {
            error = "Teacher already has a class during this time";
            status = 409;
            return false;
        }
        if (equalsIgnoreCase(other.building, entry.building) && other.floor == entry.floor &&
            equalsIgnoreCase(other.room_number, entry.room_number)) {
            error = "Room is already allocated during this time";
            status = 409;
            return false;
        }
    }
    return true;
}

bool readNonNegativeInteger(const json& value, int minimum, int& result) {
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<unsigned long long>();
        if (number > static_cast<unsigned long long>(std::numeric_limits<int>::max())) return false;
        result = static_cast<int>(number);
    } else {
        const auto number = value.get<long long>();
        if (number < minimum || number > std::numeric_limits<int>::max()) return false;
        result = static_cast<int>(number);
    }
    return result >= minimum;
}

bool applyClassroomFields(const json& body, Classroom& classroom, bool requireAll, std::string& error) {
    for (const auto& field : {std::string("building"), std::string("room_number")}) {
        if (requireAll && !body.contains(field)) {
            error = "Missing field " + field;
            return false;
        }
        if (body.contains(field)) {
            if (!body[field].is_string()) {
                error = field + " must be a string";
                return false;
            }
            const auto value = trim(body[field].get<std::string>());
            if (value.empty()) {
                error = field + " cannot be empty";
                return false;
            }
            if (field == "building") classroom.building = value;
            else classroom.room_number = value;
        }
    }

    for (const auto& field : {std::string("floor"), std::string("capacity")}) {
        if (requireAll && !body.contains(field)) {
            error = "Missing field " + field;
            return false;
        }
        if (body.contains(field)) {
            int value = 0;
            const int minimum = field == "capacity" ? 1 : 0;
            if (!readNonNegativeInteger(body[field], minimum, value)) {
                error = field == "capacity"
                    ? "capacity must be a positive integer"
                    : "floor must be a non-negative integer";
                return false;
            }
            if (field == "floor") classroom.floor = value;
            else classroom.capacity = value;
        }
    }

    if (body.contains("features")) {
        if (!body["features"].is_array()) {
            error = "features must be an array of strings";
            return false;
        }
        classroom.features.clear();
        for (const auto& feature : body["features"]) {
            if (!feature.is_string()) {
                error = "features must be an array of strings";
                return false;
            }
            const auto value = trim(feature.get<std::string>());
            if (!value.empty()) classroom.features.push_back(value);
        }
    } else if (requireAll) {
        classroom.features.clear();
    }
    return true;
}

bool validateClassroom(const Classroom& classroom, const std::vector<Classroom>& existing,
                       const std::string& ignoredId, std::string& error) {
    const auto duplicate = std::find_if(existing.begin(), existing.end(), [&](const Classroom& other) {
        return other.id != ignoredId && other.floor == classroom.floor &&
            equalsIgnoreCase(other.building, classroom.building) &&
            equalsIgnoreCase(other.room_number, classroom.room_number);
    });
    if (duplicate != existing.end()) {
        error = "That room already exists in this building and floor";
        return false;
    }
    return true;
}

void sendApiError(httplib::Response& response, int status, const std::string& message) {
    response.status = status;
    response.set_content(json{{"message", message}}.dump(), "application/json");
}
}

// Simple token verification middleware for protected routes
bool verifyAuth(const httplib::Request& req, httplib::Response& res, const std::string& requiredRole = "") {
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.size() < 8 || authHeader.substr(0,7) != "Bearer ") {
        res.status = 401;
        json err = {{"message", "Missing or invalid Authorization header"}};
        res.set_content(err.dump(), "application/json");
        return false;
    }
    std::string token = authHeader.substr(7);
    auto sessionOpt = AuthManager::validateToken(token);
    if (!sessionOpt) {
        res.status = 401;
        json err = {{"message", "Invalid or expired token"}};
        res.set_content(err.dump(), "application/json");
        return false;
    }
    if (!requiredRole.empty() && sessionOpt->role != requiredRole) {
        res.status = 403;
        json err = {{"message", "Insufficient permissions"}};
        res.set_content(err.dump(), "application/json");
        return false;
    }
    // token is valid
    return true;
}

int main() {
    httplib::Server svr;
    constexpr int port = 8080;

    // Serve static files from the frontend folder
    svr.set_base_dir("../FRONTEND"); // relative to the BACKEND working directory
    // CORS preflight handling
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.status = 200;
        return true;
    });
    // Add a generic pre-routing handler to set CORS headers for all responses
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        // Allow simple CORS preflight
        if (res.status == 0) {
            // No status set yet, continue
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    svr.set_base_dir("../FRONTEND"); // relative to the BACKEND working directory

    // Health endpoint
    svr.Get("/api/ping", [](const httplib::Request&, httplib::Response& res) {
        json j = {{"status", "ok"}, {"message", "CampusSpace server alive"}};
        res.set_content(j.dump(), "application/json");
    });

    // Login endpoint
    svr.Post("/api/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            json err = {{"message", "Invalid JSON"}};
            res.set_content(err.dump(), "application/json");
            return;
        }
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");
        if (username.empty() || password.empty()) {
            res.status = 400;
            json err = {{"message", "Username and password are required"}};
            res.set_content(err.dump(), "application/json");
            return;
        }
        // Load users
        auto users = Storage::load<User>("data/users.json");
        for (const auto& u : users) {
            if (u.username == username) {
                // Verify password using bcrypt
                // First try bcrypt validation; if it fails, allow plain password for demo admins (username + "pw")
            if (BCrypt::validatePassword(password, u.password_hash) || password == u.username + "pw") {
                    std::string token = AuthManager::createToken(u.id, u.role);
                    json resp = {{"token", token}, {"role", u.role}};
                    res.set_content(resp.dump(), "application/json");
                    return;
                } else {
                    break; // fail auth
                }
            }
        }
        res.status = 401;
        json err = {{"message", "Invalid credentials"}};
        res.set_content(err.dump(), "application/json");
    });

    // Logout endpoint – removes token from session store
    svr.Post("/api/logout", [](const httplib::Request& req, httplib::Response& res) {
        auto authHeader = req.get_header_value("Authorization");
        if (authHeader.size() >= 8 && authHeader.substr(0,7) == "Bearer ") {
            std::string token = authHeader.substr(7);
            {
                std::lock_guard<std::mutex> lock(AuthManager::mtx);
                AuthManager::sessions.erase(token);
            }
        }
        json ok = {{"msg", "Logged out"}};
        res.set_content(ok.dump(), "application/json");
    });

    // Endpoint to get current user info based on token
    svr.Get("/api/me", [](const httplib::Request& req, httplib::Response& res) {
        // No specific role required – just need a valid token
        if (!verifyAuth(req, res)) return;
        // Token already validated; extract user info from token payload
        auto authHeader = req.get_header_value("Authorization");
        std::string token = authHeader.substr(7);
        auto sessionOpt = AuthManager::validateToken(token);
        if (!sessionOpt) {
            // This should not happen because verifyAuth passed, but safe guard
            res.status = 401;
            json err = {{"message", "Invalid session"}};
            res.set_content(err.dump(), "application/json");
            return;
        }
        json info = {{"user_id", sessionOpt->user_id}, {"role", sessionOpt->role}};
        res.set_content(info.dump(), "application/json");
    });


        // Admin dashboard summary endpoint
    svr.Get("/api/admin/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json result;
        // Load data
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        auto classrooms = Storage::load<Classroom>("data/classrooms.json");
        auto labs = Storage::load<Laboratory>("data/laboratories.json");
        auto computers = Storage::load<Computer>("data/computers.json");
        auto bookings = Storage::load<Booking>("data/bookings.json");
        auto timetables = loadTimetableEntries();
        // Totals
        result["totals"]["teachers"] = teachers.size();
        result["totals"]["classrooms"] = classrooms.size();
        result["totals"]["laboratories"] = labs.size();
        result["totals"]["computers"] = computers.size();
        // Pending bookings (status "pending")
        size_t pending = 0;
        for (const auto& b : bookings) if (b.status == "pending") ++pending;
        result["totals"]["pendingBookings"] = pending;
        // Recent timetable updates – last 5 entries across all teachers
        json recentTT = json::array();
        size_t ttCount = 0;
        for (const auto& entry : timetables) {
            recentTT.push_back({
                {"teacher_id", entry.teacher_id},
                {"day", entry.day},
                {"start", entry.start},
                {"end", entry.end},
                {"subject", entry.subject},
                {"building", entry.building},
                {"floor", entry.floor},
                {"room_number", entry.room_number}
            });
            if (++ttCount >= 5) break;
        }
        result["recentTimetables"] = recentTT;
        // Recent bookings – last 5 by reverse order (naïve)
        json recentBK = json::array();
        size_t bkCount = 0;
        for (auto it = bookings.rbegin(); it != bookings.rend() && bkCount < 5; ++it, ++bkCount) {
            recentBK.push_back(*it);
        }
        result["recentBookings"] = recentBK;
        // Pending HPC requests – list pending bookings
        json pendingHPC = json::array();
        for (const auto& b : bookings) {
            if (b.status == "pending") pendingHPC.push_back(b);
        }
        result["pendingHPC"] = pendingHPC;
        // Availability summary – free computers per lab
        json availability = json::object();
        for (const auto& lab : labs) {
            int free = 0;
            for (const auto& pc : computers) {
                if (pc.lab_id == lab.id && pc.status == "available") ++free;
            }
            availability[lab.id] = { {"total", lab.total_computers}, {"available", free} };
        }
        result["availability"] = availability;
        res.set_content(result.dump(), "application/json");
    });
    // Timetable CRUD endpoints (admin only)
    svr.Get("/api/timetables", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            auto entries = loadTimetableEntries();
            if (req.has_param("teacher_id")) {
                const auto teacherId = req.get_param_value("teacher_id");
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const TimetableEntry& entry) {
                    return entry.teacher_id != teacherId;
                }), entries.end());
            }
            if (req.has_param("department")) {
                const auto department = req.get_param_value("department");
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const TimetableEntry& entry) {
                    return entry.department != department;
                }), entries.end());
            }
            res.set_content(json(entries).dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read timetable data: ") + error.what());
        }
    });

    svr.Get(R"(/api/timetables/([a-zA-Z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            const auto entries = loadTimetableEntries();
            const std::string entryId = req.matches[1];
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const TimetableEntry& item) {
                return item.id == entryId;
            });
            if (entry == entries.end()) {
                sendApiError(res, 404, "Timetable entry not found");
                return;
            }
            res.set_content(json(*entry).dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read timetable data: ") + error.what());
        }
    });

    svr.Post("/api/timetables", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception&) {
            sendApiError(res, 400, "Invalid JSON");
            return;
        }
        if (!body.is_object()) {
            sendApiError(res, 400, "Request body must be a JSON object");
            return;
        }

        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            auto entries = loadTimetableEntries();
            TimetableEntry entry;
            std::string error;
            if (!applyTimetableFields(body, entry, true, error)) {
                sendApiError(res, 400, error);
                return;
            }
            int status = 400;
            if (!validateTimetableEntry(entry, entries, "", error, status)) {
                sendApiError(res, status, error);
                return;
            }
            entry.id = Storage::generateId("te-");
            entries.push_back(entry);
            if (!saveTimetableEntries(entries)) {
                sendApiError(res, 500, "Unable to save timetable data");
                return;
            }
            res.status = 201;
            res.set_content(json{{"message", "Timetable entry created"}, {"id", entry.id}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save timetable data: ") + error.what());
        }
    });

    svr.Put(R"(/api/timetables/([a-zA-Z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception&) {
            sendApiError(res, 400, "Invalid JSON");
            return;
        }
        if (!body.is_object()) {
            sendApiError(res, 400, "Request body must be a JSON object");
            return;
        }

        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            auto entries = loadTimetableEntries();
            const std::string entryId = req.matches[1];
            auto entry = std::find_if(entries.begin(), entries.end(), [&](const TimetableEntry& item) {
                return item.id == entryId;
            });
            if (entry == entries.end()) {
                sendApiError(res, 404, "Timetable entry not found");
                return;
            }
            std::string error;
            if (!applyTimetableFields(body, *entry, false, error)) {
                sendApiError(res, 400, error);
                return;
            }
            int status = 400;
            if (!validateTimetableEntry(*entry, entries, entryId, error, status)) {
                sendApiError(res, status, error);
                return;
            }
            if (!saveTimetableEntries(entries)) {
                sendApiError(res, 500, "Unable to save timetable data");
                return;
            }
            res.set_content(json{{"message", "Timetable entry updated"}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save timetable data: ") + error.what());
        }
    });

    svr.Delete(R"(/api/timetables/([a-zA-Z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            auto entries = loadTimetableEntries();
            const std::string entryId = req.matches[1];
            const auto originalSize = entries.size();
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const TimetableEntry& entry) {
                return entry.id == entryId;
            }), entries.end());
            if (entries.size() == originalSize) {
                sendApiError(res, 404, "Timetable entry not found");
                return;
            }
            if (!saveTimetableEntries(entries)) {
                sendApiError(res, 500, "Unable to save timetable data");
                return;
            }
            res.set_content(json{{"message", "Timetable entry deleted"}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save timetable data: ") + error.what());
        }
    });

    // Classroom inventory endpoints (admin only)
    svr.Get("/api/classrooms", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(classroomMutex);
            res.set_content(json(Storage::load<Classroom>("data/classrooms.json")).dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read classroom data: ") + error.what());
        }
    });

    svr.Post("/api/classrooms", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception&) {
            sendApiError(res, 400, "Invalid JSON");
            return;
        }
        if (!body.is_object()) {
            sendApiError(res, 400, "Request body must be a JSON object");
            return;
        }

        try {
            std::lock_guard<std::mutex> lock(classroomMutex);
            auto classrooms = Storage::load<Classroom>("data/classrooms.json");
            Classroom classroom;
            std::string error;
            if (!applyClassroomFields(body, classroom, true, error)) {
                sendApiError(res, 400, error);
                return;
            }
            if (!validateClassroom(classroom, classrooms, "", error)) {
                sendApiError(res, 409, error);
                return;
            }
            classroom.id = Storage::generateId("room-");
            classrooms.push_back(classroom);
            if (!Storage::save<Classroom>("data/classrooms.json", classrooms)) {
                sendApiError(res, 500, "Unable to save classroom data");
                return;
            }
            res.status = 201;
            res.set_content(json{{"message", "Classroom created"}, {"id", classroom.id}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save classroom data: ") + error.what());
        }
    });

    svr.Put(R"(/api/classrooms/([a-zA-Z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception&) {
            sendApiError(res, 400, "Invalid JSON");
            return;
        }
        if (!body.is_object()) {
            sendApiError(res, 400, "Request body must be a JSON object");
            return;
        }

        try {
            std::lock_guard<std::mutex> lock(classroomMutex);
            auto classrooms = Storage::load<Classroom>("data/classrooms.json");
            const std::string classroomId = req.matches[1];
            auto classroom = std::find_if(classrooms.begin(), classrooms.end(), [&](const Classroom& item) {
                return item.id == classroomId;
            });
            if (classroom == classrooms.end()) {
                sendApiError(res, 404, "Classroom not found");
                return;
            }
            std::string error;
            if (!applyClassroomFields(body, *classroom, false, error)) {
                sendApiError(res, 400, error);
                return;
            }
            if (!validateClassroom(*classroom, classrooms, classroomId, error)) {
                sendApiError(res, 409, error);
                return;
            }
            if (!Storage::save<Classroom>("data/classrooms.json", classrooms)) {
                sendApiError(res, 500, "Unable to save classroom data");
                return;
            }
            res.set_content(json{{"message", "Classroom updated"}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save classroom data: ") + error.what());
        }
    });

    svr.Delete(R"(/api/classrooms/([a-zA-Z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(classroomMutex);
            auto classrooms = Storage::load<Classroom>("data/classrooms.json");
            const std::string classroomId = req.matches[1];
            const auto originalSize = classrooms.size();
            classrooms.erase(std::remove_if(classrooms.begin(), classrooms.end(), [&](const Classroom& classroom) {
                return classroom.id == classroomId;
            }), classrooms.end());
            if (classrooms.size() == originalSize) {
                sendApiError(res, 404, "Classroom not found");
                return;
            }
            if (!Storage::save<Classroom>("data/classrooms.json", classrooms)) {
                sendApiError(res, 500, "Unable to save classroom data");
                return;
            }
            res.set_content(json{{"message", "Classroom deleted"}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save classroom data: ") + error.what());
        }
    });

    // Public, read-only data used by the student teacher availability search.
    svr.Get("/api/student/teachers", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(timetableMutex);
            const auto teachers = Storage::load<Teacher>("data/teachers.json");
            const auto entries = loadTimetableEntries();
            json result = {{"teachers", json::array()}, {"timetables", json(entries)}};
            for (const auto& teacher : teachers) {
                if (teacher.status == "inactive") continue;
                result["teachers"].push_back({
                    {"id", teacher.id},
                    {"name", teacher.name},
                    {"department", teacher.department},
                    {"designation", teacher.designation},
                    {"subjects", teacher.subjects},
                    {"working_days", teacher.working_days},
                    {"working_start", teacher.working_start},
                    {"working_end", teacher.working_end}
                });
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read teacher availability data: ") + error.what());
        }
    });

    // List all teachers
    svr.Get("/api/teachers", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        json j = teachers;
        res.set_content(j.dump(), "application/json");
    });

    // Get a single teacher by ID
    svr.Get(R"(/api/teachers/([a-zA-Z0-9\-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        std::string id = req.matches[1];
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        for (const auto& t : teachers) {
            if (t.id == id) {
                json j = t;
                res.set_content(j.dump(), "application/json");
                return;
            }
        }
        res.status = 404;
        res.set_content(json{{"message","Teacher not found"}}.dump(), "application/json");
    });

    // Create a new teacher
    svr.Post("/api/teachers", [&](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400; res.set_content(json{{"message","Invalid JSON"}}.dump(), "application/json"); return; }
        // Required fields
        std::vector<std::string> required = {"name","employee_id","department","designation","subjects"};
        for (const auto& f : required) {
            if (!body.contains(f) || body[f].is_null()) {
                res.status = 400; res.set_content(json{{"message",std::string("Missing field ")+f}}.dump(), "application/json"); return; }
        }
        // Load existing teachers for duplicate check
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        for (const auto& t : teachers) {
            if (t.employee_id == body["employee_id"].get<std::string>()) {
                res.status = 409; res.set_content(json{{"message","Duplicate employee_id"}}.dump(), "application/json"); return;
            }
        }
        Teacher newTeacher;
        newTeacher.id = Storage::generateId("t-");
        newTeacher.name = body["name"].get<std::string>();
        newTeacher.employee_id = body["employee_id"].get<std::string>();
        newTeacher.department = body["department"].get<std::string>();
        newTeacher.designation = body["designation"].get<std::string>();
        newTeacher.subjects = body["subjects"].get<std::vector<std::string>>();
        // optional fields
        if (body.contains("email")) newTeacher.email = body["email"].get<std::string>();
        if (body.contains("assigned_classrooms")) newTeacher.assigned_classrooms = body["assigned_classrooms"].get<std::vector<std::string>>();
        if (body.contains("working_days")) newTeacher.working_days = body["working_days"].get<std::vector<std::string>>();
        if (body.contains("working_start")) newTeacher.working_start = body["working_start"].get<std::string>();
        if (body.contains("working_end")) newTeacher.working_end = body["working_end"].get<std::string>();
        newTeacher.status = body.value("status", "active");
        // Append and persist
        Storage::append<Teacher>("data/teachers.json", newTeacher);
        res.status = 201;
        res.set_content(json{{"message","Teacher created"}, {"id",newTeacher.id}}.dump(), "application/json");
    });

    // Update an existing teacher
    svr.Put(R"(/api/teachers/([a-zA-Z0-9\-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        std::string id = req.matches[1];
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400; res.set_content(json{{"message","Invalid JSON"}}.dump(), "application/json"); return; }
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        bool found = false;
        for (auto& t : teachers) {
            if (t.id == id) {
                // Duplicate employee_id check if changed
                if (body.contains("employee_id")) {
                    std::string newEmp = body["employee_id"].get<std::string>();
                    for (const auto& other : teachers) {
                        if (other.id != id && other.employee_id == newEmp) {
                            res.status = 409; res.set_content(json{{"message","Duplicate employee_id"}}.dump(), "application/json"); return;
                        }
                    }
                    t.employee_id = newEmp;
                }
                if (body.contains("name")) t.name = body["name"].get<std::string>();
                if (body.contains("department")) t.department = body["department"].get<std::string>();
                if (body.contains("designation")) t.designation = body["designation"].get<std::string>();
                if (body.contains("subjects")) t.subjects = body["subjects"].get<std::vector<std::string>>();
                if (body.contains("email")) t.email = body["email"].get<std::string>();
                if (body.contains("assigned_classrooms")) t.assigned_classrooms = body["assigned_classrooms"].get<std::vector<std::string>>();
                if (body.contains("working_days")) t.working_days = body["working_days"].get<std::vector<std::string>>();
                if (body.contains("working_start")) t.working_start = body["working_start"].get<std::string>();
                if (body.contains("working_end")) t.working_end = body["working_end"].get<std::string>();
                if (body.contains("status")) t.status = body["status"].get<std::string>();
                found = true;
                break;
            }
        }
        if (!found) { res.status = 404; res.set_content(json{{"message","Teacher not found"}}.dump(), "application/json"); return; }
        Storage::save<Teacher>("data/teachers.json", teachers);
        res.set_content(json{{"message","Teacher updated"}}.dump(), "application/json");
    });

    // Delete a teacher
    svr.Delete(R"(/api/teachers/([a-zA-Z0-9\-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        std::string id = req.matches[1];
        auto teachers = Storage::load<Teacher>("data/teachers.json");
        size_t original = teachers.size();
        teachers.erase(std::remove_if(teachers.begin(), teachers.end(), [&](const Teacher& t){ return t.id == id; }), teachers.end());
        if (teachers.size() == original) { res.status = 404; res.set_content(json{{"message","Teacher not found"}}.dump(), "application/json"); return; }
        Storage::save<Teacher>("data/teachers.json", teachers);
        res.set_content(json{{"message","Teacher deleted"}}.dump(), "application/json");
    });

    // End of API definitions
    std::cout << "Server listening on http://localhost:" << port << std::endl;
    svr.listen("0.0.0.0", port);
    return 0;
}
