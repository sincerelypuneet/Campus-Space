document.addEventListener('DOMContentLoaded', () => {
  if (sessionStorage.getItem('studentAuthenticated') !== 'true') {
    window.location.replace('student-login.html');
    return;
  }

  const name = sessionStorage.getItem('studentName');
  if (name) {
    document.querySelector('#student-root .card-title').textContent = `Welcome, ${name}`;
  }
});