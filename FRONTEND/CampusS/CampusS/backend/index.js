// backend/index.js

const express = require('express');
const path = require('path');
const config = require('./config/config');
const apiRouter = require('./routes/api');

const app = express();

// Middleware
app.use(express.json()); // parse JSON bodies
app.use(express.urlencoded({ extended: true }));

// Serve static frontend files
app.use(express.static(path.join(__dirname, '..', 'frontend')));

// API routes
app.use('/api', apiRouter);

// Fallback for SPA or unknown routes – serve index.html
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'frontend', 'index.html')));
});

const PORT = config.port || 3000;
app.listen(PORT, () => {
  console.log(`Server listening on http://localhost:${PORT}`);
});
