# CampusSpace

A web‑based college resource management and booking system built with:
- **Frontend:** HTML5, CSS3, vanilla JavaScript
- **Backend:** C++ (cpp‑httplib for HTTP, nlohmann/json for JSON)
- **Storage:** JSON files on disk

The project follows the architecture and roadmap described in `PROJECT_CONTEXT.md`.

## Running the login server

Build and run `CampusSpaceBackend` with `BACKEND` as its working directory. The server
listens on port `8080`, serves the frontend, and handles `/api/login` and the admin
teacher timetable API. Open `http://localhost:8080/login.html` in the browser; opening
`login.html` directly or serving it from a separate Live Server port will not reach the
backend APIs.

Administrators can manage schedules at `http://localhost:8080/admin-timetable.html`.
Timetable records are stored in `BACKEND/data/timetables.json` and persist across server
restarts. The API validates required fields, teacher IDs, day and time values, teacher
overlaps, and room allocation conflicts before saving changes.

Administrators can add, edit, and delete classrooms at
`http://localhost:8080/admin-classrooms.html`. Classroom records are stored in
`BACKEND/data/classrooms.json` and persist across server restarts. Rooms are unique by
building, floor, and room number.
