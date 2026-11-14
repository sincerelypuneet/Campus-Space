const LPC_LABORATORIES_API = '/api/student/lpc/laboratories';
const LPC_COMPUTERS_API = '/api/student/lpc/computers';
const LPC_BOOKINGS_API = '/api/student/lpc/bookings';

let lpcAvailabilityQuery = null;
let lpcAvailabilityRequest = 0;

function setLpcMessage(message, isError = false, isSuccess = false) {
  const element = document.getElementById('lpc-booking-message');
  element.textContent = message;
  element.classList.toggle('error', isError);
  element.classList.toggle('success', isSuccess);
}

async function lpcApiRequest(url, options = {}) {
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
    const error = new Error('The server returned an invalid response.');
    error.status = response.status;
    throw error;
  }
  if (!response.ok) {
    const error = new Error(data.message || 'LPC booking request failed.');
    error.status = response.status;
    throw error;
  }
  return data;
}

function clearLpcAvailability(message = '') {
  lpcAvailabilityQuery = null;
  const computer = document.getElementById('lpc-computer');
  computer.replaceChildren(new Option('Check availability to list computers', ''));
  computer.disabled = true;
  document.getElementById('lpc-confirm-booking').disabled = true;
  if (message) setLpcMessage(message);
}

function currentLpcQuery() {
  const form = document.getElementById('lpc-booking-form');
  const values = {
    lab_id: form.elements.lab_id.value,
    date: form.elements.date.value,
    start: form.elements.start.value,
    end: form.elements.end.value
  };
  if (!values.lab_id || !values.date || !values.start || !values.end) {
    throw new Error('Select a laboratory, date, start time, and end time.');
  }
  if (values.start >= values.end) {
    throw new Error('End time must be after start time.');
  }
  return values;
}

async function findAvailableLpcComputers(event) {
  event.preventDefault();
  const requestId = ++lpcAvailabilityRequest;
  const computerSelect = document.getElementById('lpc-computer');
  const confirmButton = document.getElementById('lpc-confirm-booking');
  computerSelect.replaceChildren(new Option('Checking availability…', ''));
  computerSelect.disabled = true;
  confirmButton.disabled = true;
  try {
    const query = currentLpcQuery();
    const params = new URLSearchParams(query);
    const data = await lpcApiRequest(`${LPC_COMPUTERS_API}?${params}`);
    if (requestId !== lpcAvailabilityRequest) return;

    lpcAvailabilityQuery = query;
    computerSelect.replaceChildren(new Option(
      data.computers.length ? 'Select an available computer' : 'No computers available for this period',
      ''
    ));
    data.computers.forEach(computer => {
      const label = `PC ${computer.computer_number}${computer.specifications ? ` — ${computer.specifications}` : ''}`;
      computerSelect.add(new Option(label, computer.id));
    });
    computerSelect.disabled = data.computers.length === 0;
    confirmButton.disabled = data.computers.length === 0;
    setLpcMessage(
      `${data.computers.length} available computer${data.computers.length === 1 ? '' : 's'} found.`
    );
  } catch (error) {
    if (requestId !== lpcAvailabilityRequest) return;
    lpcAvailabilityQuery = null;
    computerSelect.replaceChildren(new Option('Availability could not be checked', ''));
    setLpcMessage(error.message, true);
  }
}

async function confirmLpcBooking() {
  const form = document.getElementById('lpc-booking-form');
  if (!form.reportValidity()) return;
  const computerId = form.elements.computer_id.value;
  if (!lpcAvailabilityQuery || !computerId) {
    setLpcMessage('Check availability and select a computer before booking.', true);
    return;
  }

  const currentQuery = currentLpcQuery();
  if (Object.keys(currentQuery).some(key => currentQuery[key] !== lpcAvailabilityQuery[key])) {
    clearLpcAvailability('The booking details changed. Check availability again.');
    return;
  }

  const button = document.getElementById('lpc-confirm-booking');
  button.disabled = true;
  try {
    const booking = await lpcApiRequest(LPC_BOOKINGS_API, {
      method: 'POST',
      body: JSON.stringify({
        ...currentQuery,
        computer_id: computerId,
        student_name: form.elements.student_name.value.trim(),
        purpose: form.elements.purpose.value.trim()
      })
    });
    lpcAvailabilityQuery = null;
    const selectedComputer = form.elements.computer_id.selectedOptions[0].textContent;
    setLpcMessage(
      `${booking.message}: ${booking.laboratory}, ${selectedComputer}, ${booking.booking.date} ${booking.booking.start}–${booking.booking.end}. Reference: ${booking.booking.id}`,
      false,
      true
    );
    const computerSelect = form.elements.computer_id;
    computerSelect.replaceChildren(new Option('Check availability to list computers', ''));
    computerSelect.disabled = true;
  } catch (error) {
    if (error.status === 404 || error.status === 409) {
      clearLpcAvailability();
    } else {
      button.disabled = false;
    }
    setLpcMessage(error.message, true);
  }
}

async function loadLpcLaboratories() {
  const select = document.getElementById('lpc-laboratory');
  try {
    const data = await lpcApiRequest(LPC_LABORATORIES_API);
    select.replaceChildren(new Option(
      data.laboratories.length ? 'Select an LPC laboratory' : 'No operational LPC laboratories',
      ''
    ));
    data.laboratories.forEach(laboratory => {
      const location = [laboratory.building, laboratory.room_number]
        .filter(Boolean)
        .join(' · ');
      select.add(new Option(`${laboratory.name}${location ? ` (${location})` : ''}`, laboratory.id));
    });
    select.disabled = data.laboratories.length === 0;
    if (!data.laboratories.length) {
      setLpcMessage('No operational LPC laboratories are configured. Please contact the administrator.');
    }
  } catch (error) {
    select.replaceChildren(new Option('Unable to load laboratories', ''));
    select.disabled = true;
    setLpcMessage(error.message, true);
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

  const form = document.getElementById('lpc-booking-form');
  const date = form.elements.date;
  const now = new Date();
  date.min = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
  form.elements.student_name.value = sessionStorage.getItem('studentName') || '';
  form.addEventListener('submit', findAvailableLpcComputers);
  ['lab_id', 'date', 'start', 'end'].forEach(name => {
    form.elements[name].addEventListener('change', () => {
      lpcAvailabilityRequest++;
      clearLpcAvailability('Booking details changed. Check availability again.');
    });
  });
  form.elements.computer_id.addEventListener('change', event => {
    document.getElementById('lpc-confirm-booking').disabled = !event.target.value || !lpcAvailabilityQuery;
  });
  document.getElementById('lpc-confirm-booking').addEventListener('click', confirmLpcBooking);
  await loadLpcLaboratories();
});
