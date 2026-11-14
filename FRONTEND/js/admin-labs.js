const LABORATORIES_API = '/api/laboratories';
const COMPUTERS_API = '/api/computers';
const HPC_REQUESTS_API = '/api/admin/hpc-requests';

let laboratories = [];
let computers = [];
let editingLaboratoryId = null;
let editingComputerId = null;
let reviewedHpcRequest = null;

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

function setHpcRequestMessage(message, isError = false) {
  const element = document.getElementById('hpc-request-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function setHpcAvailabilityMessage(message, isError = false) {
  const element = document.getElementById('hpc-availability-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
}

function renderHpcRequests(requests) {
  const root = document.getElementById('hpc-request-list');
  root.replaceChildren();
  if (!requests.length) {
    root.textContent = 'No HPC booking requests have been submitted.';
    return;
  }

  const labNames = new Map(laboratories.map(laboratory => [laboratory.id, laboratory.name]));
  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const header = document.createElement('tr');
  ['Student', 'Laboratory', 'Date', 'Time', 'Status', 'Actions'].forEach(label => {
    const cell = document.createElement('th');
    cell.scope = 'col';
    cell.textContent = label;
    header.appendChild(cell);
  });
  const head = document.createElement('thead');
  head.appendChild(header);
  table.appendChild(head);

  const body = document.createElement('tbody');
  requests.forEach(request => {
    const row = document.createElement('tr');
    addTableCell(row, request.student_name || request.user_id);
    addTableCell(row, labNames.get(request.lab_id) || request.lab_id);
    addTableCell(row, request.date);
    addTableCell(row, `${request.start}–${request.end}`);
    addTableCell(row, request.status);
    const actions = addTableCell(row, '');
    actions.appendChild(makeActionButton(
      request.status === 'pending' ? 'Review' : 'View details',
      () => openHpcRequest(request.id),
      request.status === 'pending' ? 'btn-primary' : 'btn-secondary'
    ));
    body.appendChild(row);
  });
  table.appendChild(body);
  root.appendChild(table);
}

async function loadHpcRequests(successMessage = '') {
  try {
    const data = await labApiRequest(HPC_REQUESTS_API);
    renderHpcRequests(data.requests || []);
    setHpcRequestMessage(successMessage || `${(data.requests || []).length} HPC request(s) loaded.`);
  } catch (error) {
    setHpcRequestMessage(error.message, true);
    document.getElementById('hpc-request-list').replaceChildren();
  }
}

function addHpcDetail(label, value) {
  const details = document.getElementById('hpc-request-details');
  const term = document.createElement('dt');
  term.textContent = label;
  const description = document.createElement('dd');
  description.textContent = value || '—';
  details.append(term, description);
}

async function openHpcRequest(requestId) {
  const panel = document.getElementById('hpc-review-panel');
  const computerSection = document.getElementById('hpc-computer-selection');
  const rejectionSection = document.getElementById('hpc-rejection-field');
  const computerSelect = document.getElementById('hpc-computer');
  const approveButton = document.getElementById('hpc-approve');
  const rejectButton = document.getElementById('hpc-reject');
  const completeButton = document.getElementById('hpc-complete');
  reviewedHpcRequest = null;
  approveButton.disabled = true;
  rejectButton.disabled = true;
  completeButton.hidden = true;
  computerSelect.replaceChildren(new Option('Checking available computers…', ''));
  setHpcAvailabilityMessage('Loading request details…');
  panel.hidden = false;
  panel.scrollIntoView({ behavior: 'smooth', block: 'start' });

  try {
    const request = await labApiRequest(`${HPC_REQUESTS_API}/${encodeURIComponent(requestId)}`);
    reviewedHpcRequest = request;
    const lab = laboratories.find(item => item.id === request.lab_id);
    document.getElementById('hpc-request-details').replaceChildren();
    addHpcDetail('Request ID', request.id);
    addHpcDetail('Student', request.student_name || request.user_id);
    addHpcDetail('Laboratory', lab ? `${lab.name} (${lab.id})` : request.lab_id);
    addHpcDetail('Date', request.date);
    addHpcDetail('Time', `${request.start}–${request.end}`);
    addHpcDetail('Purpose', request.purpose);
    addHpcDetail('Status', request.status);
    if (request.computer_id) addHpcDetail('Assigned computer', request.computer_id);
    if (request.approved_by) addHpcDetail('Approved by', request.approved_by);
    if (request.approval_timestamp) addHpcDetail('Approved at', request.approval_timestamp);
    if (request.rejection_reason) addHpcDetail('Rejection reason', request.rejection_reason);

    const pending = request.status === 'pending';
    computerSection.hidden = !pending;
    rejectionSection.hidden = !pending;
    document.getElementById('hpc-rejection-reason').value = '';
    completeButton.hidden = request.status !== 'approved';
    completeButton.disabled = request.status !== 'approved';
    if (!pending) {
      computerSelect.replaceChildren(new Option('No assignment needed', ''));
      setHpcAvailabilityMessage(
        request.status === 'approved' ? 'This approved booking can be marked completed.' : `Request status: ${request.status}.`
      );
      return;
    }

    const availability = await labApiRequest(
      `${HPC_REQUESTS_API}/${encodeURIComponent(requestId)}/availability`
    );
    computerSelect.replaceChildren(new Option(
      availability.computers.length ? 'Select an available computer' : 'No computers available for this period',
      ''
    ));
    availability.computers.forEach(computer => {
      computerSelect.add(new Option(`PC ${computer.computer_number} — ${computer.specifications}`, computer.id));
    });
    computerSelect.disabled = availability.computers.length === 0;
    rejectButton.disabled = false;
    setHpcAvailabilityMessage(
      availability.computers.length
        ? `${availability.computers.length} available computer(s). Availability will be checked again when approving.`
        : 'No operational computers are available for this period.'
    );
  } catch (error) {
    computerSelect.replaceChildren(new Option('Unable to check request', ''));
    setHpcAvailabilityMessage(error.message, true);
  }
}

async function updateHpcRequest(action) {
  if (!reviewedHpcRequest) return;
  const payload = { action };
  if (action === 'approve') {
    payload.computer_id = document.getElementById('hpc-computer').value;
    if (!payload.computer_id) {
      setHpcAvailabilityMessage('Select an available computer before approving.', true);
      return;
    }
  }
  if (action === 'reject') {
    payload.rejection_reason = document.getElementById('hpc-rejection-reason').value.trim();
  }

  document.getElementById('hpc-approve').disabled = true;
  document.getElementById('hpc-reject').disabled = true;
  document.getElementById('hpc-complete').disabled = true;
  try {
    const result = await labApiRequest(
      `${HPC_REQUESTS_API}/${encodeURIComponent(reviewedHpcRequest.id)}`,
      { method: 'PUT', body: JSON.stringify(payload) }
    );
    reviewedHpcRequest = result.request;
    document.getElementById('hpc-review-panel').hidden = true;
    await loadHpcRequests(result.message);
  } catch (error) {
    const requestId = reviewedHpcRequest.id;
    await loadHpcRequests();
    await openHpcRequest(requestId);
    setHpcAvailabilityMessage(error.message, true);
  }
}

document.addEventListener('DOMContentLoaded', async () => {
  if (!sessionStorage.getItem('authToken')) {
    window.location.href = 'login.html?returnTo=admin-labs.html';
    return;
  }
  if (window.matchMedia('(max-width: 768px)').matches) {
    document.getElementById('sidebar').classList.add('collapsed');
  }
  document.getElementById('lab-form').addEventListener('submit', saveLaboratory);
  document.getElementById('cancel-lab-edit').addEventListener('click', resetLaboratoryForm);
  document.getElementById('computer-form').addEventListener('submit', saveComputer);
  document.getElementById('cancel-computer-edit').addEventListener('click', resetComputerForm);
  document.getElementById('hpc-approve').addEventListener('click', () => updateHpcRequest('approve'));
  document.getElementById('hpc-reject').addEventListener('click', () => updateHpcRequest('reject'));
  document.getElementById('hpc-complete').addEventListener('click', () => updateHpcRequest('complete'));
  document.getElementById('hpc-review-close').addEventListener('click', () => {
    document.getElementById('hpc-review-panel').hidden = true;
  });
  document.getElementById('hpc-computer').addEventListener('change', event => {
    document.getElementById('hpc-approve').disabled = !event.target.value;
  });
  await Promise.all([loadInventory(), loadHpcRequests()]);
});
