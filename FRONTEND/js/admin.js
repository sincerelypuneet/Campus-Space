// admin.js – UI logic for admin pages (teacher management with API integration)

// Base API URL (relative)
const API_BASE = '/api/teachers';

// Load teachers from API (fallback to static JSON on error)
async function loadTeachers() {
  try {
    const resp = await fetch(API_BASE);
    if (!resp.ok) throw new Error('API request failed');
    const teachers = await resp.json();
    renderTeacherTable(teachers);
  } catch (e) {
    console.warn('API error, loading static data:', e);
    const fallback = await fetch('/data/teachers.json');
    if (fallback.ok) {
      const teachers = await fallback.json();
      renderTeacherTable(teachers);
    } else {
      alert('Unable to load teacher data');
    }
  }
}

function renderTeacherTable(teachers) {
  const root = document.getElementById('admin-root');
  if (!root) return;
  const table = document.createElement('table');
  table.className = 'table';
  const thead = document.createElement('thead');
  thead.innerHTML = `<tr>
    <th>ID</th><th>Name</th><th>Department</th><th>Status</th><th>Actions</th>
  </tr>`;
  table.appendChild(thead);
  const tbody = document.createElement('tbody');
  teachers.forEach(t => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${t.id}</td><td>${t.name}</td><td>${t.department}</td><td>${t.status || 'active'}</td>` +
      `<td>
        <button class="btn-primary" onclick="viewTeacher('${t.id}')">View</button>
        <button class="btn-secondary" onclick="editTeacher('${t.id}')">Edit</button>
        <button class="btn" onclick="deleteTeacher('${t.id}')">Delete</button>
       </td>`;
    tbody.appendChild(tr);
  });
  table.appendChild(tbody);
  root.innerHTML = '';
  root.appendChild(table);
}

// View teacher details (placeholder – could be expanded to a modal)
async function viewTeacher(id) {
  try {
    const resp = await fetch(`${API_BASE}/${id}`);
    if (!resp.ok) throw new Error('Not found');
    const teacher = await resp.json();
    alert(`Teacher Details\nID: ${teacher.id}\nName: ${teacher.name}\nDept: ${teacher.department}\nStatus: ${teacher.status || 'active'}`);
  } catch (e) {
    alert('Failed to load teacher');
  }
}

// Edit teacher – minimal prompt UI, then PUT to API
async function editTeacher(id) {
  const name = prompt('Enter new name');
  const department = prompt('Enter new department');
  if (!name || !department) { alert('All fields required'); return; }
  const payload = { name, department };
  try {
    const resp = await fetch(`${API_BASE}/${id}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (!resp.ok) throw new Error('Update failed');
    await loadTeachers();
    alert('Teacher updated');
  } catch (e) {
    alert('Failed to update teacher');
  }
}

// Delete teacher – calls DELETE endpoint
async function deleteTeacher(id) {
  if (!confirm('Delete teacher ' + id + '?')) return;
  try {
    const resp = await fetch(`${API_BASE}/${id}`, { method: 'DELETE' });
    if (!resp.ok) throw new Error('Delete failed');
    await loadTeachers();
    alert('Teacher deleted');
  } catch (e) {
    alert('Failed to delete teacher');
  }
}

// Add teacher – prompts for required fields, then POST to API
async function addTeacherPrompt() {
  const name = prompt('Enter Name');
  const employee_id = prompt('Enter Employee ID');
  const department = prompt('Enter Department');
  const designation = prompt('Enter Designation');
  const subjectsStr = prompt('Enter subjects (comma separated)');
  if (!name || !employee_id || !department || !designation || !subjectsStr) { alert('All fields required'); return; }
  const subjects = subjectsStr.split(',').map(s => s.trim()).filter(Boolean);
  const payload = { name, employee_id, department, designation, subjects };
  try {
    const resp = await fetch(API_BASE, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (!resp.ok) throw new Error('Create failed');
    await loadTeachers();
    alert('Teacher added');
  } catch (e) {
    alert('Failed to add teacher');
  }
}

// Initialise – load table and bind Add Teacher button
document.addEventListener('DOMContentLoaded', () => {
  const token = sessionStorage.getItem('authToken');
  if (!token) {
    const loginPanel = document.getElementById('login-panel');
    if (loginPanel) loginPanel.style.display = 'block';
    return;
  }

  loadTeachers();
  const addBtn = document.getElementById('add-teacher-btn');
  if (addBtn) addBtn.addEventListener('click', addTeacherPrompt);
});
