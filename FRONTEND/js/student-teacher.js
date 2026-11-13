const STUDENT_TEACHER_API = '/api/student/teachers';
const TEACHER_WEEK_DAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];

let availableTeachers = [];
let teacherSchedules = [];

function setTeacherSearchMessage(message, isError = false) {
  const element = document.getElementById('teacher-search-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function minutesFromTime(value) {
  const match = /^([01]\d|2[0-3]):([0-5]\d)$/.exec(value || '');
  return match ? Number(match[1]) * 60 + Number(match[2]) : null;
}

function timeFromMinutes(value) {
  return `${String(Math.floor(value / 60)).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}`;
}

function formatRoom(entry) {
  const parts = [];
  if (entry.building) parts.push(entry.building);
  if (entry.floor !== undefined && entry.floor !== null) parts.push(`Floor ${entry.floor}`);
  if (entry.room_number) parts.push(`Room ${entry.room_number}`);
  return parts.join(', ') || entry.classroom || 'Classroom not specified';
}

function entriesForTeacherDay(teacherId, day) {
  return teacherSchedules
    .filter(entry => entry.teacher_id === teacherId && entry.day === day)
    .map(entry => ({
      ...entry,
      startMinutes: minutesFromTime(entry.start),
      endMinutes: minutesFromTime(entry.end)
    }))
    .filter(entry => entry.startMinutes !== null && entry.endMinutes !== null && entry.endMinutes > entry.startMinutes)
    .sort((left, right) => left.startMinutes - right.startMinutes);
}

function teacherWorkingDays(teacher) {
  return Array.isArray(teacher.working_days) ? teacher.working_days : [];
}

function workingWindow(teacher, day, entries = entriesForTeacherDay(teacher.id, day)) {
  const configuredStart = (teacher.working_start || '').trim();
  const configuredEnd = (teacher.working_end || '').trim();
  if (configuredStart || configuredEnd) {
    const start = minutesFromTime(configuredStart);
    const end = minutesFromTime(configuredEnd);
    return start !== null && end !== null && start < end ? { start, end } : null;
  }

  if (!entries.length) return null;
  return {
    start: Math.min(...entries.map(entry => entry.startMinutes)),
    end: Math.max(...entries.map(entry => entry.endMinutes))
  };
}

function isWorkingDay(teacher, day) {
  const configuredDays = teacherWorkingDays(teacher);
  if (configuredDays.length) {
    return configuredDays.some(value => value.toLocaleLowerCase() === day.toLocaleLowerCase());
  }
  return teacherSchedules.some(entry => entry.teacher_id === teacher.id && entry.day === day);
}

function getTargetDay() {
  const date = document.getElementById('teacher-date').value;
  if (date) {
    const parsedDate = new Date(`${date}T12:00:00`);
    return TEACHER_WEEK_DAYS[(parsedDate.getDay() + 6) % 7];
  }
  return document.getElementById('teacher-day').value ||
    TEACHER_WEEK_DAYS[(new Date().getDay() + 6) % 7];
}

function targetTimeForDay(day, window) {
  const date = document.getElementById('teacher-date').value;
  const now = new Date();
  const todayDate = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
  const isToday = date ? date === todayDate : day === TEACHER_WEEK_DAYS[(now.getDay() + 6) % 7];
  return isToday ? now.getHours() * 60 + now.getMinutes() : window.start;
}

function getSearchPeriod() {
  const startValue = document.getElementById('teacher-start').value;
  const endValue = document.getElementById('teacher-end').value;
  if (Boolean(startValue) !== Boolean(endValue)) {
    throw new Error('Enter both a preferred start time and end time.');
  }
  if (!startValue) return null;
  const start = minutesFromTime(startValue);
  const end = minutesFromTime(endValue);
  if (start === null || end === null || start >= end) {
    throw new Error('Preferred end time must be after the start time.');
  }
  return { start, end };
}

function teacherSubjects(teacher) {
  const subjects = new Set(Array.isArray(teacher.subjects) ? teacher.subjects.filter(Boolean) : []);
  teacherSchedules
    .filter(entry => entry.teacher_id === teacher.id && entry.subject)
    .forEach(entry => subjects.add(entry.subject));
  return [...subjects];
}

function matchesSearch(teacher, query) {
  if (query.name && !teacher.name.toLocaleLowerCase().includes(query.name)) return false;
  if (query.department && teacher.department !== query.department) return false;
  if (query.subject && !teacherSubjects(teacher).some(subject =>
    subject.toLocaleLowerCase().includes(query.subject))) return false;
  return true;
}

function availabilityFor(teacher, day, period) {
  const configuredDays = teacherWorkingDays(teacher);
  if (configuredDays.length &&
      !configuredDays.some(value => value.toLocaleLowerCase() === day.toLocaleLowerCase())) {
    return { status: 'Unavailable', currentClasses: [], window: null, entries: [] };
  }
  const entries = entriesForTeacherDay(teacher.id, day);
  if (!configuredDays.length && !entries.length) {
    return { status: 'Hours not configured', currentClasses: [], window: null, entries };
  }
  const window = workingWindow(teacher, day, entries);
  if (!window) {
    return { status: 'Hours not configured', currentClasses: [], window: null, entries };
  }

  let checkStart;
  let checkEnd;
  if (period) {
    checkStart = period.start;
    checkEnd = period.end;
  } else {
    checkStart = targetTimeForDay(day, window);
    checkEnd = checkStart;
  }

  const currentClasses = entries.filter(entry => period
    ? checkStart < entry.endMinutes && entry.startMinutes < checkEnd
    : checkStart >= entry.startMinutes && checkStart < entry.endMinutes);
  const insideWorkingHours = checkStart >= window.start &&
    (period ? checkEnd <= window.end : checkStart < window.end);
  return {
    status: currentClasses.length ? 'Busy' : insideWorkingHours ? 'Available' : 'Unavailable',
    currentClasses,
    window,
    entries
  };
}

function nextAvailablePeriod(availability, period, day) {
  if (!availability.window || !isWorkingDayForResult(day)) return 'Not configured';
  let cursor = period ? period.end : targetTimeForDay(day, availability.window);
  cursor = Math.max(cursor, availability.window.start);
  if (cursor >= availability.window.end) return 'No remaining working time';

  for (const entry of availability.entries) {
    if (entry.endMinutes <= cursor) continue;
    if (entry.startMinutes > cursor) {
      return `${timeFromMinutes(cursor)}–${timeFromMinutes(Math.min(entry.startMinutes, availability.window.end))}`;
    }
    cursor = Math.max(cursor, entry.endMinutes);
    if (cursor >= availability.window.end) return 'No remaining working time';
  }
  return `${timeFromMinutes(cursor)}–${timeFromMinutes(availability.window.end)}`;
}

function isWorkingDayForResult(day) {
  return TEACHER_WEEK_DAYS.includes(day);
}

function makeProfileButton(teacher) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'btn-secondary';
  button.textContent = 'View Profile';
  button.addEventListener('click', () => showTeacherProfile(teacher));
  return button;
}

function renderTeacherResults(teachers, day, period) {
  const root = document.getElementById('teacher-results');
  root.replaceChildren();
  if (!teachers.length) {
    root.textContent = 'No teachers match those filters.';
    return;
  }

  teachers.forEach(teacher => {
    const availability = availabilityFor(teacher, day, period);
    const card = document.createElement('article');
    card.className = 'teacher-result';
    const title = document.createElement('h3');
    title.textContent = teacher.name;
    const details = document.createElement('p');
    details.textContent = `${teacher.department || 'Department not specified'} · ${teacher.designation || 'Designation not specified'}`;
    const subjects = document.createElement('p');
    subjects.textContent = `Subject: ${teacherSubjects(teacher).join(', ') || 'Not specified'}`;
    const status = document.createElement('p');
    status.className = `teacher-availability teacher-availability-${availability.status.toLocaleLowerCase().replace(/ /g, '-')}`;
    status.textContent = `Status: ${availability.status}`;
    const classInfo = document.createElement('p');
    classInfo.textContent = `Current class: ${availability.currentClasses.map(entry =>
      `${entry.subject || 'Class'}${entry.class_section ? ` (${entry.class_section})` : ''}`
    ).join(', ') || 'None'}`;
    const roomInfo = document.createElement('p');
    roomInfo.textContent = `Current classroom: ${availability.currentClasses.map(formatRoom).join('; ') || 'None'}`;
    const next = document.createElement('p');
    next.textContent = `Next available period: ${availability.status === 'Busy' || availability.status === 'Available'
      ? nextAvailablePeriod(availability, period, day)
      : 'Not configured'}`;
    card.append(title, details, subjects, status, classInfo, roomInfo, next, makeProfileButton(teacher));
    root.appendChild(card);
  });
}

function makeProfileTable(teacher, day) {
  const section = document.createElement('section');
  section.className = 'teacher-profile-day';
  const heading = document.createElement('h3');
  heading.textContent = day;
  const entries = entriesForTeacherDay(teacher.id, day);
  const window = workingWindow(teacher, day, entries);
  const rows = [];
  let cursor = window ? window.start : null;
  entries.forEach(entry => {
    if (cursor !== null && entry.startMinutes > cursor) {
      rows.push(['Free period', `${timeFromMinutes(cursor)}–${timeFromMinutes(entry.startMinutes)}`, 'Free', '—']);
    }
    rows.push([
      `${entry.start}–${entry.end}`,
      entry.subject || 'Class',
      entry.class_section || '—',
      formatRoom(entry)
    ]);
    if (cursor !== null) cursor = Math.max(cursor, entry.endMinutes);
  });
  if (window && cursor !== null && cursor < window.end) {
    rows.push(['Free period', `${timeFromMinutes(cursor)}–${timeFromMinutes(window.end)}`, 'Free', '—']);
  }

  if (!rows.length) {
    const message = document.createElement('p');
    message.textContent = isWorkingDay(teacher, day)
      ? window
        ? `No classes are scheduled during working hours (${timeFromMinutes(window.start)}–${timeFromMinutes(window.end)}).`
        : 'No classes are scheduled and working hours are not configured.'
      : 'No working hours or scheduled classes configured.';
    section.append(heading, message);
    return section;
  }

  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const thead = document.createElement('thead');
  const header = document.createElement('tr');
  ['Time', 'Subject', 'Class / Section', 'Assigned classroom'].forEach(label => {
    const cell = document.createElement('th');
    cell.scope = 'col';
    cell.textContent = label;
    header.appendChild(cell);
  });
  thead.appendChild(header);
  table.appendChild(thead);
  const tbody = document.createElement('tbody');
  rows.forEach(values => {
    const row = document.createElement('tr');
    values.forEach(value => {
      const cell = document.createElement('td');
      cell.textContent = value;
      row.appendChild(cell);
    });
    tbody.appendChild(row);
  });
  table.appendChild(tbody);
  section.append(heading, table);
  return section;
}

function showTeacherProfile(teacher) {
  document.getElementById('teacher-profile-title').textContent = teacher.name;
  const content = document.getElementById('teacher-profile-content');
  content.replaceChildren();
  const summary = document.createElement('p');
  summary.textContent = `${teacher.department || 'Department not specified'} · ${teacher.designation || 'Designation not specified'}`;
  content.appendChild(summary);
  TEACHER_WEEK_DAYS.forEach(day => content.appendChild(makeProfileTable(teacher, day)));
  document.getElementById('teacher-profile').showModal();
}

function searchTeachers(event) {
  event.preventDefault();
  try {
    const selectedDay = document.getElementById('teacher-day').value;
    const dateValue = document.getElementById('teacher-date').value;
    const day = getTargetDay();
    if (dateValue && selectedDay && day !== selectedDay) {
      throw new Error('The selected date does not fall on the selected day.');
    }
    const period = getSearchPeriod();
    const query = {
      name: document.getElementById('teacher-name').value.trim().toLocaleLowerCase(),
      department: document.getElementById('teacher-department').value,
      subject: document.getElementById('teacher-subject').value.trim().toLocaleLowerCase()
    };
    const matches = availableTeachers.filter(teacher => matchesSearch(teacher, query));
    renderTeacherResults(matches, day, period);
    setTeacherSearchMessage(`${matches.length} teacher${matches.length === 1 ? '' : 's'} found for ${day}.`);
  } catch (error) {
    document.getElementById('teacher-results').replaceChildren();
    setTeacherSearchMessage(error.message, true);
  }
}

async function loadTeacherAvailability() {
  setTeacherSearchMessage('Loading teacher and timetable data…');
  try {
    const response = await fetch(STUDENT_TEACHER_API);
    const data = await response.json();
    if (!response.ok) throw new Error(data.message || 'Unable to load teacher availability.');
    availableTeachers = data.teachers;
    teacherSchedules = data.timetables;
    const departments = [...new Set(availableTeachers.map(teacher => teacher.department).filter(Boolean))]
      .sort((left, right) => left.localeCompare(right));
    const select = document.getElementById('teacher-department');
    departments.forEach(department => select.add(new Option(department, department)));
    setTeacherSearchMessage(`${availableTeachers.length} teachers and ${teacherSchedules.length} timetable entries loaded.`);
  } catch (error) {
    setTeacherSearchMessage(error.message || 'Unable to load teacher availability.', true);
  }
}

document.addEventListener('DOMContentLoaded', async () => {
  if (sessionStorage.getItem('studentAuthenticated') !== 'true') {
    window.location.replace('student-login.html');
    return;
  }

  document.getElementById('teacher-search-form').addEventListener('submit', searchTeachers);
  document.getElementById('teacher-search-form').addEventListener('reset', () => {
    window.setTimeout(() => {
      document.getElementById('teacher-results').replaceChildren();
      setTeacherSearchMessage(`${availableTeachers.length} teachers loaded. Enter filters to search.`);
    });
  });
  document.getElementById('teacher-date').addEventListener('change', event => {
    if (!event.target.value) return;
    const parsedDate = new Date(`${event.target.value}T12:00:00`);
    document.getElementById('teacher-day').value = TEACHER_WEEK_DAYS[(parsedDate.getDay() + 6) % 7];
  });
  document.getElementById('close-teacher-profile').addEventListener('click', () => {
    document.getElementById('teacher-profile').close();
  });
  await loadTeacherAvailability();
});
