#include <httplib.h>
#include <nlohmann/json.hpp>
#include "storage.h"
#include "models.h"
#include "auth.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <bcrypt/BCrypt.hpp>

using json = nlohmann::json;

namespace {
const std::string timetableFile = "data/timetables.json";
const std::string classroomBookingFile = "data/classroom_bookings.json";
const std::string maintenanceFile = "data/maintenance.json";
const std::string laboratoriesFile = "data/laboratories.json";
const std::string computersFile = "data/computers.json";
const std::string bookingsFile = "data/bookings.json";
std::mutex timetableMutex;
std::mutex classroomMutex;
std::mutex classroomBookingMutex;
std::mutex laboratoryMutex;
std::mutex lpcBookingMutex;
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
    if (!input) {
        if (std::filesystem::exists(timetableFile)) {
            throw std::runtime_error("Unable to read " + timetableFile);
        }
        return {};
    }

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

json loadJsonArrayStrict(const std::string& filename) {
    std::ifstream input(filename);
    if (!input) {
        if (std::filesystem::exists(filename)) {
            throw std::runtime_error("Unable to read " + filename);
        }
        return json::array();
    }

    json records;
    input >> records;
    if (!records.is_array()) throw std::runtime_error(filename + " must be a JSON array");
    return records;
}

bool parseDate(const std::string& value, int& year, int& month, int& day) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    }
    year = std::stoi(value.substr(0, 4));
    month = std::stoi(value.substr(5, 2));
    day = std::stoi(value.substr(8, 2));
    if (year < 1 || month < 1 || month > 12) return false;
    const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int daysInMonth[] = {31, leapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day >= 1 && day <= daysInMonth[month - 1];
}

std::string dayOfWeek(const std::string& date) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (!parseDate(date, year, month, day)) return "";

    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned dayOfYear = (153 * adjustedMonth + 2) / 5 + static_cast<unsigned>(day - 1);
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const long long daysSinceEpoch = static_cast<long long>(era) * 146097 + dayOfEra - 719468;
    const int sundayBasedDay = static_cast<int>((daysSinceEpoch % 7 + 7) % 7 + 4) % 7;
    static const std::vector<std::string> days = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    return days[static_cast<std::size_t>(sundayBasedDay)];
}

bool sameRoom(const json& record, const Classroom& classroom) {
    for (const auto& key : {"room_id", "classroom_id"}) {
        if (record.contains(key) && record[key].is_string() &&
            record[key].get<std::string>() == classroom.id) return true;
    }

    const std::string building = record.value("building", "");
    const std::string roomNumber = record.value("room_number", "");
    if (!building.empty() && !roomNumber.empty() &&
        equalsIgnoreCase(building, classroom.building) &&
        equalsIgnoreCase(roomNumber, classroom.room_number)) {
        if (!record.contains("floor") || !record["floor"].is_number_integer()) return true;
        const int recordFloor = record["floor"].get<int>();
        if (recordFloor < 0 || recordFloor == classroom.floor) {
            return true;
        }
    }

    if (record.contains("classroom") && record["classroom"].is_string()) {
        const auto room = record["classroom"].get<std::string>();
        return equalsIgnoreCase(room, classroom.id) ||
            equalsIgnoreCase(room, classroom.room_number);
    }
    return false;
}

bool overlaps(const std::string& startValue, const std::string& endValue,
              int requestedStart, int requestedEnd) {
    int start = 0;
    int end = 0;
    if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) return true;
    return requestedStart < end && start < requestedEnd;
}

bool bookingBlocksComputer(const json& booking, const std::string& computerId,
                           const std::string& date, int requestedStart, int requestedEnd) {
    if (!booking.is_object() || booking.value("computer_id", "") != computerId) return false;
    const std::string status = booking.value("status", "confirmed");
    if (status != "confirmed" && status != "approved" &&
        status != "active") return false;
    if (booking.value("date", "") != date) return false;
    return overlaps(booking.value("start", ""), booking.value("end", ""),
                    requestedStart, requestedEnd);
}

bool laboratoryAcceptsHpcRequests(const Laboratory& laboratory) {
    return equalsIgnoreCase(laboratory.category, "HPC") &&
        equalsIgnoreCase(laboratory.operational_status, "operational") &&
        !laboratory.maintenance;
}

bool laboratoryAcceptsLpcBookings(const Laboratory& laboratory) {
    return equalsIgnoreCase(laboratory.category, "LPC") &&
        equalsIgnoreCase(laboratory.operational_status, "operational") &&
        !laboratory.maintenance;
}

std::string utcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

bool roomIsAvailable(const Classroom& classroom, const std::string& date,
                    const std::string& day, int requestedStart, int requestedEnd,
                    const std::vector<TimetableEntry>& timetables,
                    const json& bookings, const json& maintenance) {
    for (const auto& entry : timetables) {
        json room = {
            {"building", entry.building},
            {"floor", entry.floor},
            {"room_number", entry.room_number},
            {"classroom", entry.classroom}
        };
        if (equalsIgnoreCase(entry.day, day) && sameRoom(room, classroom) &&
            overlaps(entry.start, entry.end, requestedStart, requestedEnd)) return false;
    }

    for (const auto& booking : bookings) {
        if (!booking.is_object() || !sameRoom(booking, classroom)) continue;
        const std::string status = booking.value("status", "approved");
        if (status != "approved" && status != "confirmed" &&
            status != "pending" && status != "active") continue;
        if (booking.contains("date") && booking["date"].is_string()) {
            const auto bookingDate = booking["date"].get<std::string>();
            int year = 0, month = 0, day = 0;
            if (!parseDate(bookingDate, year, month, day)) return false;
            if (bookingDate != date) continue;
        }
        if (overlaps(booking.value("start", ""), booking.value("end", ""),
                     requestedStart, requestedEnd)) return false;
    }

    for (const auto& record : maintenance) {
        if (!record.is_object() || !sameRoom(record, classroom)) continue;
        const std::string status = record.value("status", "active");
        if (status == "completed" || status == "cancelled" ||
            status == "resolved" || status == "inactive") continue;

        if (record.contains("date") && record["date"].is_string()) {
            const auto maintenanceDate = record["date"].get<std::string>();
            int maintenanceYear = 0, maintenanceMonth = 0, maintenanceDay = 0;
            if (!parseDate(maintenanceDate, maintenanceYear, maintenanceMonth, maintenanceDay)) return false;
            if (maintenanceDate != date) continue;
        } else if (record.contains("start_date") && record["start_date"].is_string() &&
                   record.contains("end_date") && record["end_date"].is_string()) {
            const auto startDate = record["start_date"].get<std::string>();
            const auto endDate = record["end_date"].get<std::string>();
            int startYear = 0, startMonth = 0, startDay = 0;
            int endYear = 0, endMonth = 0, endDay = 0;
            if (!parseDate(startDate, startYear, startMonth, startDay) ||
                !parseDate(endDate, endYear, endMonth, endDay) ||
                startDate > endDate) return false;
            if (date < startDate || date > endDate) continue;
        }

        if (overlaps(record.value("start", ""), record.value("end", ""),
                     requestedStart, requestedEnd)) return false;
    }
    return true;
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

bool applyLaboratoryFields(const json& body, Laboratory& laboratory, bool requireAll,
                           std::string& error) {
    const std::vector<std::string> stringFields = {
        "name", "building", "room_number", "category", "lab_administrator", "operational_status"
    };
    for (const auto& field : stringFields) {
        if (requireAll && !body.contains(field)) {
            error = "Missing field " + field;
            return false;
        }
        if (!body.contains(field)) continue;
        if (!body[field].is_string()) {
            error = field + " must be a string";
            return false;
        }
        std::string value = trim(body[field].get<std::string>());
        if (value.empty()) {
            error = field + " cannot be empty";
            return false;
        }
        if (field == "category") {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            if (value != "LPC" && value != "HPC") {
                error = "category must be LPC or HPC";
                return false;
            }
            laboratory.category = value;
        } else if (field == "operational_status") {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (value != "operational" && value != "maintenance" && value != "inactive") {
                error = "operational_status must be operational, maintenance, or inactive";
                return false;
            }
            laboratory.operational_status = value;
            laboratory.maintenance = value == "maintenance";
        } else if (field == "name") {
            if (value.size() > 100) {
                error = "name cannot exceed 100 characters";
                return false;
            }
            laboratory.name = value;
        } else if (field == "building") {
            if (value.size() > 80) {
                error = "building cannot exceed 80 characters";
                return false;
            }
            laboratory.building = value;
        } else if (field == "room_number") {
            if (value.size() > 40) {
                error = "room_number cannot exceed 40 characters";
                return false;
            }
            laboratory.room_number = value;
        } else {
            if (value.size() > 100) {
                error = "lab_administrator cannot exceed 100 characters";
                return false;
            }
            laboratory.lab_administrator = value;
        }
    }

    if (requireAll && !body.contains("floor")) {
        error = "Missing field floor";
        return false;
    }
    if (body.contains("floor")) {
        if (!readNonNegativeInteger(body["floor"], 0, laboratory.floor)) {
            error = "floor must be a non-negative integer";
            return false;
        }
    }
    return true;
}

bool validateLaboratory(const Laboratory& laboratory, const std::vector<Laboratory>& existing,
                        const std::string& ignoredId, std::string& error) {
    const auto duplicate = std::find_if(existing.begin(), existing.end(), [&](const Laboratory& other) {
        return other.id != ignoredId && other.floor == laboratory.floor &&
            equalsIgnoreCase(other.building, laboratory.building) &&
            equalsIgnoreCase(other.room_number, laboratory.room_number);
    });
    if (duplicate != existing.end()) {
        error = "A laboratory already exists at that building, floor, and room";
        return false;
    }
    return true;
}

bool applyComputerFields(const json& body, Computer& computer, bool requireAll, std::string& error) {
    if (requireAll && !body.contains("lab_id")) {
        error = "Missing field lab_id";
        return false;
    }
    if (body.contains("lab_id")) {
        if (!body["lab_id"].is_string() || trim(body["lab_id"].get<std::string>()).empty()) {
            error = "lab_id must be a non-empty string";
            return false;
        }
        computer.lab_id = trim(body["lab_id"].get<std::string>());
    }

    if (requireAll && !body.contains("computer_number")) {
        error = "Missing field computer_number";
        return false;
    }
    if (body.contains("computer_number") &&
        !readNonNegativeInteger(body["computer_number"], 1, computer.computer_number)) {
        error = "computer_number must be a positive integer";
        return false;
    }

    if (requireAll && !body.contains("specifications")) {
        error = "Missing field specifications";
        return false;
    }
    if (body.contains("specifications")) {
        if (!body["specifications"].is_string()) {
            error = "specifications must be a string";
            return false;
        }
        computer.specifications = trim(body["specifications"].get<std::string>());
        if (computer.specifications.empty() || computer.specifications.size() > 500) {
            error = "specifications must contain 1 to 500 characters";
            return false;
        }
    }

    const char* statusField = body.contains("operational_status")
        ? "operational_status"
        : body.contains("status") ? "status" : nullptr;
    if (requireAll && !statusField) {
        error = "Missing field operational_status";
        return false;
    }
    if (statusField) {
        if (!body[statusField].is_string()) {
            error = "operational_status must be a string";
            return false;
        }
        std::string status = trim(body[statusField].get<std::string>());
        std::transform(status.begin(), status.end(), status.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (status != "available" && status != "occupied" && status != "maintenance") {
            error = "operational_status must be available, occupied, or maintenance";
            return false;
        }
        computer.status = status;
    }
    return true;
}

bool validateComputer(const Computer& computer, const std::vector<Computer>& existing,
                      const std::string& ignoredId, std::string& error) {
    const auto duplicate = std::find_if(existing.begin(), existing.end(), [&](const Computer& other) {
        return other.id != ignoredId && other.lab_id == computer.lab_id &&
            other.computer_number == computer.computer_number;
    });
    if (duplicate != existing.end()) {
        error = "That computer number is already assigned in this laboratory";
        return false;
    }
    return true;
}

void updateLaboratoryComputerCounts(std::vector<Laboratory>& laboratories,
                                   const std::vector<Computer>& computers) {
    for (auto& laboratory : laboratories) {
        laboratory.total_computers = static_cast<int>(std::count_if(
            computers.begin(), computers.end(), [&](const Computer& computer) {
                return computer.lab_id == laboratory.id;
            }));
        laboratory.maintenance = laboratory.operational_status == "maintenance";
    }
}

bool saveLaboratoryInventory(const std::vector<Laboratory>& laboratories,
                             const std::vector<Computer>& computers,
                             const std::vector<Laboratory>& previousLaboratories,
                             const std::vector<Computer>& previousComputers,
                             std::string& error) {
    if (!Storage::save<Computer>(computersFile, computers)) {
        const bool restored = Storage::save<Computer>(computersFile, previousComputers);
        error = restored
            ? "Unable to save computer data; the previous inventory was restored"
            : "Unable to save computer data and failed to restore the previous computer inventory";
        return false;
    }
    if (Storage::save<Laboratory>(laboratoriesFile, laboratories)) return true;

    const bool computersRestored = Storage::save<Computer>(computersFile, previousComputers);
    const bool laboratoriesRestored = Storage::save<Laboratory>(laboratoriesFile, previousLaboratories);
    error = computersRestored && laboratoriesRestored
        ? "Unable to save laboratory data; computer changes were rolled back"
        : "Unable to save laboratory data and failed to restore the previous inventory";
    return false;
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
        std::vector<Booking> bookings;
        {
            std::lock_guard<std::mutex> bookingLock(lpcBookingMutex);
            bookings = Storage::load<Booking>(bookingsFile);
        }
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
            if (b.type == "HPC" && b.status == "pending") pendingHPC.push_back(b);
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

    // Laboratory and computer inventory endpoints (admin only).
    svr.Get("/api/laboratories", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(laboratoryMutex);
            auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            const auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
            bool countsChanged = false;
            for (auto& laboratory : laboratories) {
                const int count = static_cast<int>(std::count_if(
                    computers.begin(), computers.end(), [&](const Computer& computer) {
                        return computer.lab_id == laboratory.id;
                    }));
                if (laboratory.total_computers != count) {
                    laboratory.total_computers = count;
                    countsChanged = true;
                }
                laboratory.maintenance = laboratory.operational_status == "maintenance";
            }
            if (countsChanged && !Storage::save<Laboratory>(laboratoriesFile, laboratories)) {
                sendApiError(res, 500, "Unable to update laboratory computer counts");
                return;
            }
            res.set_content(json(laboratories).dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read laboratory data: ") + error.what());
        }
    });

    svr.Post("/api/laboratories", [](const httplib::Request& req, httplib::Response& res) {
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
            std::lock_guard<std::mutex> lock(laboratoryMutex);
            auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            Laboratory laboratory;
            std::string error;
            if (!applyLaboratoryFields(body, laboratory, true, error)) {
                sendApiError(res, 400, error);
                return;
            }
            if (!validateLaboratory(laboratory, laboratories, "", error)) {
                sendApiError(res, 409, error);
                return;
            }
            do {
                laboratory.id = Storage::generateId("lab-");
            } while (std::any_of(laboratories.begin(), laboratories.end(), [&](const Laboratory& other) {
                return other.id == laboratory.id;
            }));
            laboratory.total_computers = 0;
            laboratories.push_back(laboratory);
            if (!Storage::save<Laboratory>(laboratoriesFile, laboratories)) {
                sendApiError(res, 500, "Unable to save laboratory data");
                return;
            }
            res.status = 201;
            res.set_content(json{{"message", "Laboratory created"}, {"id", laboratory.id}}.dump(),
                            "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to save laboratory data: ") + error.what());
        }
    });

    svr.Put(R"(/api/laboratories/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
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
                std::lock_guard<std::mutex> lock(laboratoryMutex);
                auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                const std::string laboratoryId = req.matches[1];
                auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                    [&](const Laboratory& item) { return item.id == laboratoryId; });
                if (laboratory == laboratories.end()) {
                    sendApiError(res, 404, "Laboratory not found");
                    return;
                }
                std::string error;
                if (!applyLaboratoryFields(body, *laboratory, false, error)) {
                    sendApiError(res, 400, error);
                    return;
                }
                if (!validateLaboratory(*laboratory, laboratories, laboratoryId, error)) {
                    sendApiError(res, 409, error);
                    return;
                }
                if (!Storage::save<Laboratory>(laboratoriesFile, laboratories)) {
                    sendApiError(res, 500, "Unable to save laboratory data");
                    return;
                }
                res.set_content(json{{"message", "Laboratory updated"}}.dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to update laboratory data: ") + error.what());
            }
        });

    svr.Delete(R"(/api/laboratories/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!verifyAuth(req, res, "admin")) return;
            try {
                std::lock_guard<std::mutex> lock(laboratoryMutex);
                auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
                const auto previousLaboratories = laboratories;
                const auto previousComputers = computers;
                const std::string laboratoryId = req.matches[1];
                const auto originalSize = laboratories.size();
                laboratories.erase(std::remove_if(laboratories.begin(), laboratories.end(),
                    [&](const Laboratory& laboratory) { return laboratory.id == laboratoryId; }),
                    laboratories.end());
                if (laboratories.size() == originalSize) {
                    sendApiError(res, 404, "Laboratory not found");
                    return;
                }
                computers.erase(std::remove_if(computers.begin(), computers.end(),
                    [&](const Computer& computer) { return computer.lab_id == laboratoryId; }),
                    computers.end());
                updateLaboratoryComputerCounts(laboratories, computers);
                std::string error;
                if (!saveLaboratoryInventory(laboratories, computers,
                                             previousLaboratories, previousComputers, error)) {
                    sendApiError(res, 500, error);
                    return;
                }
                res.set_content(json{{"message", "Laboratory and its computers deleted"}}.dump(),
                                "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to delete laboratory: ") + error.what());
            }
        });

    svr.Get("/api/computers", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> lock(laboratoryMutex);
            auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
            if (req.has_param("lab_id")) {
                const auto laboratoryId = req.get_param_value("lab_id");
                computers.erase(std::remove_if(computers.begin(), computers.end(),
                    [&](const Computer& computer) { return computer.lab_id != laboratoryId; }),
                    computers.end());
            }
            res.set_content(json(computers).dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read computer data: ") + error.what());
        }
    });

    svr.Post("/api/computers", [](const httplib::Request& req, httplib::Response& res) {
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
            std::lock_guard<std::mutex> lock(laboratoryMutex);
            auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
            const auto previousComputers = computers;
            Computer computer;
            std::string error;
            if (!applyComputerFields(body, computer, true, error)) {
                sendApiError(res, 400, error);
                return;
            }
            const bool laboratoryExists = std::any_of(laboratories.begin(), laboratories.end(),
                [&](const Laboratory& laboratory) { return laboratory.id == computer.lab_id; });
            if (!laboratoryExists) {
                sendApiError(res, 400, "lab_id does not identify an existing laboratory");
                return;
            }
            if (!validateComputer(computer, computers, "", error)) {
                sendApiError(res, 409, error);
                return;
            }
            do {
                computer.id = Storage::generateId("pc-");
            } while (std::any_of(computers.begin(), computers.end(), [&](const Computer& other) {
                return other.id == computer.id;
            }));
            computers.push_back(computer);
            const auto previousLaboratories = laboratories;
            updateLaboratoryComputerCounts(laboratories, computers);
            if (!saveLaboratoryInventory(laboratories, computers,
                                         previousLaboratories, previousComputers, error)) {
                sendApiError(res, 500, error);
                return;
            }
            res.status = 201;
            res.set_content(json{{"message", "Computer added"}, {"id", computer.id}}.dump(),
                            "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to add computer: ") + error.what());
        }
    });

    svr.Put(R"(/api/computers/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
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
                std::lock_guard<std::mutex> lock(laboratoryMutex);
                const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
                const auto previousComputers = computers;
                const std::string computerId = req.matches[1];
                auto computer = std::find_if(computers.begin(), computers.end(),
                    [&](const Computer& item) { return item.id == computerId; });
                if (computer == computers.end()) {
                    sendApiError(res, 404, "Computer not found");
                    return;
                }
                std::string error;
                if (!applyComputerFields(body, *computer, false, error)) {
                    sendApiError(res, 400, error);
                    return;
                }
                const bool laboratoryExists = std::any_of(laboratories.begin(), laboratories.end(),
                    [&](const Laboratory& laboratory) { return laboratory.id == computer->lab_id; });
                if (!laboratoryExists) {
                    sendApiError(res, 400, "lab_id does not identify an existing laboratory");
                    return;
                }
                if (!validateComputer(*computer, computers, computerId, error)) {
                    sendApiError(res, 409, error);
                    return;
                }
                auto updatedLaboratories = laboratories;
                updateLaboratoryComputerCounts(updatedLaboratories, computers);
                const auto previousLaboratories = laboratories;
                if (!saveLaboratoryInventory(updatedLaboratories, computers,
                                             previousLaboratories, previousComputers, error)) {
                    sendApiError(res, 500, error);
                    return;
                }
                res.set_content(json{{"message", "Computer updated"}}.dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to update computer: ") + error.what());
            }
        });

    svr.Delete(R"(/api/computers/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!verifyAuth(req, res, "admin")) return;
            try {
                std::lock_guard<std::mutex> lock(laboratoryMutex);
                auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
                const auto previousComputers = computers;
                const auto previousLaboratories = laboratories;
                const std::string computerId = req.matches[1];
                const auto originalSize = computers.size();
                computers.erase(std::remove_if(computers.begin(), computers.end(),
                    [&](const Computer& computer) { return computer.id == computerId; }),
                    computers.end());
                if (computers.size() == originalSize) {
                    sendApiError(res, 404, "Computer not found");
                    return;
                }
                updateLaboratoryComputerCounts(laboratories, computers);
                std::string error;
                if (!saveLaboratoryInventory(laboratories, computers,
                                             previousLaboratories, previousComputers, error)) {
                    sendApiError(res, 500, error);
                    return;
                }
                res.set_content(json{{"message", "Computer deleted"}}.dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to delete computer: ") + error.what());
            }
        });

    // Student HPC requests remain unassigned until an administrator approves them.
    svr.Get("/api/student/hpc/laboratories", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> inventoryLock(laboratoryMutex);
            const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            json result = json::array();
            for (const auto& laboratory : laboratories) {
                if (!laboratoryAcceptsHpcRequests(laboratory)) continue;
                result.push_back({
                    {"id", laboratory.id},
                    {"name", laboratory.name},
                    {"building", laboratory.building},
                    {"room_number", laboratory.room_number}
                });
            }
            res.set_content(json{{"laboratories", result}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read HPC laboratories: ") + error.what());
        }
    });

    svr.Post("/api/student/hpc-requests", [](const httplib::Request& req, httplib::Response& res) {
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
        for (const auto& field : {"student_name", "lab_id", "date", "start", "end", "purpose"}) {
            if (!body.contains(field) || !body[field].is_string() ||
                trim(body[field].get<std::string>()).empty()) {
                sendApiError(res, 400, std::string(field) + " is required");
                return;
            }
        }

        const std::string studentName = trim(body["student_name"].get<std::string>());
        const std::string laboratoryId = trim(body["lab_id"].get<std::string>());
        const std::string date = trim(body["date"].get<std::string>());
        const std::string startValue = trim(body["start"].get<std::string>());
        const std::string endValue = trim(body["end"].get<std::string>());
        const std::string purpose = trim(body["purpose"].get<std::string>());
        if (studentName.size() > 120 || purpose.size() > 500) {
            sendApiError(res, 400, "Student name or purpose exceeds the maximum length");
            return;
        }
        int year = 0, month = 0, day = 0;
        int start = 0, end = 0;
        if (!parseDate(date, year, month, day)) {
            sendApiError(res, 400, "date must be a valid YYYY-MM-DD date");
            return;
        }
        if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) {
            sendApiError(res, 400, "start and end must be valid 24-hour times, with end after start");
            return;
        }

        try {
            std::unique_lock<std::mutex> inventoryLock(laboratoryMutex, std::defer_lock);
            std::unique_lock<std::mutex> bookingLock(lpcBookingMutex, std::defer_lock);
            std::lock(inventoryLock, bookingLock);
            const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            const auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                [&](const Laboratory& item) { return item.id == laboratoryId; });
            if (laboratory == laboratories.end() || !laboratoryAcceptsHpcRequests(*laboratory)) {
                sendApiError(res, 409, "Select an operational HPC laboratory");
                return;
            }

            auto bookings = loadJsonArrayStrict(bookingsFile);
            std::string requestId;
            do {
                requestId = Storage::generateId("hpc-request-");
            } while (std::any_of(bookings.begin(), bookings.end(), [&](const json& existing) {
                return existing.is_object() && existing.value("id", "") == requestId;
            }));
            const std::string createdAt = utcTimestamp();
            json request = {
                {"id", requestId},
                {"type", "HPC"},
                {"lab_id", laboratoryId},
                {"computer_id", ""},
                {"user_id", studentName},
                {"student_name", studentName},
                {"date", date},
                {"start", startValue},
                {"end", endValue},
                {"status", "pending"},
                {"purpose", purpose},
                {"created_at", createdAt}
            };
            bookings.push_back(request);
            if (!Storage::save<json>(bookingsFile, bookings.get<std::vector<json>>())) {
                sendApiError(res, 500, "Unable to save HPC request");
                return;
            }
            res.status = 201;
            res.set_content(json{
                {"message", "HPC request submitted and pending administrator approval"},
                {"request", request},
                {"laboratory", laboratory->name}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to submit HPC request: ") + error.what());
        }
    });

    svr.Get("/api/student/hpc-requests", [](const httplib::Request& req, httplib::Response& res) {
        const std::string studentName = trim(req.get_param_value("student_name"));
        if (studentName.empty()) {
            sendApiError(res, 400, "student_name is required");
            return;
        }
        try {
            std::lock_guard<std::mutex> bookingLock(lpcBookingMutex);
            const auto bookings = loadJsonArrayStrict(bookingsFile);
            json requests = json::array();
            for (const auto& booking : bookings) {
                if (!booking.is_object() || booking.value("type", "") != "HPC") continue;
                const std::string owner = booking.value(
                    "student_name", booking.value("user_id", ""));
                if (equalsIgnoreCase(trim(owner), studentName)) requests.push_back(booking);
            }
            res.set_content(json{{"requests", requests}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read HPC request status: ") + error.what());
        }
    });

    svr.Put(R"(/api/student/hpc-requests/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::exception&) {
                sendApiError(res, 400, "Invalid JSON");
                return;
            }
            if (!body.is_object() || !body.contains("action") || !body["action"].is_string() ||
                body["action"].get<std::string>() != "cancel" ||
                !body.contains("student_name") || !body["student_name"].is_string() ||
                trim(body["student_name"].get<std::string>()).empty()) {
                sendApiError(res, 400, "A student name and cancel action are required");
                return;
            }
            const std::string studentName = trim(body["student_name"].get<std::string>());
            try {
                std::lock_guard<std::mutex> bookingLock(lpcBookingMutex);
                auto bookings = loadJsonArrayStrict(bookingsFile);
                const std::string requestId = req.matches[1];
                auto request = std::find_if(bookings.begin(), bookings.end(), [&](const json& item) {
                    return item.is_object() && item.value("id", "") == requestId &&
                        item.value("type", "") == "HPC";
                });
                if (request == bookings.end()) {
                    sendApiError(res, 404, "HPC request not found");
                    return;
                }
                const std::string owner = request->value(
                    "student_name", request->value("user_id", ""));
                if (!equalsIgnoreCase(trim(owner), studentName)) {
                    sendApiError(res, 403, "This HPC request belongs to another student");
                    return;
                }
                const std::string status = request->value("status", "");
                if (status != "pending" && status != "approved") {
                    sendApiError(res, 409, "Only pending or approved HPC requests can be cancelled");
                    return;
                }
                (*request)["status"] = "cancelled";
                (*request)["cancelled_at"] = utcTimestamp();
                if (!Storage::save<json>(bookingsFile, bookings.get<std::vector<json>>())) {
                    sendApiError(res, 500, "Unable to cancel HPC request");
                    return;
                }
                res.set_content(json{{"message", "HPC request cancelled"}, {"request", *request}}.dump(),
                                "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to cancel HPC request: ") + error.what());
            }
        });

    // Admin review APIs. Approval and availability checks serialize with all
    // laboratory/computer changes and other booking decisions.
    svr.Get("/api/admin/hpc-requests", [](const httplib::Request& req, httplib::Response& res) {
        if (!verifyAuth(req, res, "admin")) return;
        try {
            std::lock_guard<std::mutex> bookingLock(lpcBookingMutex);
            const auto bookings = loadJsonArrayStrict(bookingsFile);
            json requests = json::array();
            for (const auto& booking : bookings) {
                if (booking.is_object() && booking.value("type", "") == "HPC") {
                    requests.push_back(booking);
                }
            }
            std::stable_sort(requests.begin(), requests.end(), [](const json& left, const json& right) {
                const bool leftPending = left.value("status", "") == "pending";
                const bool rightPending = right.value("status", "") == "pending";
                if (leftPending != rightPending) return leftPending;
                return left.value("created_at", "") > right.value("created_at", "");
            });
            res.set_content(json{{"requests", requests}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read HPC requests: ") + error.what());
        }
    });

    svr.Get(R"(/api/admin/hpc-requests/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!verifyAuth(req, res, "admin")) return;
            try {
                std::lock_guard<std::mutex> bookingLock(lpcBookingMutex);
                const auto bookings = loadJsonArrayStrict(bookingsFile);
                const std::string requestId = req.matches[1];
                const auto request = std::find_if(bookings.begin(), bookings.end(),
                    [&](const json& item) {
                        return item.is_object() && item.value("id", "") == requestId &&
                            item.value("type", "") == "HPC";
                    });
                if (request == bookings.end()) {
                    sendApiError(res, 404, "HPC request not found");
                    return;
                }
                res.set_content(request->dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to read HPC request: ") + error.what());
            }
        });

    svr.Get(R"(/api/admin/hpc-requests/([a-zA-Z0-9\-]+)/availability)",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!verifyAuth(req, res, "admin")) return;
            try {
                std::unique_lock<std::mutex> inventoryLock(laboratoryMutex, std::defer_lock);
                std::unique_lock<std::mutex> bookingLock(lpcBookingMutex, std::defer_lock);
                std::lock(inventoryLock, bookingLock);
                const auto bookings = loadJsonArrayStrict(bookingsFile);
                const std::string requestId = req.matches[1];
                const auto request = std::find_if(bookings.begin(), bookings.end(),
                    [&](const json& item) {
                        return item.is_object() && item.value("id", "") == requestId &&
                            item.value("type", "") == "HPC";
                    });
                if (request == bookings.end()) {
                    sendApiError(res, 404, "HPC request not found");
                    return;
                }
                if (request->value("status", "") != "pending") {
                    sendApiError(res, 409, "Only pending HPC requests can be approved");
                    return;
                }
                const std::string labId = request->value("lab_id", "");
                const std::string date = request->value("date", "");
                int year = 0, month = 0, day = 0;
                int start = 0, end = 0;
                if (!parseDate(date, year, month, day) ||
                    !parseTime(request->value("start", ""), start) ||
                    !parseTime(request->value("end", ""), end) || start >= end) {
                    sendApiError(res, 400, "HPC request contains an invalid date or time period");
                    return;
                }
                const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                const auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                    [&](const Laboratory& item) { return item.id == labId; });
                if (laboratory == laboratories.end() || !laboratoryAcceptsHpcRequests(*laboratory)) {
                    sendApiError(res, 409, "The requested HPC laboratory is no longer operational");
                    return;
                }
                const auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
                json available = json::array();
                for (const auto& computer : computers) {
                    if (computer.lab_id != labId || !equalsIgnoreCase(computer.status, "available")) continue;
                    const bool conflict = std::any_of(bookings.begin(), bookings.end(),
                        [&](const json& booking) {
                            return bookingBlocksComputer(booking, computer.id, date, start, end);
                        });
                    if (!conflict) {
                        available.push_back({
                            {"id", computer.id},
                            {"computer_number", computer.computer_number},
                            {"specifications", computer.specifications}
                        });
                    }
                }
                res.set_content(json{
                    {"laboratory", *laboratory},
                    {"computers", available}
                }.dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to check HPC availability: ") + error.what());
            }
        });

    svr.Put(R"(/api/admin/hpc-requests/([a-zA-Z0-9\-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!verifyAuth(req, res, "admin")) return;
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::exception&) {
                sendApiError(res, 400, "Invalid JSON");
                return;
            }
            if (!body.is_object() || !body.contains("action") || !body["action"].is_string()) {
                sendApiError(res, 400, "action must be approve, reject, or complete");
                return;
            }
            const std::string action = body["action"].get<std::string>();
            if (action != "approve" && action != "reject" && action != "complete") {
                sendApiError(res, 400, "action must be approve, reject, or complete");
                return;
            }
            std::string computerId;
            if (action == "approve") {
                if (!body.contains("computer_id") || !body["computer_id"].is_string() ||
                    trim(body["computer_id"].get<std::string>()).empty()) {
                    sendApiError(res, 400, "computer_id is required to approve an HPC request");
                    return;
                }
                computerId = trim(body["computer_id"].get<std::string>());
            }
            std::string rejectionReason;
            if (body.contains("rejection_reason")) {
                if (!body["rejection_reason"].is_string()) {
                    sendApiError(res, 400, "rejection_reason must be a string");
                    return;
                }
                rejectionReason = trim(body["rejection_reason"].get<std::string>());
                if (rejectionReason.size() > 500) {
                    sendApiError(res, 400, "rejection_reason cannot exceed 500 characters");
                    return;
                }
            }

            const std::string authHeader = req.get_header_value("Authorization");
            const auto session = AuthManager::validateToken(authHeader.substr(7));
            if (!session) {
                sendApiError(res, 401, "Invalid or expired token");
                return;
            }
            const std::string administratorId = session->user_id;

            try {
                std::unique_lock<std::mutex> inventoryLock(laboratoryMutex, std::defer_lock);
                std::unique_lock<std::mutex> bookingLock(lpcBookingMutex, std::defer_lock);
                std::lock(inventoryLock, bookingLock);
                auto bookings = loadJsonArrayStrict(bookingsFile);
                const std::string requestId = req.matches[1];
                auto request = std::find_if(bookings.begin(), bookings.end(),
                    [&](const json& item) {
                        return item.is_object() && item.value("id", "") == requestId &&
                            item.value("type", "") == "HPC";
                    });
                if (request == bookings.end()) {
                    sendApiError(res, 404, "HPC request not found");
                    return;
                }
                const std::string status = request->value("status", "");
                const std::string updatedAt = utcTimestamp();
                if (action == "approve") {
                    if (status != "pending") {
                        sendApiError(res, 409, "Only pending HPC requests can be approved");
                        return;
                    }
                    const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
                    const std::string labId = request->value("lab_id", "");
                    const auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                        [&](const Laboratory& item) { return item.id == labId; });
                    if (laboratory == laboratories.end() || !laboratoryAcceptsHpcRequests(*laboratory)) {
                        sendApiError(res, 409, "The requested HPC laboratory is no longer operational");
                        return;
                    }
                    int year = 0, month = 0, day = 0;
                    int start = 0, end = 0;
                    const std::string date = request->value("date", "");
                    if (!parseDate(date, year, month, day) ||
                        !parseTime(request->value("start", ""), start) ||
                        !parseTime(request->value("end", ""), end) || start >= end) {
                        sendApiError(res, 400, "HPC request contains an invalid date or time period");
                        return;
                    }
                    const auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
                    const auto computer = std::find_if(computers.begin(), computers.end(),
                        [&](const Computer& item) {
                            return item.id == computerId && item.lab_id == labId;
                        });
                    if (computer == computers.end()) {
                        sendApiError(res, 404, "Selected computer does not belong to the requested laboratory");
                        return;
                    }
                    if (!equalsIgnoreCase(computer->status, "available")) {
                        sendApiError(res, 409, "Selected computer is under maintenance or otherwise unavailable");
                        return;
                    }
                    const bool conflict = std::any_of(bookings.begin(), bookings.end(),
                        [&](const json& booking) {
                            return bookingBlocksComputer(booking, computerId, date, start, end);
                        });
                    if (conflict) {
                        sendApiError(res, 409, "Selected computer is already booked for an overlapping period");
                        return;
                    }
                    (*request)["status"] = "approved";
                    (*request)["computer_id"] = computerId;
                    (*request)["approved_by"] = administratorId;
                    (*request)["approval_timestamp"] = updatedAt;
                    (*request)["reviewed_by"] = administratorId;
                    (*request)["reviewed_at"] = updatedAt;
                    (*request).erase("rejection_reason");
                } else if (action == "reject") {
                    if (status != "pending") {
                        sendApiError(res, 409, "Only pending HPC requests can be rejected");
                        return;
                    }
                    (*request)["status"] = "rejected";
                    (*request)["reviewed_by"] = administratorId;
                    (*request)["reviewed_at"] = updatedAt;
                    if (rejectionReason.empty()) (*request).erase("rejection_reason");
                    else (*request)["rejection_reason"] = rejectionReason;
                } else {
                    if (status != "approved") {
                        sendApiError(res, 409, "Only approved HPC requests can be marked completed");
                        return;
                    }
                    (*request)["status"] = "completed";
                    (*request)["completed_at"] = updatedAt;
                    (*request)["reviewed_by"] = administratorId;
                    (*request)["reviewed_at"] = updatedAt;
                }

                if (!Storage::save<json>(bookingsFile, bookings.get<std::vector<json>>())) {
                    sendApiError(res, 500, "Unable to save HPC request status");
                    return;
                }
                res.set_content(json{
                    {"message", action == "approve" ? "HPC request approved" :
                        action == "reject" ? "HPC request rejected" : "HPC request marked completed"},
                    {"request", *request}
                }.dump(), "application/json");
            } catch (const std::exception& error) {
                sendApiError(res, 500, std::string("Unable to update HPC request: ") + error.what());
            }
        });

    // Student LPC inventory and bookings use the same inventory and booking locks
    // for availability checks and final reservations.
    svr.Get("/api/student/lpc/laboratories", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(laboratoryMutex);
            const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            json result = json::array();
            for (const auto& laboratory : laboratories) {
                if (!laboratoryAcceptsLpcBookings(laboratory)) continue;
                result.push_back({
                    {"id", laboratory.id},
                    {"name", laboratory.name},
                    {"building", laboratory.building},
                    {"floor", laboratory.floor},
                    {"room_number", laboratory.room_number}
                });
            }
            res.set_content(json{{"laboratories", result}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to read LPC laboratories: ") + error.what());
        }
    });

    svr.Get("/api/student/lpc/computers", [](const httplib::Request& req, httplib::Response& res) {
        const std::string laboratoryId = trim(req.get_param_value("lab_id"));
        const std::string date = req.get_param_value("date");
        const std::string startValue = req.get_param_value("start");
        const std::string endValue = req.get_param_value("end");
        if (laboratoryId.empty()) {
            sendApiError(res, 400, "lab_id is required");
            return;
        }
        int year = 0, month = 0, dayOfMonth = 0;
        int start = 0, end = 0;
        if (!parseDate(date, year, month, dayOfMonth)) {
            sendApiError(res, 400, "date must be a valid YYYY-MM-DD date");
            return;
        }
        if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) {
            sendApiError(res, 400, "start and end must be valid 24-hour times, with end after start");
            return;
        }

        try {
            std::unique_lock<std::mutex> inventoryLock(laboratoryMutex, std::defer_lock);
            std::unique_lock<std::mutex> bookingLock(lpcBookingMutex, std::defer_lock);
            std::lock(inventoryLock, bookingLock);
            const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            const auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                [&](const Laboratory& item) { return item.id == laboratoryId; });
            if (laboratory == laboratories.end() || !laboratoryAcceptsLpcBookings(*laboratory)) {
                sendApiError(res, 404, "Operational LPC laboratory not found");
                return;
            }

            const auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
            const auto bookings = loadJsonArrayStrict(bookingsFile);
            json available = json::array();
            for (const auto& computer : computers) {
                if (computer.lab_id != laboratoryId || !equalsIgnoreCase(computer.status, "available")) continue;
                const bool hasConflict = std::any_of(bookings.begin(), bookings.end(),
                    [&](const json& booking) {
                        return bookingBlocksComputer(booking, computer.id, date, start, end);
                    });
                if (hasConflict) continue;
                available.push_back({
                    {"id", computer.id},
                    {"computer_number", computer.computer_number},
                    {"specifications", computer.specifications}
                });
            }
            res.set_content(json{{"computers", available}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to check LPC computer availability: ") + error.what());
        }
    });

    svr.Post("/api/student/lpc/bookings", [](const httplib::Request& req, httplib::Response& res) {
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
        for (const auto& field : {"student_name", "lab_id", "computer_id", "date", "start", "end", "purpose"}) {
            if (!body.contains(field) || !body[field].is_string() ||
                trim(body[field].get<std::string>()).empty()) {
                sendApiError(res, 400, std::string(field) + " is required");
                return;
            }
        }

        const std::string studentName = trim(body["student_name"].get<std::string>());
        const std::string laboratoryId = trim(body["lab_id"].get<std::string>());
        const std::string computerId = trim(body["computer_id"].get<std::string>());
        const std::string date = trim(body["date"].get<std::string>());
        const std::string startValue = trim(body["start"].get<std::string>());
        const std::string endValue = trim(body["end"].get<std::string>());
        const std::string purpose = trim(body["purpose"].get<std::string>());
        if (studentName.size() > 120 || purpose.size() > 500) {
            sendApiError(res, 400, "Student name or purpose exceeds the maximum length");
            return;
        }

        int year = 0, month = 0, dayOfMonth = 0;
        int start = 0, end = 0;
        if (!parseDate(date, year, month, dayOfMonth)) {
            sendApiError(res, 400, "date must be a valid YYYY-MM-DD date");
            return;
        }
        if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) {
            sendApiError(res, 400, "start and end must be valid 24-hour times, with end after start");
            return;
        }

        try {
            std::unique_lock<std::mutex> inventoryLock(laboratoryMutex, std::defer_lock);
            std::unique_lock<std::mutex> bookingLock(lpcBookingMutex, std::defer_lock);
            std::lock(inventoryLock, bookingLock);
            const auto laboratories = loadJsonArrayStrict(laboratoriesFile).get<std::vector<Laboratory>>();
            const auto laboratory = std::find_if(laboratories.begin(), laboratories.end(),
                [&](const Laboratory& item) { return item.id == laboratoryId; });
            if (laboratory == laboratories.end() || !laboratoryAcceptsLpcBookings(*laboratory)) {
                sendApiError(res, 409, "This LPC laboratory is no longer operational");
                return;
            }

            const auto computers = loadJsonArrayStrict(computersFile).get<std::vector<Computer>>();
            const auto computer = std::find_if(computers.begin(), computers.end(),
                [&](const Computer& item) {
                    return item.id == computerId && item.lab_id == laboratoryId;
                });
            if (computer == computers.end()) {
                sendApiError(res, 404, "Computer does not belong to this laboratory");
                return;
            }
            if (!equalsIgnoreCase(computer->status, "available")) {
                sendApiError(res, 409, "This computer is under maintenance or otherwise unavailable");
                return;
            }

            auto bookings = loadJsonArrayStrict(bookingsFile);
            const bool conflict = std::any_of(bookings.begin(), bookings.end(),
                [&](const json& booking) {
                    return bookingBlocksComputer(booking, computerId, date, start, end);
                });
            if (conflict) {
                sendApiError(res, 409, "This computer has already been booked for an overlapping time");
                return;
            }

            std::string bookingId;
            do {
                bookingId = Storage::generateId("lpc-booking-");
            } while (std::any_of(bookings.begin(), bookings.end(), [&](const json& existing) {
                return existing.is_object() && existing.value("id", "") == bookingId;
            }));
            json booking = {
                {"id", bookingId},
                {"type", "LPC"},
                {"lab_id", laboratoryId},
                {"computer_id", computerId},
                {"user_id", studentName},
                {"student_name", studentName},
                {"date", date},
                {"start", startValue},
                {"end", endValue},
                {"status", "confirmed"},
                {"purpose", purpose}
            };
            bookings.push_back(booking);
            if (!Storage::save<json>(bookingsFile, bookings.get<std::vector<json>>())) {
                sendApiError(res, 500, "Unable to save booking");
                return;
            }
            res.status = 201;
            res.set_content(json{
                {"message", "Booking confirmed"},
                {"booking", booking},
                {"laboratory", laboratory->name},
                {"computer_number", computer->computer_number}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to create LPC booking: ") + error.what());
        }
    });

    // Public classroom search; availability is calculated from current server-side records.
    svr.Get("/api/student/classrooms", [](const httplib::Request& req, httplib::Response& res) {
        const std::string date = req.get_param_value("date");
        const std::string startValue = req.get_param_value("start");
        const std::string endValue = req.get_param_value("end");
        const std::string buildingFilter = trim(req.get_param_value("building"));
        const std::string floorFilter = req.get_param_value("floor");
        const std::string capacityFilter = req.get_param_value("min_capacity");
        int year = 0, month = 0, dayOfMonth = 0;
        int start = 0, end = 0;
        if (!parseDate(date, year, month, dayOfMonth)) {
            sendApiError(res, 400, "date must be a valid YYYY-MM-DD date");
            return;
        }
        if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) {
            sendApiError(res, 400, "start and end must be valid 24-hour times, with end after start");
            return;
        }

        int floor = 0;
        if (!floorFilter.empty()) {
            try {
                std::size_t parsed = 0;
                floor = std::stoi(floorFilter, &parsed);
                if (parsed != floorFilter.size() || floor < 0) throw std::invalid_argument("floor");
            } catch (const std::exception&) {
                sendApiError(res, 400, "floor must be a non-negative integer");
                return;
            }
        }
        int minimumCapacity = 0;
        if (!capacityFilter.empty()) {
            try {
                std::size_t parsed = 0;
                minimumCapacity = std::stoi(capacityFilter, &parsed);
                if (parsed != capacityFilter.size() || minimumCapacity < 1) {
                    throw std::invalid_argument("capacity");
                }
            } catch (const std::exception&) {
                sendApiError(res, 400, "min_capacity must be a positive integer");
                return;
            }
        }

        try {
            std::unique_lock<std::mutex> classroomLock(classroomMutex, std::defer_lock);
            std::unique_lock<std::mutex> timetableLock(timetableMutex, std::defer_lock);
            std::unique_lock<std::mutex> bookingLock(classroomBookingMutex, std::defer_lock);
            std::lock(classroomLock, timetableLock, bookingLock);
            const auto classrooms = loadJsonArrayStrict("data/classrooms.json").get<std::vector<Classroom>>();
            const auto timetables = loadTimetableEntries();
            auto bookings = loadJsonArrayStrict("data/bookings.json");
            auto classroomBookings = loadJsonArrayStrict(classroomBookingFile);
            bookings.insert(bookings.end(), classroomBookings.begin(), classroomBookings.end());
            const auto maintenance = loadJsonArrayStrict(maintenanceFile);
            const std::string day = dayOfWeek(date);

            std::vector<Classroom> available;
            for (const auto& classroom : classrooms) {
                if (!buildingFilter.empty() && !equalsIgnoreCase(classroom.building, buildingFilter)) continue;
                if (!floorFilter.empty() && classroom.floor != floor) continue;
                if (classroom.capacity < minimumCapacity) continue;
                if (roomIsAvailable(classroom, date, day, start, end, timetables, bookings, maintenance)) {
                    available.push_back(classroom);
                }
            }

            const std::string sort = req.get_param_value("sort");
            if (sort == "capacity") {
                std::sort(available.begin(), available.end(), [](const Classroom& left, const Classroom& right) {
                    return left.capacity == right.capacity
                        ? left.building < right.building
                        : left.capacity < right.capacity;
                });
            } else if (sort == "building") {
                std::sort(available.begin(), available.end(), [](const Classroom& left, const Classroom& right) {
                    return left.building == right.building
                        ? left.room_number < right.room_number
                        : left.building < right.building;
                });
            } else if (!sort.empty() && sort != "none") {
                sendApiError(res, 400, "sort must be capacity, building, or none");
                return;
            }

            json result = json::array();
            for (const auto& classroom : available) {
                json item = classroom;
                item["availability_status"] = "Available";
                result.push_back(std::move(item));
            }
            res.set_content(json{{"classrooms", result}}.dump(), "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to check classroom availability: ") + error.what());
        }
    });

    // Recheck availability while holding the same locks used by the search before reserving.
    svr.Post("/api/student/classroom-bookings", [](const httplib::Request& req, httplib::Response& res) {
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
        for (const auto& field : {"classroom_id", "date", "start", "end"}) {
            if (!body.contains(field) || !body[field].is_string() ||
                trim(body[field].get<std::string>()).empty()) {
                sendApiError(res, 400, std::string(field) + " is required");
                return;
            }
        }
        if (body.contains("student_name") && !body["student_name"].is_string()) {
            sendApiError(res, 400, "student_name must be a string");
            return;
        }

        const std::string classroomId = trim(body["classroom_id"].get<std::string>());
        const std::string date = trim(body["date"].get<std::string>());
        const std::string startValue = trim(body["start"].get<std::string>());
        const std::string endValue = trim(body["end"].get<std::string>());
        int year = 0, month = 0, dayOfMonth = 0;
        int start = 0, end = 0;
        if (!parseDate(date, year, month, dayOfMonth)) {
            sendApiError(res, 400, "date must be a valid YYYY-MM-DD date");
            return;
        }
        if (!parseTime(startValue, start) || !parseTime(endValue, end) || start >= end) {
            sendApiError(res, 400, "start and end must be valid 24-hour times, with end after start");
            return;
        }

        try {
            std::unique_lock<std::mutex> classroomLock(classroomMutex, std::defer_lock);
            std::unique_lock<std::mutex> timetableLock(timetableMutex, std::defer_lock);
            std::unique_lock<std::mutex> bookingLock(classroomBookingMutex, std::defer_lock);
            std::lock(classroomLock, timetableLock, bookingLock);
            const auto classrooms = loadJsonArrayStrict("data/classrooms.json").get<std::vector<Classroom>>();
            const auto classroom = std::find_if(classrooms.begin(), classrooms.end(),
                [&](const Classroom& item) { return item.id == classroomId; });
            if (classroom == classrooms.end()) {
                sendApiError(res, 404, "Classroom not found");
                return;
            }

            const auto timetables = loadTimetableEntries();
            auto bookings = loadJsonArrayStrict("data/bookings.json");
            auto classroomBookingRecords =
                loadJsonArrayStrict(classroomBookingFile).get<std::vector<json>>();
            bookings.insert(bookings.end(), classroomBookingRecords.begin(), classroomBookingRecords.end());
            const auto maintenance = loadJsonArrayStrict(maintenanceFile);
            if (!roomIsAvailable(*classroom, date, dayOfWeek(date), start, end,
                                 timetables, bookings, maintenance)) {
                sendApiError(res, 409, "This classroom is no longer available for the selected time.");
                return;
            }

            json booking = {
                {"id", Storage::generateId("classroom-booking-")},
                {"classroom_id", classroom->id},
                {"building", classroom->building},
                {"floor", classroom->floor},
                {"room_number", classroom->room_number},
                {"date", date},
                {"start", startValue},
                {"end", endValue},
                {"status", "approved"},
                {"student_name", trim(body.value("student_name", ""))}
            };
            classroomBookingRecords.push_back(booking);
            if (!Storage::save<json>(classroomBookingFile, classroomBookingRecords)) {
                sendApiError(res, 500, "Unable to save classroom booking");
                return;
            }
            res.status = 201;
            res.set_content(json{{"message", "Classroom reserved"}, {"booking", booking}}.dump(),
                            "application/json");
        } catch (const std::exception& error) {
            sendApiError(res, 500, std::string("Unable to reserve classroom: ") + error.what());
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
