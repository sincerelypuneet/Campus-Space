const STUDENT_HPC_LABS_API = '/api/student/hpc/laboratories';
const STUDENT_HPC_REQUESTS_API = '/api/student/hpc-requests';

let hpcLaboratories = [];
let hpcKnownStatuses = new Map();
let hpcFirstStatusLoad = true;
let hpcRequestInProgress = false;

function setHpcFormMessage(message, isError = false, isSuccess = false) {
  const element = document.getElementById('hpc-form-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
  element.classList.toggle('success', isSuccess);
}

async function studentHpcApiRequest(url, options = {}) {
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
  if (!response.ok) throw new Error(data.message || 'HPC request failed.');
  return data;
}

function renderStudentHpcRequests(requests) {
  const root = document.getElementById('hpc-my-requests');
  root.replaceChildren();
  if (!requests.length) {
    root.textContent = 'You have not submitted any HPC requests.';
    return;
  }

  const labNames = new Map(hpcLaboratories.map(lab => [lab.id, lab.name]));
  const table = document.createElement('table');
  table.className = 'table timetable-table';
  const header = document.createElement('tr');
  ['Laboratory', 'Date', 'Time', 'Purpose', 'Status', 'Assigned computer', 'Details', 'Actions'].forEach(label => {
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
    const values = [
      labNames.get(request.lab_id) || request.lab_id,
      request.date,
      `${request.start}–${request.end}`,
      request.purpose,
      request.status,
      request.computer_id || '',
      request.rejection_reason || (request.approved_by
        ? `Approved by ${request.approved_by}${request.approval_timestamp ? ` at ${request.approval_timestamp}` : ''}`
        : '')
    ];
    values.forEach(value => {
      const cell = document.createElement('td');
      cell.textContent = value || '—';
      row.appendChild(cell);
    });

    const actions = document.createElement('td');
    if (request.status === 'pending' || request.status === 'approved') {
      const cancel = document.createElement('button');
      cancel.type = 'button';
      cancel.className = 'btn-secondary';
      cancel.textContent = 'Cancel';
      cancel.addEventListener('click', () => cancelStudentHpcRequest(request));
      actions.appendChild(cancel);
    } else {
      actions.textContent = '—';
    }
    row.appendChild(actions);
    body.appendChild(row);
  });
  table.appendChild(body);
  root.appendChild(table);
}

async function loadStudentHpcRequests() {
  const name = sessionStorage.getItem('studentName') || '';
  if (!name.trim()) {
    document.getElementById('hpc-my-requests').textContent = 'Your student name is not available. Please sign in again.';
    return;
  }
  try {
    const params = new URLSearchParams({ student_name: name });
    const data = await studentHpcApiRequest(`${STUDENT_HPC_REQUESTS_API}?${params}`);
    const requests = data.requests || [];
    if (!hpcFirstStatusLoad) {
      const changed = requests.find(request => {
        return hpcKnownStatuses.has(request.id) && hpcKnownStatuses.get(request.id) !== request.status;
      });
      if (changed) {
        const label = changed.status.charAt(0).toUpperCase() + changed.status.slice(1);
        document.getElementById('hpc-notification').textContent =
          `Your HPC request for ${changed.date} is now ${label.toLowerCase()}.` +
          (changed.computer_id ? ` Assigned computer: ${changed.computer_id}.` : '') +
          (changed.rejection_reason ? ` Reason: ${changed.rejection_reason}` : '');
      }
    }
    hpcKnownStatuses = new Map(requests.map(request => [request.id, request.status]));
    hpcFirstStatusLoad = false;
    renderStudentHpcRequests(requests);
  } catch (error) {
    document.getElementById('hpc-my-requests').textContent = error.message;
  }
}

async function submitStudentHpcRequest(event) {
  event.preventDefault();
  const form = event.currentTarget;
  if (!form.reportValidity() || hpcRequestInProgress) return;
  if (form.elements.start.value >= form.elements.end.value) {
    setHpcFormMessage('End time must be after start time.', true);
    return;
  }

  const submitButton = document.getElementById('hpc-submit');
  hpcRequestInProgress = true;
  submitButton.disabled = true;
  setHpcFormMessage('Submitting request…');
  try {
    const result = await studentHpcApiRequest(STUDENT_HPC_REQUESTS_API, {
      method: 'POST',
      body: JSON.stringify({
        student_name: form.elements.student_name.value.trim(),
        lab_id: form.elements.lab_id.value,
        date: form.elements.date.value,
        start: form.elements.start.value,
        end: form.elements.end.value,
        purpose: form.elements.purpose.value.trim()
      })
    });
    form.elements.date.value = '';
    form.elements.start.value = '';
    form.elements.end.value = '';
    form.elements.purpose.value = '';
    setHpcFormMessage(`${result.message}. Reference: ${result.request.id}`, false, true);
    await loadStudentHpcRequests();
  } catch (error) {
    setHpcFormMessage(error.message, true);
  } finally {
    hpcRequestInProgress = false;
    submitButton.disabled = false;
  }
}

async function cancelStudentHpcRequest(request) {
  if (!window.confirm(`Cancel your ${request.status} HPC request for ${request.date} (${request.start}–${request.end})?`)) {
    return;
  }
  try {
    await studentHpcApiRequest(`${STUDENT_HPC_REQUESTS_API}/${encodeURIComponent(request.id)}`, {
      method: 'PUT',
      body: JSON.stringify({
        action: 'cancel',
        student_name: sessionStorage.getItem('studentName') || ''
      })
    });
    document.getElementById('hpc-notification').textContent = 'Your HPC request was cancelled.';
    await loadStudentHpcRequests();
  } catch (error) {
    document.getElementById('hpc-notification').textContent = error.message;
  }
}

async function loadHpcLaboratories() {
  const select = document.getElementById('hpc-laboratory');
  try {
    const data = await studentHpcApiRequest(STUDENT_HPC_LABS_API);
    hpcLaboratories = data.laboratories || [];
    select.replaceChildren(new Option(
      hpcLaboratories.length ? 'Select an HPC laboratory' : 'No operational HPC laboratories',
      ''
    ));
    hpcLaboratories.forEach(laboratory => {
      const location = [laboratory.building, laboratory.room_number].filter(Boolean).join(' · ');
      select.add(new Option(`${laboratory.name}${location ? ` (${location})` : ''}`, laboratory.id));
    });
    select.disabled = hpcLaboratories.length === 0;
    if (!hpcLaboratories.length) {
      setHpcFormMessage('No operational HPC laboratories are configured. Please contact the administrator.');
    }
  } catch (error) {
    select.replaceChildren(new Option('Unable to load laboratories', ''));
    select.disabled = true;
    setHpcFormMessage(error.message, true);
  }
}

document.addEventListener('DOMContentLoaded', async () => {
  if (sessionStorage.getItem('studentAuthenticated') !== 'true') {
    window.location.replace('student-login.html');
    return;
  }
  if (window.matchMedia('(max-width: 768px)').matches) {
    document.getElementById('sidebar').classList.add('collapsed');
  }

  const form = document.getElementById('hpc-request-form');
  form.elements.student_name.value = sessionStorage.getItem('studentName') || '';
  form.elements.student_name.readOnly = true;
  const now = new Date();
  form.elements.date.min = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
  form.addEventListener('submit', submitStudentHpcRequest);
  await loadHpcLaboratories();
  await loadStudentHpcRequests();
  window.setInterval(loadStudentHpcRequests, 30000);
});
