// backend/controllers/adminController.js

module.exports = {
  // Simple health check for admin interface
  getStatus: (req, res) => {
    res.json({
      interface: 'admin',
      status: 'ok',
      message: 'Admin interface is reachable.'
    });
  }
};