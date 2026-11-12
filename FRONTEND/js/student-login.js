document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('student-login-form');
  const error = document.getElementById('student-login-error');

  form.addEventListener('submit', event => {
    event.preventDefault();
    const username = form.elements.username.value.trim();
    const password = form.elements.password.value;
    if (!username || !password) {
      error.textContent = 'Enter any username and password to continue.';
      return;
    }

    sessionStorage.setItem('studentAuthenticated', 'true');
    sessionStorage.setItem('studentName', username);
    window.location.href = 'student-dashboard.html';
  });
});
