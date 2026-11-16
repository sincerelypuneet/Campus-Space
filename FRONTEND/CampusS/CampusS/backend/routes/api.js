// backend/routes/api.js

const express = require('express');
const router = express.Router();

// Controllers (placeholders for now)
const adminController = require('../controllers/adminController');
const studentController = require('../controllers/studentController');

// Admin routes
router.get('/admin', adminController.getStatus);
// Student routes
router.get('/student', studentController.getStatus);

module.exports = router;
