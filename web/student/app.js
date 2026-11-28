'use strict';

let csrfToken = '';

const $ = (id) => document.getElementById(id);

function showMessage(text) {
  $('message').textContent = text;
}

function clearMessage() {
  $('message').textContent = '';
}

async function request(path, options) {
  const response = await fetch(path, Object.assign({ credentials: 'same-origin' }, options));
  let body = null;
  try {
    body = await response.json();
  } catch (error) {
    body = { code: 'InvalidInput', message: '响应不是 JSON' };
  }
  if (!response.ok || body.code !== 'Success') {
    throw new Error(body.message || ('HTTP ' + response.status));
  }
  return body.data;
}

function postJson(path, payload) {
  return request(path, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-CSRF-Token': csrfToken,
    },
    body: JSON.stringify(payload),
  });
}

function deleteWithCsrf(path) {
  return request(path, {
    method: 'DELETE',
    headers: { 'X-CSRF-Token': csrfToken },
  });
}

function line(text, className) {
  const element = document.createElement('div');
  element.textContent = text;
  if (className) {
    element.className = className;
  }
  return element;
}

function renderCourses(courses) {
  const container = $('courses');
  container.replaceChildren();
  if (courses.length === 0) {
    container.appendChild(line('没有找到匹配课程。', 'empty'));
    return;
  }
  for (const view of courses) {
    const title = view.course.course_code + ' ' + view.course.course_name +
      '（' + view.course.credits + ' 学分）';
    container.appendChild(line(title, 'title'));
    for (const item of view.teaching_classes) {
      const row = document.createElement('div');
      row.className = 'row';
      row.appendChild(line(
        '班 ' + item.teaching_class_id + ' / ' + item.teacher_name +
        ' / 容量 ' + item.capacity + ' / 余 ' + item.remaining_seats,
        'detail'));
      const button = document.createElement('button');
      button.textContent = '选课';
      button.addEventListener('click', () => enroll(item.teaching_class_id));
      row.appendChild(button);
      container.appendChild(row);
    }
  }
}

function renderTimetable(entries) {
  const container = $('timetable');
  container.replaceChildren();
  if (entries.length === 0) {
    container.appendChild(line('还没有选课。', 'empty'));
    return;
  }
  for (const entry of entries) {
    container.appendChild(line(
      '周' + entry.meeting.day_of_week + ' ' +
      entry.meeting.start_minute + '-' + entry.meeting.end_minute + ' ' +
      entry.course_code + ' ' + entry.course_name + ' ' +
      entry.meeting.room, 'detail'));
  }
}

function renderEnrollments(data) {
  const container = $('enrollments');
  container.replaceChildren();
  container.appendChild(line(
    '有效 ' + data.active + ' 门，已退 ' + data.dropped + ' 门。', 'detail'));
  for (const record of data.records) {
    const row = document.createElement('div');
    row.className = 'row';
    row.appendChild(line(
      '班 ' + record.teaching_class_id + ' / ' + record.status, 'detail'));
    if (record.status === 'active') {
      const button = document.createElement('button');
      button.textContent = '退课';
      button.addEventListener('click', () => drop(record.teaching_class_id));
      row.appendChild(button);
    }
    container.appendChild(row);
  }
}

async function enroll(classId) {
  clearMessage();
  try {
    await postJson('/api/me/enrollments', { teaching_class_id: classId });
    showMessage('选课成功。');
  } catch (error) {
    showMessage('选课失败：' + error.message);
  }
  await refreshAll();
}

async function drop(classId) {
  clearMessage();
  try {
    await deleteWithCsrf('/api/me/enrollments/' + classId);
    showMessage('退课成功。');
  } catch (error) {
    showMessage('退课失败：' + error.message);
  }
  await refreshAll();
}

async function search() {
  clearMessage();
  const keyword = $('keyword').value.trim();
  try {
    const courses = await request('/api/courses?keyword=' + encodeURIComponent(keyword));
    renderCourses(courses);
  } catch (error) {
    renderCourses([]);
    showMessage('查询失败：' + error.message);
  }
}

async function loadMe() {
  const me = await request('/api/auth/me');
  csrfToken = me.csrf_token;
  $('who').textContent = me.user.account_number + '（' + me.user.role + '）';
  $('login-view').classList.add('hidden');
  $('app-view').classList.remove('hidden');
}

async function refreshAll() {
  try {
    const timetable = await request('/api/me/timetable');
    renderTimetable(timetable);
    const enrollments = await request('/api/me/enrollments');
    renderEnrollments(enrollments);
  } catch (error) {
    showMessage('刷新失败：' + error.message);
  }
}

async function login() {
  $('login-error').textContent = '';
  const account = $('account').value.trim();
  const password = $('password').value;
  if (!account || !password) {
    $('login-error').textContent = '请输入学号和密码。';
    return;
  }
  try {
    await request('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ account_number: account, password: password }),
    });
    await loadMe();
    await search();
    await refreshAll();
  } catch (error) {
    $('login-error').textContent = '登录失败：' + error.message;
  }
}

async function logout() {
  try {
    await request('/api/auth/logout', {
      method: 'POST',
      headers: { 'X-CSRF-Token': csrfToken },
    });
  } catch (error) {
    // 会话已失效时也回到登录页。
  }
  csrfToken = '';
  $('app-view').classList.add('hidden');
  $('login-view').classList.remove('hidden');
  $('courses').replaceChildren();
  $('timetable').replaceChildren();
  $('enrollments').replaceChildren();
  clearMessage();
}

async function changePassword() {
  $('password-message').textContent = '';
  const currentPassword = $('current-password').value;
  const newPassword = $('new-password').value;
  if (!currentPassword || !newPassword) {
    $('password-message').textContent = '请输入当前密码和新密码。';
    return;
  }
  try {
    await request('/api/auth/password', {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json',
        'X-CSRF-Token': csrfToken,
      },
      body: JSON.stringify({
        current_password: currentPassword,
        new_password: newPassword,
      }),
    });
    $('password-message').textContent = '密码修改成功。';
    $('current-password').value = '';
    $('new-password').value = '';
  } catch (error) {
    $('password-message').textContent = '修改失败：' + error.message;
  }
}

$('login-button').addEventListener('click', login);
$('logout-button').addEventListener('click', logout);
$('search-button').addEventListener('click', search);
$('refresh-button').addEventListener('click', refreshAll);
$('change-password-button').addEventListener('click', changePassword);
$('keyword').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    search();
  }
});

// 页面加载时先问服务器当前会话；已登录则直接进入。
request('/api/auth/me').then(loadMe).then(async () => {
  await search();
  await refreshAll();
}).catch(() => {
  $('login-view').classList.remove('hidden');
});
