#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Forward declarations for JSON (de)serialization
struct User {
    std::string id;
    std::string username;
    std::string password_hash;
    std::string role; // "admin" or "student"
};

struct Teacher {
    std::string id;
    std::string name;
    std::string employee_id;
    std::string department;
    std::string designation;
    std::vector<std::string> subjects;
    std::string email;
    std::vector<std::string> assigned_classrooms;
    std::vector<std::string> working_days; // e.g., ["Monday","Tuesday"]
    std::string working_start; // optional HH:MM availability boundary
    std::string working_end; // optional HH:MM availability boundary
    std::string status; // "active" or "inactive"
};

struct TimetableEntry {
    std::string id; // unique entry id
    std::string teacher_id; // parent teacher id
    std::string day; // e.g., "Monday"
    std::string start; // HH:MM
    std::string end;   // HH:MM
    std::string subject;
    std::string class_section; // class or section
    std::string department;
    std::string building;
    int floor = 0;
    std::string room_number;
    std::string classroom; // legacy field for compatibility (optional)
};

struct Classroom {
    std::string id;
    std::string building;
    std::string room_number;
    int floor = 0;
    int capacity = 0;
    std::vector<std::string> features;
};

struct Laboratory {
    std::string id;
    std::string name;
    std::string category; // "LPC" or "HPC"
    int total_computers;
    bool maintenance;
};

struct Computer {
    std::string id;
    std::string lab_id;
    std::string status; // "available", "occupied", "maintenance"
};

struct Booking {
    std::string id;
    std::string type; // "LPC" or "HPC"
    std::string lab_id;
    std::string computer_id; // optional for HPC request until approved
    std::string user_id;
    std::string date; // YYYY-MM-DD
    std::string start; // HH:MM
    std::string end;   // HH:MM
    std::string status; // "confirmed", "pending", "approved", "rejected"
    std::string purpose; // optional for HPC
};

// JSON (de)serialization – defined inline for header‑only convenience
namespace nlohmann {
    template <> struct adl_serializer<User> {
        static void to_json(json& j, const User& u) {
            j = json{{"id", u.id}, {"username", u.username}, {"password_hash", u.password_hash}, {"role", u.role}};
        }
        static void from_json(const json& j, User& u) {
            j.at("id").get_to(u.id);
            j.at("username").get_to(u.username);
            j.at("password_hash").get_to(u.password_hash);
            j.at("role").get_to(u.role);
        }
    };
    template <> struct adl_serializer<Teacher> {
        static void to_json(json& j, const Teacher& t) {
            j = json{{"id", t.id}, {"name", t.name}, {"employee_id", t.employee_id}, {"department", t.department}, {"designation", t.designation}, {"subjects", t.subjects}, {"email", t.email}, {"assigned_classrooms", t.assigned_classrooms}, {"working_days", t.working_days}, {"working_start", t.working_start}, {"working_end", t.working_end}, {"status", t.status}};
        }
        static void from_json(const json& j, Teacher& t) {
            j.at("id").get_to(t.id);
            j.at("name").get_to(t.name);
            j.at("employee_id").get_to(t.employee_id);
            j.at("department").get_to(t.department);
            j.at("designation").get_to(t.designation);
            j.at("subjects").get_to(t.subjects);
            if (j.contains("email")) j.at("email").get_to(t.email);
            if (j.contains("assigned_classrooms")) j.at("assigned_classrooms").get_to(t.assigned_classrooms);
            if (j.contains("working_days")) j.at("working_days").get_to(t.working_days);
            if (j.contains("working_start")) j.at("working_start").get_to(t.working_start);
            if (j.contains("working_end")) j.at("working_end").get_to(t.working_end);
            if (j.contains("status")) j.at("status").get_to(t.status);
        }
    };
    template <> struct adl_serializer<TimetableEntry> {
        static void to_json(json& j, const TimetableEntry& e) {
            j = json{{"id", e.id}, {"teacher_id", e.teacher_id}, {"subject", e.subject}, {"class_section", e.class_section}, {"department", e.department}, {"day", e.day}, {"start", e.start}, {"end", e.end}, {"building", e.building}, {"floor", e.floor}, {"room_number", e.room_number}};
            if (!e.classroom.empty()) j["classroom"] = e.classroom;
        }
        static void from_json(const json& j, TimetableEntry& e) {
            j.at("id").get_to(e.id);
            if (j.contains("teacher_id")) j.at("teacher_id").get_to(e.teacher_id);
            j.at("day").get_to(e.day);
            j.at("start").get_to(e.start);
            j.at("end").get_to(e.end);
            j.at("subject").get_to(e.subject);
            if(j.contains("class_section")) j.at("class_section").get_to(e.class_section);
            if(j.contains("department")) j.at("department").get_to(e.department);
            if(j.contains("building")) j.at("building").get_to(e.building);
            if(j.contains("floor")) j.at("floor").get_to(e.floor);
            if(j.contains("room_number")) j.at("room_number").get_to(e.room_number);
            if(j.contains("classroom")) j.at("classroom").get_to(e.classroom);
        }
    };
    template <> struct adl_serializer<Classroom> {
        static void to_json(json& j, const Classroom& c) {
            j = json{{"id", c.id}, {"building", c.building}, {"room_number", c.room_number}, {"floor", c.floor}, {"capacity", c.capacity}, {"features", c.features}};
        }
        static void from_json(const json& j, Classroom& c) {
            j.at("id").get_to(c.id);
            j.at("building").get_to(c.building);
            if (j.contains("room_number")) j.at("room_number").get_to(c.room_number);
            if (j.contains("floor")) j.at("floor").get_to(c.floor);
            if (j.contains("capacity")) j.at("capacity").get_to(c.capacity);
            if (j.contains("features")) j.at("features").get_to(c.features);
        }
    };
    template <> struct adl_serializer<Laboratory> {
        static void to_json(json& j, const Laboratory& l) {
            j = json{{"id", l.id}, {"name", l.name}, {"category", l.category}, {"total_computers", l.total_computers}, {"maintenance", l.maintenance}};
        }
        static void from_json(const json& j, Laboratory& l) {
            j.at("id").get_to(l.id);
            j.at("name").get_to(l.name);
            j.at("category").get_to(l.category);
            j.at("total_computers").get_to(l.total_computers);
            j.at("maintenance").get_to(l.maintenance);
        }
    };
    template <> struct adl_serializer<Computer> {
        static void to_json(json& j, const Computer& c) {
            j = json{{"id", c.id}, {"lab_id", c.lab_id}, {"status", c.status}};
        }
        static void from_json(const json& j, Computer& c) {
            j.at("id").get_to(c.id);
            j.at("lab_id").get_to(c.lab_id);
            j.at("status").get_to(c.status);
        }
    };
    template <> struct adl_serializer<Booking> {
        static void to_json(json& j, const Booking& b) {
            j = json{{"id", b.id}, {"type", b.type}, {"lab_id", b.lab_id}, {"computer_id", b.computer_id}, {"user_id", b.user_id}, {"date", b.date}, {"start", b.start}, {"end", b.end}, {"status", b.status}, {"purpose", b.purpose}};
        }
        static void from_json(const json& j, Booking& b) {
            j.at("id").get_to(b.id);
            j.at("type").get_to(b.type);
            j.at("lab_id").get_to(b.lab_id);
            j.at("computer_id").get_to(b.computer_id);
            j.at("user_id").get_to(b.user_id);
            j.at("date").get_to(b.date);
            j.at("start").get_to(b.start);
            j.at("end").get_to(b.end);
            j.at("status").get_to(b.status);
            j.at("purpose").get_to(b.purpose);
        }
    };
}
