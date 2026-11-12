// auth.js – front‑end authentication utilities

/** Retrieve stored token */
export function getToken() {
    return sessionStorage.getItem('authToken');
}

/** Retrieve stored role */
export function getRole() {
    return sessionStorage.getItem('userRole');
}

/** Check if user is authenticated */
export function isAuthenticated() {
    return !!getToken();
}

/** Logout the user – call backend and clear storage */
export async function logout() {
    const token = getToken();
    if (token) {
        try {
            await fetch('/api/logout', {
                method: 'POST',
                headers: {
                    'Authorization': 'Bearer ' + token
                }
            });
        } catch (e) {
            console.error('Logout request failed', e);
        }
    }
    sessionStorage.removeItem('authToken');
    sessionStorage.removeItem('userRole');
    // redirect to login page
    window.location.href = 'login.html';
}

/** Helper to attach token to fetch requests */
export function authHeaders() {
    const token = getToken();
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

