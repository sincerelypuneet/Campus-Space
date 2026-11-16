const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const fs = require('fs');
const path = require('path');
const { v4: uuidv4 } = require('uuid');

const app = express();
const PORT = process.env.PORT || 8080;

app.use(cors());
app.use(bodyParser.json());

// Helper to load and save JSON data files
function dataFile(name) {
  return path.join(__dirname, 'data', `${name}.json`);
}

function loadData(name) {
  const filePath = dataFile(name);
  try {
    const raw = fs.readFileSync(filePath, 'utf8');
    return JSON.parse(raw);
  } catch (e) {
    console.error(`Failed to load ${name}:`, e);
    return [];
  }
}

function saveData(name, data) {
  const filePath = dataFile(name);
  fs.writeFileSync(filePath, JSON.stringify(data, null, 2), 'utf8');
}

// ---------- Authentication (demo) ----------
app.post('/api/login', (req, res) => {
  // Demo: accept any username/password
  const token = uuidv4();
  res.json({ token, role: 'admin' });
});

// ---------- Teachers ----------
app.get('/api/teachers', (req, res) => {
  const teachers = loadData('teachers');
  res.json(teachers);
});

// ---------- Classrooms ----------
app.get('/api/classrooms', (req, res) => {
  const rooms = loadData('classrooms');
  res.json(rooms);
});

// ---------- Laboratories ----------
app.get('/api/laboratories', (req, res) => {
  const labs = loadData('laboratories');
  res.json(labs);
});

// ---------- Computers ----------
app.get('/api/computers', (req, res) => {
  const comps = loadData('computers');
  res.json(comps);
});

// ---------- Timetables ----------
app.get('/api/timetables', (req, res) => {
  const tt = loadData('timetables');
  res.json(tt);
});

// Validation helpers
function timeToMinutes(t) {
  const [h, m] = t.split(':').map(Number);
  return h * 60 + m;
}

function overlaps(aStart, aEnd, bStart, bEnd) {
  return aStart < bEnd && bStart < aEnd;
}

function validateTimetableEntry(entry, existing, ignoreId = null) {
  const { teacherId, dayOfWeek, startTime, endTime, building, floor, roomNumber } = entry;
  const s = timeToMinutes(startTime);
  const e = timeToMinutes(endTime);
  for (const eItem of existing) {
    if (ignoreId && eItem.id === ignoreId) continue;
    if (eItem.dayOfWeek !== dayOfWeek) continue;
    const s2 = timeToMinutes(eItem.startTime);
    const e2 = timeToMinutes(eItem.endTime);
    // Teacher overlap
    if (eItem.teacherId === teacherId && overlaps(s, e, s2, e2)) {
      return { ok: false, message: 'Teacher has overlapping class.' };
    }
    // Room overlap
    if (
      eItem.building === building &&
      eItem.floor === floor &&
      eItem.roomNumber === roomNumber &&
      overlaps(s, e, s2, e2)
    ) {
      return { ok: false, message: 'Room is already booked for this time.' };
    }
  }
  return { ok: true };
}

// Create new timetable entry
app.post('/api/timetables', (req, res) => {
  const newEntry = req.body;
  const tt = loadData('timetables');
  const validation = validateTimetableEntry(newEntry, tt);
  if (!validation.ok) {
    return res.status(400).json({ error: validation.message });
  }
  newEntry.id = uuidv4();
  tt.push(newEntry);
  saveData('timetables', tt);
  res.status(201).json(newEntry);
});

// Update existing entry
app.put('/api/timetables/:id', (req, res) => {
  const { id } = req.params;
  const updated = req.body;
  const tt = loadData('timetables');
  const index = tt.findIndex(e => e.id === id);
  if (index === -1) return res.status(404).json({ error: 'Not found' });
  const validation = validateTimetableEntry(updated, tt, id);
  if (!validation.ok) {
    return res.status(400).json({ error: validation.message });
  }
  updated.id = id;
  tt[index] = updated;
  saveData('timetables', tt);
  res.json(updated);
});

// Delete entry
app.delete('/api/timetables/:id', (req, res) => {
  const { id } = req.params;
  let tt = loadData('timetables');
  const before = tt.length;
  tt = tt.filter(e => e.id !== id);
  if (tt.length === before) return res.status(404).json({ error: 'Not found' });
  saveData('timetables', tt);
  res.status(204).send();
});

// ---------- Start server ----------
app.listen(PORT, () => {
  console.log(`CampusSpace backend listening on port ${PORT}`);
});
