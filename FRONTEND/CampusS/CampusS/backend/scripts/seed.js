// backend/scripts/seed.js

/**
 * Seed script to populate the JSON file‑based database with sample data.
 * Run with: `node backend/scripts/seed.js`
 */

const storage = require('../services/fileStorage');

// Simple validators – throw if a rule fails
function validateEmail(email) {
  const re = /^[^@\s]+@[^@\s]+\.[^@\s]+$/;
  if (!re.test(email)) throw new Error('Invalid email format');
}

async function main() {
  // ------- Departments -------
  const csDept = await storage.add('departments.json', {
    name: 'Computer Science',
    code: 'CS'
  });
  const eeDept = await storage.add('departments.json', {
    name: 'Electrical Engineering',
    code: 'EE'
  });

  // ------- Teachers & Users -------
  const teacherAlice = await storage.add('teachers.json', {
    name: 'Dr. Alice Nguyen',
    departmentId: csDept.id,
    email: 'alice.nguyen@university.edu',
    subjectsIds: []
  });
  await storage.add('users.json', {
    email: teacherAlice.email,
    passwordHash: 'hashed‑placeholder', // replace with real hash in production
    role: 'teacher',
    profileId: teacherAlice.id
  });

  // ------- Students & Users -------
  const studentBob = await storage.add('students.json', {
    name: 'Bob Patel',
    departmentId: csDept.id,
    email: 'bob.patel@university.edu',
    enrolledSubjectIds: []
  });
  await storage.add('users.json', {
    email: studentBob.email,
    passwordHash: 'hashed‑placeholder',
    role: 'student',
    profileId: studentBob.id
  });

  // ------- Subjects -------
  const algSubject = await storage.add('subjects.json', {
    name: 'Introduction to Algorithms',
    code: 'CS101',
    departmentId: csDept.id,
    teacherId: teacherAlice.id
  });

  // Link subject to teacher & student
  await storage.update('teachers.json', teacherAlice.id, { subjectsIds: [algSubject.id] });
  await storage.update('students.json', studentBob.id, { enrolledSubjectIds: [algSubject.id] });

  // ------- Classrooms -------
  const roomA101 = await storage.add('classrooms.json', {
    roomNumber: 'A101',
    capacity: 40,
    building: 'Main Campus'
  });

  // ------- Timetable -------
  await storage.add('timetable.json', {
    subjectId: algSubject.id,
    classroomId: roomA101.id,
    dayOfWeek: 'Mon',
    startTime: '09:00',
    endTime: '10:30'
  });

  // ------- Laboratories -------
  await storage.add('laboratories.json', {
    name: 'Computer Lab 1',
    location: 'Building B',
    capacity: 25
  });

  // ------- Settings (default) -------
  await storage.write('settings.json', {
    semesterStart: '2027-01-10',
    semesterEnd: '2027-05-20',
    maxBookingHours: 4,
    timezone: 'Asia/Kolkata'
  });

  console.log('✅ Seed data creation complete.');
}

main().catch(err => {
  console.error('❌ Seed script error:', err);
  process.exit(1);
});
