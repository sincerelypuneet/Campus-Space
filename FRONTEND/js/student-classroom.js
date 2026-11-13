const STUDENT_CLASSROOMS_API = '/api/student/classrooms';
const STUDENT_CLASSROOM_BOOKINGS_API = '/api/student/classroom-bookings';

let classroomSearch = null;
let selectedClassroom = null;

function setClassroomSearchMessage(message, isError = false) {
  const element = document.getElementById('classroom-search-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

async function classroomApiRequest(url, options = {}) {
  const response = await fetch(url, {
    ...options,
    headers: {
      ...(options.body ? { 'Content-Type': 'application/json' } : {}),
      ...(options.headers || {})
    }
  });
  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error('The server returned an invalid response.');
  }
  if (!response.ok) throw new Error(data.message || 'Classroom request failed.');
  return data;
}

function currentClassroomSearch() {
  const form = document.getElementById('classroom-search-form');
  const params = new URLSearchParams({
    date: form.elements.date.value,
    start: form.elements.start.value,
    end: form.elements.end.value,
    sort: form.elements.sort.value
  });
  if (form.elements.building.value.trim()) params.set('building', form.elements.building.value.trim());
  if (form.elements.floor.value !== '') params.set('floor', form.elements.floor.value);
  if (form.elements.capacity.value !== '') params.set('min_capacity', form.elements.capacity.value);
  return params;
}

function addClassroomDetail(parent, label, value) {
  const detail = document.createElement('p');
  detail.textContent = `${label}: ${value}`;
  parent.appendChild(detail);
}

function showClassroomDetails(classroom) {
  selectedClassroom = classroom;
  const content = document.getElementById('classroom-details-content');
  content.replaceChildren();
  const title = document.getElementById('classroom-details-title');
  title.textContent = `Room ${classroom.room_number}`;
  addClassroomDetail(content, 'Building', classroom.building);
  addClassroomDetail(content, 'Floor', classroom.floor);
  addClassroomDetail(content, 'Seating capacity', classroom.capacity);
  addClassroomDetail(content, 'Facilities', classroom.features.length ? classroom.features.join(', ') : 'None listed');
  addClassroomDetail(content, 'Availability', classroom.availability_status);
  document.getElementById('classroom-details').showModal();
}

function makeClassroomCard(classroom) {
  const card = document.createElement('article');
  card.className = 'classroom-result';
  const title = document.createElement('h3');
  title.textContent = `Room ${classroom.room_number}`;
  const status = document.createElement('span');
  status.className = 'classroom-status';
  status.textContent = classroom.availability_status;
  const details = document.createElement('div');
  details.className = 'classroom-result-details';
  addClassroomDetail(details, 'Building', classroom.building);
  addClassroomDetail(details, 'Floor', classroom.floor);
  addClassroomDetail(details, 'Seating capacity', classroom.capacity);
  addClassroomDetail(details, 'Facilities', classroom.features.length ? classroom.features.join(', ') : 'None listed');
  const actions = document.createElement('div');
  actions.className = 'classroom-result-actions';
  const view = document.createElement('button');
  view.type = 'button';
  view.className = 'btn-secondary';
  view.textContent = 'Classroom details';
  view.addEventListener('click', () => showClassroomDetails(classroom));
  const reserve = document.createElement('button');
  reserve.type = 'button';
  reserve.className = 'btn-primary';
  reserve.textContent = 'Reserve';
  reserve.addEventListener('click', () => reserveClassroom(classroom));
  actions.append(view, reserve);
  card.append(title, status, details, actions);
  return card;
}

function renderClassroomResults(classrooms) {
  const root = document.getElementById('classroom-results');
  root.replaceChildren();
  if (!classrooms.length) {
    root.textContent = 'No classrooms are available for this time and these filters.';
    return;
  }
  classrooms.forEach(classroom => {
    classroom.features = Array.isArray(classroom.features) ? classroom.features : [];
    root.appendChild(makeClassroomCard(classroom));
  });
}

async function searchClassrooms(event) {
  if (event) event.preventDefault();
  const root = document.getElementById('classroom-results');
  try {
    classroomSearch = currentClassroomSearch();
    const data = await classroomApiRequest(`${STUDENT_CLASSROOMS_API}?${classroomSearch}`);
    renderClassroomResults(data.classrooms);
    setClassroomSearchMessage(
      `${data.classrooms.length} available classroom${data.classrooms.length === 1 ? '' : 's'} found.`
    );
  } catch (error) {
    root.replaceChildren();
    setClassroomSearchMessage(error.message, true);
  }
}

async function reserveClassroom(classroom) {
  if (!classroomSearch) return;
  const query = classroomSearch;
  const studentName = sessionStorage.getItem('studentName') || '';
  try {
    const data = await classroomApiRequest(STUDENT_CLASSROOM_BOOKINGS_API, {
      method: 'POST',
      body: JSON.stringify({
        classroom_id: classroom.id,
        date: query.get('date'),
        start: query.get('start'),
        end: query.get('end'),
        student_name: studentName
      })
    });
    document.getElementById('classroom-details').close();
    await searchClassrooms();
    setClassroomSearchMessage(data.message);
  } catch (error) {
    await searchClassrooms();
    setClassroomSearchMessage(error.message, true);
  }
}

document.addEventListener('DOMContentLoaded', () => {
  if (sessionStorage.getItem('studentAuthenticated') !== 'true') {
    window.location.replace('student-login.html');
    return;
  }

  const form = document.getElementById('classroom-search-form');
  const dateInput = form.elements.date;
  const today = new Date();
  dateInput.min = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`;
  form.addEventListener('submit', searchClassrooms);
  form.addEventListener('reset', () => {
    window.setTimeout(() => {
      classroomSearch = null;
      document.getElementById('classroom-results').replaceChildren();
      setClassroomSearchMessage('');
    });
  });
  const invalidateSearch = () => {
    if (!classroomSearch) return;
    classroomSearch = null;
    selectedClassroom = null;
    document.getElementById('classroom-results').replaceChildren();
    const dialog = document.getElementById('classroom-details');
    if (dialog.open) dialog.close();
    setClassroomSearchMessage('Search again to refresh classroom availability.');
  };
  form.addEventListener('input', invalidateSearch);
  form.addEventListener('change', invalidateSearch);
  document.getElementById('reserve-classroom').addEventListener('click', () => {
    if (selectedClassroom) reserveClassroom(selectedClassroom);
  });
  document.getElementById('close-classroom-details').addEventListener('click', () => {
    document.getElementById('classroom-details').close();
  });
});
