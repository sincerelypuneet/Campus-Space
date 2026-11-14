// nav.js – builds reusable navigation sidebars for Student and Admin interfaces
// The script is loaded after app.js so we can reuse the navigateTo helper.

// Define menu items for each role
const MENU_CONFIG = {
    admin: [
        { label: 'Dashboard', icon: '🏠', href: 'admin.html' },
        { label: 'Teachers', icon: '👩‍🏫', href: 'admin-teachers.html' },
        { label: 'Timetables', icon: '📅', href: 'admin-timetable.html' },
        { label: 'Classrooms', icon: '🏫', href: 'admin-classrooms.html' },
        { label: 'Laboratories', icon: '🧪', href: 'admin-labs.html' },
        { label: 'Bookings', icon: '🗂️', href: 'admin-bookings.html' },
        { label: 'Settings', icon: '⚙️', href: 'admin-settings.html' },
        { label: 'Logout', icon: '🚪', href: 'login.html', onClick: () => {
            sessionStorage.removeItem('authToken');
            sessionStorage.removeItem('userRole');
            window.location.href = 'login.html';
        } }
    ],
    student: [
        { label: 'Dashboard', icon: '🏠', href: 'student-dashboard.html' },
        { label: 'Find Teacher', icon: '🧑‍🏫', href: 'student-teacher.html' },
        { label: 'Find Classroom', icon: '🏫', href: 'student-classroom.html' },
        { label: 'Computer Labs', icon: '💻', href: 'student-labs.html' },
        { label: 'HPC Requests', icon: '🖥️', href: 'student-hpc.html' },
        { label: 'My Bookings', icon: '📑', href: 'student-bookings.html' },
        { label: 'Logout', icon: '🚪', href: 'student-login.html', onClick: () => {
            sessionStorage.removeItem('studentAuthenticated');
            sessionStorage.removeItem('studentName');
            window.location.href = 'student-login.html';
        } }
    ]
};

/**
 * Render the sidebar menu for a given role.
 * @param {string} role – "admin" or "student"
 */
function renderSidebar(role) {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;
    const menu = MENU_CONFIG[role] || [];
    const ul = document.createElement('ul');
    menu.forEach(item => {
        const li = document.createElement('li');
        const a = document.createElement('a');
        a.href = item.href;
        a.innerHTML = `${item.icon} ${item.label}`;
        if (item.onClick) {
            a.addEventListener('click', e => {
                e.preventDefault();
                item.onClick();
            });
        }
        // Mark the active page based on URL
        if (window.location.pathname.endsWith(item.href)) {
            a.classList.add('active');
        }
        li.appendChild(a);
        ul.appendChild(li);
    });
    sidebar.innerHTML = '';
    sidebar.appendChild(ul);
}

/**
 * Toggle sidebar visibility on mobile.
 */
function toggleSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;
    sidebar.classList.toggle('collapsed');
}

// Auto‑initialize when DOM is ready – role is specified via data-role attribute on <body>
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        const role = document.body.getAttribute('data-role');
        renderSidebar(role);
    });
} else {
    const role = document.body.getAttribute('data-role');
    renderSidebar(role);
}
