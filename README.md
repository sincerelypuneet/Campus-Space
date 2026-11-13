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

Students can search teacher availability at `http://localhost:8080/student-teacher.html`
after signing in through the student demo login. The read-only student endpoint uses
teacher records and saved timetable entries; optional `working_days`, `working_start`,
and `working_end` teacher fields define working hours. When working hours are not
configured, availability is limited to the span of that teacher's scheduled classes
for the selected day rather than assuming a campus-wide timetable.

Students can search and reserve free classrooms at `http://localhost:8080/student-classroom.html`.
The C++ availability endpoint checks classroom inventory, weekly timetable allocations,
approved/pending classroom bookings, and maintenance records for the selected date and
time. Classroom reservations are stored in `BACKEND/data/classroom_bookings.json` and
are rechecked atomically to prevent overlapping reservations. Store classroom maintenance
records in `BACKEND/data/maintenance.json` as a JSON array with a `classroom_id` (or
`building`, `floor`, and `room_number`), an active `status`, and either `date` or
inclusive `start_date`/`end_date` fields; optional `start` and `end` times limit the
maintenance window. Completed, cancelled, resolved, and inactive records do not block
availability. Existing room entries in `BACKEND/data/bookings.json` are also considered.
