// Placeholder app.js – router and health check

// Simple navigation helper used by the welcome cards
function navigateTo(page) {
    window.location.href = page;
}

// Health check – called on every page load
function healthCheck() {
    // Simple ping to verify server is alive; ignore any network errors silently.
    fetch('/api/ping')
        .then(r => {
            if (!r.ok) throw new Error('Server not reachable');
            return r.json();
        })
        .then(data => {
            console.log('Server health:', data);
        })
        .catch(err => {
            console.warn('Health check failed (ignored):', err);
        });
}
    fetch('/api/ping')
        .then(r => r.json())
        .then(data => {
            console.log('Server health:', data);
            // Show a tiny toast if desired
            const toast = document.createElement('div');
            toast.textContent = data.message || 'Server reachable';
            toast.style.position = 'fixed';
            toast.style.bottom = '1rem';
            toast.style.right = '1rem';
            toast.style.background = 'rgba(0,0,0,0.7)';
            toast.style.color = '#fff';
            toast.style.padding = '0.5rem 1rem';
            toast.style.borderRadius = '4px';
            document.body.appendChild(toast);
            setTimeout(() => toast.remove(), 3000);
        })
        .catch(err => {
            console.error('Health check failed', err);
        });
}

// Run health check after DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', healthCheck);
} else {
    healthCheck();
}

