document.addEventListener('DOMContentLoaded', () => {
    const form = document.getElementById('login-form');
    const errorMsg = document.getElementById('error-msg');

    if (!form || !errorMsg) return;

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        errorMsg.style.display = 'none';
        const username = document.getElementById('username').value.trim();
        const password = document.getElementById('password').value;
        if (!username || !password) {
            showError('Enter your username and password to continue.');
            return;
        }

// Demo bypass: accept any credentials and redirect to admin dashboard
        // Store a dummy token so other pages can assume an authenticated session
        const dummyToken = 'demo-token';
        sessionStorage.setItem('authToken', dummyToken);
        sessionStorage.setItem('userRole', 'admin');
        // Always go to the admin dashboard after login
        window.location.href = 'admin-dashboard.html';
    });

    function showError(msg) {
        errorMsg.textContent = msg;
        errorMsg.style.display = 'block';
    }
});
