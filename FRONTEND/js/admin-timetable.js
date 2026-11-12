const API_TIMETABLE = '/api/timetables';
const API_TEACHERS = '/api/teachers';
const WEEK_DAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
const DAY_START = 8 * 60;
const DAY_END = 18 * 60;

let teachersCache = [];
let timetableEntries = [];
let currentView = 'weekly';
let editingEntryId = null;

function toggleSidebar() {
  document.getElementById('sidebar')?.classList.toggle('collapsed');
}

async function fetchJson(url, options = {}) {
  const token = sessionStorage.getItem('authToken');
  const headers = { ...(options.headers || {}) };
  if (options.body && !headers['Content-Type']) headers['Content-Type'] = 'application/json';
  if (token) headers.Authorization = `Bearer ${token}`;

  const response = await fetch(url, { ...options, headers });
  const responseText = await response.text();
  let data = {};
  if (responseText) {
    try {
      data = JSON.parse(responseText);
    } catch {
      throw new Error('The server returned an invalid response.');
    }
  }
  if (!response.ok) throw new Error(data.message || response.statusText || 'Request failed');
  return data;
}

function setMessage(message, isError = false) {
  const element = document.getElementById('timetable-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function timeToMinutes(value) {
  const [hours, minutes] = value.split(':').map(Number);
  return hours * 60 + minutes;
}

function formatTime(minutes) {
  return `${String(Math.floor(minutes / 60)).padStart(2, '0')}:${String(minutes % 60).padStart(2, '0')}`;
}

function teacherLabel(teacherId) {
  const teacher = teachersCache.find(item => item.id === teacherId);
  return teacher ? `${teacher.name} (${teacher.id})` : teacherId;
}

function populateTeacherOptions() {
  const select = document.getElementById('schedule-teacher');
  select.replaceChildren(new Option('Select a teacher', ''));
  teachersCache.forEach(teacher => {
    select.add(new Option(`${teacher.name} (${teacher.id})`, teacher.id));
  });

  const departments = [...new Set([
    ...teachersCache.map(teacher => teacher.department),
    ...timetableEntries.map(entry => entry.department)
  ].filter(Boolean))]
    .sort((left, right) => left.localeCompare(right));
  const departmentSelect = document.getElementById('filter-dept');
  departmentSelect.replaceChildren(new Option('All Departments', ''));
  departments.forEach(department => departmentSelect.add(new Option(department, department)));
}

function filteredEntries() {
  const search = document.getElementById('search-teacher').value.trim().toLocaleLowerCase();
  const department = document.getElementById('filter-dept').value;
  return timetableEntries.filter(entry => {
    if (department && entry.department !== department) return false;
    if (!search) return true;
    const teacher = teachersCache.find(item => item.id === entry.teacher_id);
    return [entry.teacher_id, teacher?.name, teacher?.employee_id]
      .filter(Boolean)
      .some(value => value.toLocaleLowerCase().includes(search));
  });
}

function makeTable(headers, rows) {
  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const head = document.createElement('thead');
  const headerRow = document.createElement('tr');
  headers.forEach(label => {
    const cell = document.createElement('th');
    cell.scope = 'col';
    cell.textContent = label;
    headerRow.appendChild(cell);
  });
  head.appendChild(headerRow);
  table.appendChild(head);

  const body = document.createElement('tbody');
  rows.forEach(row => {
    const tr = document.createElement('tr');
    row.forEach(value => {
      const cell = document.createElement('td');
      if (value instanceof Node) cell.appendChild(value);
      else cell.textContent = value;
      tr.appendChild(cell);
    });
    body.appendChild(tr);
  });
  table.appendChild(body);
  return table;
}

function makeActions(entry) {
  const container = document.createElement('div');
  container.className = 'schedule-actions';
  const edit = document.createElement('button');
  edit.type = 'button';
  edit.className = 'btn-secondary';
  edit.textContent = 'Edit';
  edit.addEventListener('click', () => openScheduleForm(entry));
  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'btn-secondary';
  remove.textContent = 'Delete';
  remove.addEventListener('click', () => deleteSchedule(entry.id));
  container.append(edit, remove);
  return container;
}

function renderScheduleTable(entries, showDay) {
  const root = document.getElementById('timetable-root');
  if (!entries.length) {
    root.textContent = 'No schedule entries match the selected filters.';
    return;
  }
  const rows = entries.map(entry => [
    ...(showDay ? [entry.day] : []),
    `${entry.start}–${entry.end}`,
    teacherLabel(entry.teacher_id),
    entry.subject,
    entry.class_section,
    entry.department,
    `${entry.building}, Floor ${entry.floor}, Room ${entry.room_number}`,
    makeActions(entry)
  ]);
  root.appendChild(makeTable([
    ...(showDay ? ['Day'] : []),
    'Time', 'Teacher', 'Subject', 'Class / Section', 'Department', 'Room', 'Actions'
  ], rows));
}

function renderWeeklySchedule(entries) {
  const root = document.getElementById('timetable-root');
  const grid = document.createElement('div');
  grid.className = 'weekly-grid';
  WEEK_DAYS.forEach(day => {
    const column = document.createElement('section');
    column.className = 'weekly-day';
    const heading = document.createElement('h3');
    heading.textContent = day;
    column.appendChild(heading);

    const dayEntries = entries.filter(entry => entry.day === day)
      .sort((left, right) => left.start.localeCompare(right.start));
    if (!dayEntries.length) {
      const empty = document.createElement('p');
      empty.textContent = 'No classes';
      column.appendChild(empty);
    }
    dayEntries.forEach(entry => {
      const card = document.createElement('article');
      card.className = 'weekly-schedule';
      const subject = document.createElement('h4');
      subject.textContent = entry.subject;
      const details = document.createElement('p');
      details.textContent = `${entry.start}–${entry.end} · ${entry.class_section}`;
      const teacher = document.createElement('p');
      teacher.textContent = teacherLabel(entry.teacher_id);
      const room = document.createElement('p');
      room.textContent = `${entry.department} · ${entry.building}, Floor ${entry.floor}, Room ${entry.room_number}`;
      card.append(subject, details, teacher, room, makeActions(entry));
      column.appendChild(card);
    });
    grid.appendChild(column);
  });
  root.appendChild(grid);
}

function renderFreePeriods(entries) {
  const root = document.getElementById('timetable-root');
  const teachers = new Map();
  const search = document.getElementById('search-teacher').value.trim().toLocaleLowerCase();
  const department = document.getElementById('filter-dept').value;
  teachersCache
    .filter(teacher => (!department || teacher.department === department) &&
      (!search || [teacher.id, teacher.name, teacher.employee_id]
        .filter(Boolean)
        .some(value => value.toLocaleLowerCase().includes(search))))
    .forEach(teacher => teachers.set(teacher.id, []));
  entries.forEach(entry => {
    const teacherEntries = teachers.get(entry.teacher_id);
    if (teacherEntries) teacherEntries.push(entry);
  });
  if (!teachers.size) {
    root.textContent = 'No teachers match the selected filters.';
    return;
  }

  const rows = [];
  teachers.forEach((teacherEntries, teacherId) => {
    WEEK_DAYS.forEach(day => {
      const dayEntries = teacherEntries
        .filter(entry => entry.day === day)
        .map(entry => ({ start: timeToMinutes(entry.start), end: timeToMinutes(entry.end) }))
        .filter(entry => entry.end > DAY_START && entry.start < DAY_END)
        .sort((left, right) => left.start - right.start);
      const freePeriods = [];
      let cursor = DAY_START;
      dayEntries.forEach(entry => {
        const start = Math.max(entry.start, DAY_START);
        const end = Math.min(entry.end, DAY_END);
        if (start > cursor) freePeriods.push(`${formatTime(cursor)}–${formatTime(start)}`);
        cursor = Math.max(cursor, end);
      });
      if (cursor < DAY_END) freePeriods.push(`${formatTime(cursor)}–${formatTime(DAY_END)}`);
      rows.push([
        teacherLabel(teacherId),
        day,
        freePeriods.length ? freePeriods.join(', ') : 'No free periods'
      ]);
    });
  });
  root.appendChild(makeTable(['Teacher', 'Day', 'Free periods (08:00–18:00)'], rows));
}

function renderTimetable() {
  const root = document.getElementById('timetable-root');
  root.replaceChildren();
  const entries = filteredEntries();
  if (currentView === 'free') {
    renderFreePeriods(entries);
    return;
  }
  if (currentView === 'daily') {
    const day = document.getElementById('daily-day').value;
    renderScheduleTable(entries.filter(entry => entry.day === day)
      .sort((left, right) => left.start.localeCompare(right.start)), false);
    return;
  }
  renderWeeklySchedule(entries);
}

async function loadTimetable() {
  setMessage('Loading schedules…');
  try {
    const [teachers, entries] = await Promise.all([
      fetchJson(API_TEACHERS),
      fetchJson(API_TIMETABLE)
    ]);
    teachersCache = teachers;
    timetableEntries = entries;
    populateTeacherOptions();
    renderTimetable();
    setMessage(`${entries.length} schedule ${entries.length === 1 ? 'entry' : 'entries'} loaded.`);
  } catch (error) {
    setMessage(error.message, true);
    document.getElementById('timetable-root').replaceChildren();
  }
}

function openScheduleForm(entry = null) {
  editingEntryId = entry?.id || null;
  const form = document.getElementById('schedule-form');
  form.reset();
  document.getElementById('schedule-dialog-title').textContent = entry ? 'Edit Schedule' : 'Add Schedule';
  document.getElementById('schedule-error').textContent = '';
  if (entry) {
    form.elements.teacher_id.value = entry.teacher_id;
    form.elements.subject.value = entry.subject;
    form.elements.class_section.value = entry.class_section;
    form.elements.department.value = entry.department;
    form.elements.day.value = entry.day;
    form.elements.start.value = entry.start;
    form.elements.end.value = entry.end;
    form.elements.building.value = entry.building;
    form.elements.floor.value = entry.floor;
    form.elements.room_number.value = entry.room_number;
  }
  document.getElementById('schedule-dialog').showModal();
}

function findScheduleConflict(schedule, ignoredId) {
  const start = timeToMinutes(schedule.start);
  const end = timeToMinutes(schedule.end);
  if (start >= end) return 'End time must be after start time.';

  const overlappingEntry = timetableEntries.find(entry => {
    if (entry.id === ignoredId || entry.day !== schedule.day) return false;
    const overlaps = start < timeToMinutes(entry.end) && timeToMinutes(entry.start) < end;
    if (!overlaps) return false;
    if (entry.teacher_id === schedule.teacher_id) return true;
    return entry.floor === schedule.floor &&
      entry.building.trim().toLocaleLowerCase() === schedule.building.trim().toLocaleLowerCase() &&
      entry.room_number.trim().toLocaleLowerCase() === schedule.room_number.trim().toLocaleLowerCase();
  });
  if (!overlappingEntry) return '';
  return overlappingEntry.teacher_id === schedule.teacher_id
    ? 'This teacher already has a class during the selected time.'
    : 'This room is already allocated during the selected time.';
}

async function saveSchedule(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const payload = {
    teacher_id: form.elements.teacher_id.value,
    subject: form.elements.subject.value.trim(),
    class_section: form.elements.class_section.value.trim(),
    department: form.elements.department.value.trim(),
    day: form.elements.day.value,
    start: form.elements.start.value,
    end: form.elements.end.value,
    building: form.elements.building.value.trim(),
    floor: Number(form.elements.floor.value),
    room_number: form.elements.room_number.value.trim()
  };
  const error = document.getElementById('schedule-error');
  error.textContent = '';
  const conflict = findScheduleConflict(payload, editingEntryId);
  if (conflict) {
    error.textContent = conflict;
    return;
  }
  try {
    await fetchJson(editingEntryId ? `${API_TIMETABLE}/${encodeURIComponent(editingEntryId)}` : API_TIMETABLE, {
      method: editingEntryId ? 'PUT' : 'POST',
      body: JSON.stringify(payload)
    });
    document.getElementById('schedule-dialog').close();
    await loadTimetable();
  } catch (requestError) {
    error.textContent = requestError.message;
  }
}

async function deleteSchedule(entryId) {
  if (!window.confirm('Delete this schedule entry?')) return;
  try {
    await fetchJson(`${API_TIMETABLE}/${encodeURIComponent(entryId)}`, { method: 'DELETE' });
    await loadTimetable();
  } catch (error) {
    setMessage(error.message, true);
  }
}

function setView(view) {
  currentView = view;
  document.querySelectorAll('.timetable-views button').forEach(button => {
    button.setAttribute('aria-pressed', String(button.id === `view-${view}-btn` ||
      (view === 'free' && button.id === 'free-periods-btn')));
  });
  renderTimetable();
}

document.addEventListener('DOMContentLoaded', async () => {
  if (!sessionStorage.getItem('authToken')) {
    window.location.href = 'login.html';
    return;
  }

  document.getElementById('add-schedule-btn').addEventListener('click', () => openScheduleForm());
  document.getElementById('schedule-form').addEventListener('submit', saveSchedule);
  document.getElementById('cancel-schedule-btn').addEventListener('click', () => {
    document.getElementById('schedule-dialog').close();
  });
  document.getElementById('view-weekly-btn').addEventListener('click', () => setView('weekly'));
  document.getElementById('view-daily-btn').addEventListener('click', () => setView('daily'));
  document.getElementById('free-periods-btn').addEventListener('click', () => setView('free'));
  document.getElementById('search-teacher').addEventListener('input', renderTimetable);
  document.getElementById('filter-dept').addEventListener('change', renderTimetable);
  document.getElementById('daily-day').addEventListener('change', renderTimetable);
  document.getElementById('schedule-teacher').addEventListener('change', event => {
    const teacher = teachersCache.find(item => item.id === event.target.value);
    if (teacher) document.getElementById('schedule-department').value = teacher.department || '';
  });
  await loadTimetable();
});
