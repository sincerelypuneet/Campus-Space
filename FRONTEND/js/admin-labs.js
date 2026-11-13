const LABORATORIES_API = '/api/laboratories';
const COMPUTERS_API = '/api/computers';

let laboratories = [];
let computers = [];
let editingLaboratoryId = null;
let editingComputerId = null;

async function labApiRequest(url, options = {}) {
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

function setLabMessage(message, isError = false) {
  const element = document.getElementById('lab-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function addTableCell(row, value) {
  const cell = document.createElement('td');
  cell.textContent = String(value);
  row.appendChild(cell);
  return cell;
}

function makeActionButton(label, action, className = 'btn-secondary') {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.textContent = label;
  button.addEventListener('click', action);
  return button;
}

function renderLaboratories() {
  const root = document.getElementById('laboratory-list');
  root.replaceChildren();
  if (!laboratories.length) {
    root.textContent = 'No laboratories have been added yet.';
    return;
  }

  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const head = document.createElement('thead');
  const header = document.createElement('tr');
  ['Laboratory ID', 'Name', 'Building', 'Floor', 'Room', 'Category', 'Computers', 'Lab administrator', 'Status', 'Actions']
    .forEach(label => {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = label;
      header.appendChild(cell);
    });
  head.appendChild(header);
  table.appendChild(head);

  const body = document.createElement('tbody');
  laboratories.forEach(laboratory => {
    const row = document.createElement('tr');
    [
      laboratory.id,
      laboratory.name,
      laboratory.building,
      laboratory.floor,
      laboratory.room_number,
      laboratory.category,
      laboratory.total_computers,
      laboratory.lab_administrator,
      laboratory.operational_status
    ].forEach(value => addTableCell(row, value));
    const actions = addTableCell(row, '');
    actions.append(
      makeActionButton('Edit', () => editLaboratory(laboratory)),
      makeActionButton('Delete', () => deleteLaboratory(laboratory))
    );
    body.appendChild(row);
  });
  table.appendChild(body);
  root.appendChild(table);
}

function populateLaboratorySelect(selectedId = '') {
  const select = document.getElementById('computer-lab');
  select.replaceChildren();
  if (!laboratories.length) {
    select.add(new Option('Add a laboratory first', ''));
    select.disabled = true;
    document.getElementById('save-computer').disabled = true;
    return;
  }
  select.disabled = false;
  document.getElementById('save-computer').disabled = false;
  laboratories.forEach(laboratory => {
    select.add(new Option(`${laboratory.name} (${laboratory.id})`, laboratory.id));
  });
  if (selectedId && laboratories.some(laboratory => laboratory.id === selectedId)) {
    select.value = selectedId;
  }
}

function renderComputers() {
  const root = document.getElementById('computer-list');
  root.replaceChildren();
  if (!computers.length) {
    root.textContent = 'No computers have been added yet.';
    return;
  }

  const labNames = new Map(laboratories.map(laboratory => [laboratory.id, laboratory.name]));
  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const head = document.createElement('thead');
  const header = document.createElement('tr');
  ['PC ID', 'Laboratory', 'Computer number', 'Specifications', 'Operational status', 'Actions']
    .forEach(label => {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = label;
      header.appendChild(cell);
    });
  head.appendChild(header);
  table.appendChild(head);

  const body = document.createElement('tbody');
  computers.forEach(computer => {
    const row = document.createElement('tr');
    addTableCell(row, computer.id);
    addTableCell(row, `${labNames.get(computer.lab_id) || 'Unknown lab'} (${computer.lab_id})`);
    addTableCell(row, computer.computer_number);
    addTableCell(row, computer.specifications);

    const statusCell = document.createElement('td');
    const statusSelect = document.createElement('select');
    statusSelect.className = 'input-field';
    [
      ['available', 'Available'],
      ['occupied', 'Occupied'],
      ['maintenance', 'Under maintenance']
    ].forEach(([value, label]) => statusSelect.add(new Option(label, value)));
    statusSelect.value = computer.status;
    statusCell.appendChild(statusSelect);
    row.appendChild(statusCell);

    const actions = addTableCell(row, '');
    actions.append(
      makeActionButton('Save status', () => updateComputerStatus(computer, statusSelect.value), 'btn-primary'),
      makeActionButton('Edit', () => editComputer(computer)),
      makeActionButton('Delete', () => deleteComputer(computer))
    );
    body.appendChild(row);
  });
  table.appendChild(body);
  root.appendChild(table);
}

async function loadInventory() {
  setLabMessage('Loading laboratories and computers…');
  try {
    [laboratories, computers] = await Promise.all([
      labApiRequest(LABORATORIES_API),
      labApiRequest(COMPUTERS_API)
    ]);
    renderLaboratories();
    renderComputers();
    populateLaboratorySelect(
      editingComputerId
        ? computers.find(computer => computer.id === editingComputerId)?.lab_id
        : ''
    );
    setLabMessage(`${laboratories.length} laborator${laboratories.length === 1 ? 'y' : 'ies'} and ${computers.length} computer${computers.length === 1 ? '' : 's'} loaded.`);
  } catch (error) {
    setLabMessage(error.message, true);
    document.getElementById('laboratory-list').replaceChildren();
    document.getElementById('computer-list').replaceChildren();
  }
}

function resetLaboratoryForm() {
  editingLaboratoryId = null;
  document.getElementById('lab-form').reset();
  document.getElementById('lab-form-title').textContent = 'Add Laboratory';
  document.getElementById('save-lab').textContent = 'Save Laboratory';
  document.getElementById('cancel-lab-edit').hidden = true;
}

function editLaboratory(laboratory) {
  editingLaboratoryId = laboratory.id;
  const form = document.getElementById('lab-form');
  form.elements.name.value = laboratory.name;
  form.elements.building.value = laboratory.building;
  form.elements.floor.value = laboratory.floor;
  form.elements.room_number.value = laboratory.room_number;
  form.elements.category.value = laboratory.category;
  form.elements.lab_administrator.value = laboratory.lab_administrator;
  form.elements.operational_status.value = laboratory.operational_status;
  document.getElementById('lab-form-title').textContent = `Edit Laboratory (${laboratory.id})`;
  document.getElementById('save-lab').textContent = 'Update Laboratory';
  document.getElementById('cancel-lab-edit').hidden = false;
  document.getElementById('lab-name').focus();
}

async function saveLaboratory(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const payload = {
    name: form.elements.name.value.trim(),
    building: form.elements.building.value.trim(),
    floor: Number(form.elements.floor.value),
    room_number: form.elements.room_number.value.trim(),
    category: form.elements.category.value,
    lab_administrator: form.elements.lab_administrator.value.trim(),
    operational_status: form.elements.operational_status.value
  };
  try {
    await labApiRequest(
      editingLaboratoryId
        ? `${LABORATORIES_API}/${encodeURIComponent(editingLaboratoryId)}`
        : LABORATORIES_API,
      { method: editingLaboratoryId ? 'PUT' : 'POST', body: JSON.stringify(payload) }
    );
    resetLaboratoryForm();
    await loadInventory();
  } catch (error) {
    setLabMessage(error.message, true);
  }
}

async function deleteLaboratory(laboratory) {
  const count = computers.filter(computer => computer.lab_id === laboratory.id).length;
  const detail = count ? ` This also deletes its ${count} computer${count === 1 ? '' : 's'}.` : '';
  if (!window.confirm(`Delete ${laboratory.name}?${detail}`)) return;
  try {
    await labApiRequest(`${LABORATORIES_API}/${encodeURIComponent(laboratory.id)}`, { method: 'DELETE' });
    if (editingLaboratoryId === laboratory.id) resetLaboratoryForm();
    const editingComputer = computers.find(computer => computer.id === editingComputerId);
    if (editingComputer && editingComputer.lab_id === laboratory.id) resetComputerForm();
    await loadInventory();
  } catch (error) {
    setLabMessage(error.message, true);
  }
}

function resetComputerForm() {
  editingComputerId = null;
  document.getElementById('computer-form').reset();
  document.getElementById('computer-form-title').textContent = 'Add Computer';
  document.getElementById('save-computer').textContent = 'Add Computer';
  document.getElementById('cancel-computer-edit').hidden = true;
  populateLaboratorySelect();
}

function editComputer(computer) {
  editingComputerId = computer.id;
  const form = document.getElementById('computer-form');
  populateLaboratorySelect(computer.lab_id);
  form.elements.lab_id.value = computer.lab_id;
  form.elements.computer_number.value = computer.computer_number;
  form.elements.specifications.value = computer.specifications;
  form.elements.operational_status.value = computer.status;
  document.getElementById('computer-form-title').textContent = `Edit Computer (${computer.id})`;
  document.getElementById('save-computer').textContent = 'Update Computer';
  document.getElementById('cancel-computer-edit').hidden = false;
  document.getElementById('computer-number').focus();
}

async function saveComputer(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const payload = {
    lab_id: form.elements.lab_id.value,
    computer_number: Number(form.elements.computer_number.value),
    specifications: form.elements.specifications.value.trim(),
    operational_status: form.elements.operational_status.value
  };
  try {
    await labApiRequest(
      editingComputerId
        ? `${COMPUTERS_API}/${encodeURIComponent(editingComputerId)}`
        : COMPUTERS_API,
      { method: editingComputerId ? 'PUT' : 'POST', body: JSON.stringify(payload) }
    );
    resetComputerForm();
    await loadInventory();
  } catch (error) {
    setLabMessage(error.message, true);
  }
}

async function updateComputerStatus(computer, status) {
  try {
    await labApiRequest(`${COMPUTERS_API}/${encodeURIComponent(computer.id)}`, {
      method: 'PUT',
      body: JSON.stringify({ operational_status: status })
    });
    await loadInventory();
  } catch (error) {
    setLabMessage(error.message, true);
  }
}

async function deleteComputer(computer) {
  if (!window.confirm(`Delete computer ${computer.id}?`)) return;
  try {
    await labApiRequest(`${COMPUTERS_API}/${encodeURIComponent(computer.id)}`, { method: 'DELETE' });
    if (editingComputerId === computer.id) resetComputerForm();
    await loadInventory();
  } catch (error) {
    setLabMessage(error.message, true);
  }
}

document.addEventListener('DOMContentLoaded', async () => {
  if (!sessionStorage.getItem('authToken')) {
    window.location.href = 'login.html?returnTo=admin-labs.html';
    return;
  }
  document.getElementById('lab-form').addEventListener('submit', saveLaboratory);
  document.getElementById('cancel-lab-edit').addEventListener('click', resetLaboratoryForm);
  document.getElementById('computer-form').addEventListener('submit', saveComputer);
  document.getElementById('cancel-computer-edit').addEventListener('click', resetComputerForm);
  await loadInventory();
});
