// backend/config/config.js

const path = require('path');

module.exports = {
  port: process.env.PORT || 3000,
  // Base directory for JSON storage (relative to project root)
  dataDir: path.join(__dirname, '..', '..', 'data')
};