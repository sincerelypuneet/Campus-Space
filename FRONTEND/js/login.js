// Demo login: accepts any non-empty username and password.

document.addEventListener('DOMContentLoaded', () => {
    const form = document.getElementById('login-form');
    const errorMsg = document.getElementById('error-msg');

    if (!form || !errorMsg) return;

    form.addEventListener('submit', (e) => {
        e.preventDefault();
        errorMsg.style.display = 'none';
        const username = document.getElementById('username').value.trim();
        const password = document.getElementById('password').value;
        if (!username || !password) {
            showError('Enter any username and password to continue.');
            return;
        }
        window.location.href = 'admin-dashboard.html';
    });

    function showError(msg) {
        errorMsg.textContent = msg;
        errorMsg.style.display = 'block';
    }
});
