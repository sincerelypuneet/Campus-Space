// backend/controllers/studentController.js

module.exports = {
  // Simple health check for student interface
  getStatus: (req, res) => {
    res.json({
      interface: 'student',
      status: 'ok',
      message: 'Student interface is reachable.'
    });
  }
};