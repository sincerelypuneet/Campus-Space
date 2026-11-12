const API_CLASSROOMS = '/api/classrooms';

let classrooms = [];
let editingClassroomId = null;

async function fetchClassroomJson(url, options = {}) {
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

function setClassroomMessage(message, isError = false) {
  const element = document.getElementById('classroom-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function renderClassrooms() {
  const root = document.getElementById('classroom-list');
  root.replaceChildren();
  if (!classrooms.length) {
    root.textContent = 'No classrooms have been added yet.';
    return;
  }

  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const head = document.createElement('thead');
  const headerRow = document.createElement('tr');
  ['Building', 'Floor', 'Room number', 'Capacity', 'Features', 'Actions'].forEach(label => {
    const cell = document.createElement('th');
    cell.scope = 'col';
    cell.textContent = label;
    headerRow.appendChild(cell);
  });
  head.appendChild(headerRow);
  table.appendChild(head);

  const body = document.createElement('tbody');
  classrooms.forEach(classroom => {
    const row = document.createElement('tr');
    [
      classroom.building,
      classroom.floor,
      classroom.room_number || '—',
      classroom.capacity,
      (classroom.features || []).join(', ') || '—'
    ].forEach(value => {
      const cell = document.createElement('td');
      cell.textContent = String(value);
      row.appendChild(cell);
    });

    const actions = document.createElement('td');
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.className = 'btn-secondary';
    edit.textContent = 'Edit';
    edit.addEventListener('click', () => editClassroom(classroom));
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'btn-secondary';
    remove.textContent = 'Delete';
    remove.addEventListener('click', () => deleteClassroom(classroom));
    actions.append(edit, remove);
    row.appendChild(actions);
    body.appendChild(row);
  });
  table.appendChild(body);
  root.appendChild(table);
}

async function loadClassrooms() {
  setClassroomMessage('Loading classrooms…');
  try {
    classrooms = await fetchClassroomJson(API_CLASSROOMS);
    renderClassrooms();
    setClassroomMessage(`${classrooms.length} classroom${classrooms.length === 1 ? '' : 's'} loaded.`);
  } catch (error) {
    setClassroomMessage(error.message, true);
    document.getElementById('classroom-list').replaceChildren();
  }
}

function resetClassroomForm() {
  editingClassroomId = null;
  document.getElementById('classroom-form').reset();
  document.getElementById('classroom-form-title').textContent = 'Add Classroom';
  document.getElementById('save-classroom').textContent = 'Save Classroom';
  document.getElementById('cancel-classroom-edit').hidden = true;
}

function editClassroom(classroom) {
  editingClassroomId = classroom.id;
  const form = document.getElementById('classroom-form');
  form.elements.building.value = classroom.building;
  form.elements.floor.value = classroom.floor;
  form.elements.room_number.value = classroom.room_number || '';
  form.elements.capacity.value = classroom.capacity;
  form.elements.features.value = (classroom.features || []).join(', ');
  document.getElementById('classroom-form-title').textContent = 'Edit Classroom';
  document.getElementById('save-classroom').textContent = 'Update Classroom';
  document.getElementById('cancel-classroom-edit').hidden = false;
  document.getElementById('classroom-building').focus();
}

async function saveClassroom(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const payload = {
    building: form.elements.building.value.trim(),
    floor: Number(form.elements.floor.value),
    room_number: form.elements.room_number.value.trim(),
    capacity: Number(form.elements.capacity.value),
    features: form.elements.features.value.split(',').map(feature => feature.trim()).filter(Boolean)
  };
  try {
    await fetchClassroomJson(
      editingClassroomId ? `${API_CLASSROOMS}/${encodeURIComponent(editingClassroomId)}` : API_CLASSROOMS,
      { method: editingClassroomId ? 'PUT' : 'POST', body: JSON.stringify(payload) }
    );
    resetClassroomForm();
    await loadClassrooms();
  } catch (error) {
    setClassroomMessage(error.message, true);
  }
}

async function deleteClassroom(classroom) {
  if (!window.confirm(`Delete Room ${classroom.room_number} in ${classroom.building}?`)) return;
  try {
    await fetchClassroomJson(`${API_CLASSROOMS}/${encodeURIComponent(classroom.id)}`, { method: 'DELETE' });
    if (editingClassroomId === classroom.id) resetClassroomForm();
    await loadClassrooms();
  } catch (error) {
    setClassroomMessage(error.message, true);
  }
}

document.addEventListener('DOMContentLoaded', async () => {
  if (!sessionStorage.getItem('authToken')) {
    window.location.href = 'login.html?returnTo=admin-classrooms.html';
    return;
  }
  document.getElementById('classroom-form').addEventListener('submit', saveClassroom);
  document.getElementById('cancel-classroom-edit').addEventListener('click', resetClassroomForm);
  await loadClassrooms();
});
