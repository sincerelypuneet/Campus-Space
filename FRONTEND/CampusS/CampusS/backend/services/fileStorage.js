// backend/services/fileStorage.js

/**
 * JSON‑file based storage service for CampusSpace.
 * Uses fs/promises for async operations, atomic writes, backups,
 * validation hooks and a simple in‑process lock to avoid concurrent writes.
 */

const fs = require('fs').promises;
const path = require('path');
const crypto = require('crypto');

// Root folder that holds all JSON data files
const DATA_ROOT = path.join(__dirname, '..', 'data');

// ------------------------------------------------------------
// Helper utilities
// ------------------------------------------------------------
/** Ensure a file exists; if not, create it with the supplied default content. */
async function ensureFile(filePath, defaultContent) {
  try {
    await fs.access(filePath);
  } catch (_) {
    // File missing – write default (pretty‑printed) JSON
    await fs.writeFile(filePath, JSON.stringify(defaultContent, null, 2), 'utf8');
  }
}

/** Write data atomically: write to a temporary file then rename. */
async function atomicWrite(filePath, data) {
  const tmpPath = `${filePath}.tmp`;
  await fs.writeFile(tmpPath, JSON.stringify(data, null, 2), 'utf8');
  await fs.rename(tmpPath, filePath);
}

/** Create a timestamped backup before a destructive write. */
async function backup(filePath) {
  try {
    const ts = new Date().toISOString().replace(/[:.]/g, '-');
    const backupPath = `${filePath}.bak.${ts}`;
    await fs.copyFile(filePath, backupPath);
  } catch (e) {
    // If source doesn't exist yet we just skip backup – the caller
    // will have just created the file via ensureFile.
  }
}

/** Simple in‑process mutex to serialize writes per file. */
const writeLocks = new Map();
async function withLock(filePath, fn) {
  while (writeLocks.get(filePath)) {
    // tiny sleep loop – adequate for our single‑process environment
    await new Promise(r => setTimeout(r, 5));
  }
  writeLocks.set(filePath, true);
  try {
    return await fn();
  } finally {
    writeLocks.delete(filePath);
  }
}

/** Read JSON from a file, handling missing or corrupted data safely. */
async function read(fileName) {
  const fp = path.join(DATA_ROOT, fileName);
  // Determine default based on extension – arrays for *.json, objects for settings.json
  const isArrayFile = fileName !== 'settings.json';
  const defaultContent = isArrayFile ? [] : {};
  await ensureFile(fp, defaultContent);
  const raw = await fs.readFile(fp, 'utf8');
  try {
    return JSON.parse(raw);
  } catch (e) {
    // Corrupted JSON – rename the bad file, recreate fresh, and return default
    const corruptPath = `${fp}.corrupt.${Date.now()}`;
    await fs.rename(fp, corruptPath);
    await atomicWrite(fp, defaultContent);
    return defaultContent;
  }
}

/** Write JSON to a file with backup, atomic replace and write lock. */
async function write(fileName, data) {
  const fp = path.join(DATA_ROOT, fileName);
  return withLock(fp, async () => {
    await backup(fp);
    await atomicWrite(fp, data);
  });
}

/** Generate a short, unique identifier (12‑hex characters). */
function generateId() {
  return crypto.randomBytes(6).toString('hex');
}

/** Add a new record to an array‑based JSON file. */
async function add(fileName, record, validate) {
  const data = await read(fileName);
  if (!Array.isArray(data)) {
    throw new Error(`add() can only be used on array files (${fileName})`);
  }
  record.id = generateId();
  if (validate) await validate(record);
  data.push(record);
  await write(fileName, data);
  return record;
}

/** Update an existing record identified by its id. */
async function update(fileName, id, patch, validate) {
  const data = await read(fileName);
  const idx = data.findIndex(r => r.id === id);
  if (idx === -1) throw new Error(`${fileName}: record with id ${id} not found`);
  const updated = { ...data[idx], ...patch };
  if (validate) await validate(updated);
  data[idx] = updated;
  await write(fileName, data);
  return updated;
}

/** Delete a record by id. */
async function remove(fileName, id) {
  const data = await read(fileName);
  const filtered = data.filter(r => r.id !== id);
  if (filtered.length === data.length) {
    throw new Error(`${fileName}: record with id ${id} not found`);
  }
  await write(fileName, filtered);
  return true;
}

/** Find a single record by id (null if not found). */
async function findById(fileName, id) {
  const data = await read(fileName);
  return data.find(r => r.id === id) || null;
}

/** Simple search: field/value partial match, case‑insensitive. */
async function search(fileName, query) {
  const data = await read(fileName);
  const entries = Object.entries(query);
  return data.filter(rec =>
    entries.every(([k, v]) =>
      String(rec[k] ?? '').toLowerCase().includes(String(v).toLowerCase())
    )
  );
}

module.exports = {
  read,
  write,
  add,
  update,
  remove,
  findById,
  search,
  generateId,
};
