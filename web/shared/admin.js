// 管理员端页面：课程与教学班、学生、教师、选课规则。
// 所有写操作都经后端做权限、CSRF、校验与持久化，这里只负责收集输入与展示结果。
(function () {
  "use strict";

  const { api, esc, report, toast, meetingRow, collectMeetings, meetingsText, table, activeBadge } =
    window.Team4;

  const state = {
    courses: [],
    classes: [],
    students: [],
    teachers: [],
    rule: null,
    courseEditing: null,
    classEditing: null,
    studentEditing: null,
    teacherEditing: null,
  };

  function showTab(name) {
    document.querySelectorAll(".tabs button").forEach((button) => {
      button.classList.toggle("active", button.dataset.tab === name);
    });
    document.querySelectorAll("section.tab-panel").forEach((panel) => {
      panel.hidden = panel.dataset.tab !== name;
    });
  }

  // ---------------------------------------------------------------- 加载

  async function loadCourses() {
    state.courses = (await api.get("/api/admin/courses")).data || [];
    renderCourses();
  }

  async function loadClasses() {
    state.classes = (await api.get("/api/admin/classes")).data || [];
    renderClasses();
  }

  async function loadStudents() {
    state.students = (await api.get("/api/admin/students")).data || [];
    renderStudents();
  }

  async function loadTeachers() {
    state.teachers = (await api.get("/api/admin/teachers")).data || [];
    renderTeachers();
  }

  async function loadRule() {
    state.rule = (await api.get("/api/admin/registration-rule")).data || {};
    renderRule();
  }

  async function loadAll() {
    await Promise.all([loadCourses(), loadClasses(), loadStudents(), loadTeachers(), loadRule()]);
  }

  // ---------------------------------------------------------------- 课程

  function renderCourses() {
    const rows = [];
    for (const entry of state.courses) {
      const course = entry.course;
      const classes = entry.teaching_classes || [];
      const classText = classes.length
        ? classes
            .map(
              (item) =>
                "#" + item.teaching_class_id + "（" + item.active_enrollments + "/" +
                item.capacity + "）"
            )
            .join("、")
        : '<span class="badge off">缺教学班</span>';
      rows.push(
        "<tr>" +
          "<td>" + course.course_id + "</td>" +
          "<td>" + esc(course.course_code) + "</td>" +
          "<td>" + esc(course.course_name) + "</td>" +
          "<td>" + course.credits + "</td>" +
          "<td>" + classText + "</td>" +
          '<td class="actions">' +
          '<button data-act="edit-course" data-id="' + course.course_id + '">编辑</button> ' +
          '<button class="danger" data-act="del-course" data-id="' + course.course_id + '">删除</button>' +
          "</td>" +
          "</tr>"
      );
    }
    document.getElementById("course-table").innerHTML = table(
      ["编号", "课程代码", "课程名称", "学分", "教学班（已选/容量）", ""],
      rows
    );
  }

  function resetCourseForm() {
    state.courseEditing = null;
    const form = document.getElementById("course-form");
    form.reset();
    document.getElementById("course-form-title").textContent = "新建课程（含第一个教学班）";
    document.getElementById("course-id").disabled = false;
    document.getElementById("course-submit").textContent = "创建";
    const host = document.getElementById("course-meetings");
    host.innerHTML = "";
    host.appendChild(meetingRow(null));
  }

  function fillCourseForm(course, classes) {
    state.courseEditing = course.course_id;
    document.getElementById("course-form-title").textContent =
      "编辑课程 #" + course.course_id + "（只改课程字段；班级在下方单独维护）";
    document.getElementById("course-id").value = course.course_id;
    document.getElementById("course-id").disabled = true;
    document.getElementById("course-code").value = course.course_code;
    document.getElementById("course-name").value = course.course_name;
    document.getElementById("course-credits").value = course.credits;
    document.getElementById("course-submit").textContent = "保存课程";
    document.getElementById("course-first-class").hidden = true;
    const host = document.getElementById("course-meetings");
    host.innerHTML = "";
    if (classes && classes.length) {
      host.appendChild(meetingRow(classes[0].meetings ? classes[0].meetings[0] : null));
    }
  }

  async function submitCourse(event) {
    event.preventDefault();
    try {
      if (state.courseEditing === null) {
        const firstClass = {
          teaching_class_id: Number(document.getElementById("first-class-id").value),
          course_id: Number(document.getElementById("course-id").value),
          teacher_id: Number(document.getElementById("first-class-teacher").value),
          capacity: Number(document.getElementById("first-class-capacity").value),
          meetings: collectMeetings(document.getElementById("course-meetings")),
        };
        const course = {
          course_id: Number(document.getElementById("course-id").value),
          course_code: document.getElementById("course-code").value.trim(),
          course_name: document.getElementById("course-name").value.trim(),
          credits: Number(document.getElementById("course-credits").value),
        };
        await api.post("/api/admin/courses", { course, first_teaching_class: firstClass });
        toast("课程与第一个教学班已创建");
      } else {
        const course = {
          course_id: state.courseEditing,
          course_code: document.getElementById("course-code").value.trim(),
          course_name: document.getElementById("course-name").value.trim(),
          credits: Number(document.getElementById("course-credits").value),
        };
        await api.put("/api/admin/courses/" + state.courseEditing, course);
        toast("课程已保存");
      }
      resetCourseForm();
      await Promise.all([loadCourses(), loadClasses()]);
    } catch (error) {
      report(error);
    }
  }

  // ---------------------------------------------------------------- 教学班

  function renderClasses() {
    const rows = state.classes.map((item) =>
      "<tr>" +
        "<td>" + item.teaching_class_id + "</td>" +
        "<td>" + esc(item.course_code) + " " + esc(item.course_name) + "</td>" +
        "<td>" + esc(item.teacher_name) + "（" + item.teacher_id + "）</td>" +
        "<td>" + item.capacity + "</td>" +
        "<td>" + item.active_enrollments + "</td>" +
        "<td>" + item.remaining_seats + "</td>" +
        "<td>" + meetingsText(item.meetings) + "</td>" +
        '<td class="actions">' +
        '<button data-act="edit-class" data-id="' + item.teaching_class_id + '">编辑</button> ' +
        '<button data-act="capacity" data-id="' + item.teaching_class_id + '">改容量</button> ' +
        '<button class="danger" data-act="del-class" data-id="' + item.teaching_class_id + '">删除</button>' +
        "</td>" +
        "</tr>"
    );
    document.getElementById("class-table").innerHTML = table(
      ["编号", "课程", "教师", "容量", "已选", "余量", "时间地点", ""],
      rows
    );
  }

  function classFormValues() {
    return {
      teaching_class_id: Number(document.getElementById("class-id").value),
      course_id: Number(document.getElementById("class-course").value),
      teacher_id: Number(document.getElementById("class-teacher").value),
      capacity: Number(document.getElementById("class-capacity").value),
      meetings: collectMeetings(document.getElementById("class-meetings")),
    };
  }

  function resetClassForm() {
    state.classEditing = null;
    document.getElementById("class-form").reset();
    document.getElementById("class-form-title").textContent = "新建教学班";
    document.getElementById("class-id").disabled = false;
    document.getElementById("class-submit").textContent = "创建";
    const host = document.getElementById("class-meetings");
    host.innerHTML = "";
    host.appendChild(meetingRow(null));
  }

  function fillClassForm(item) {
    state.classEditing = item.teaching_class_id;
    document.getElementById("class-form-title").textContent = "编辑教学班 #" + item.teaching_class_id;
    document.getElementById("class-id").value = item.teaching_class_id;
    document.getElementById("class-id").disabled = true;
    document.getElementById("class-course").value = String(item.course_id);
    document.getElementById("class-teacher").value = String(item.teacher_id);
    document.getElementById("class-capacity").value = item.capacity;
    const host = document.getElementById("class-meetings");
    host.innerHTML = "";
    (item.meetings || []).forEach((meeting) => host.appendChild(meetingRow(meeting)));
    if (!host.children.length) {
      host.appendChild(meetingRow(null));
    }
    document.getElementById("class-submit").textContent = "保存教学班";
  }

  async function submitClass(event) {
    event.preventDefault();
    try {
      const values = classFormValues();
      if (state.classEditing === null) {
        await api.post("/api/admin/classes", values);
        toast("教学班已创建");
      } else {
        await api.put("/api/admin/classes/" + state.classEditing, values);
        toast("教学班已保存");
      }
      resetClassForm();
      await Promise.all([loadClasses(), loadCourses()]);
    } catch (error) {
      report(error);
    }
  }

  async function changeCapacity(id) {
    const current = state.classes.find((item) => item.teaching_class_id === id);
    const text = window.prompt("新的容量（不能低于有效选课人数）", current ? current.capacity : "");
    if (text === null) {
      return;
    }
    try {
      await api.patch("/api/admin/classes/" + id + "/capacity", { capacity: Number(text) });
      toast("容量已更新");
      await Promise.all([loadClasses(), loadCourses()]);
    } catch (error) {
      report(error);
    }
  }

  // ---------------------------------------------------------------- 学生 / 教师

  function renderStudents() {
    const rows = state.students.map((item) =>
      "<tr>" +
        "<td>" + item.student_id + "</td>" +
        "<td>" + esc(item.student_number) + "</td>" +
        "<td>" + esc(item.name) + "</td>" +
        "<td>" + esc(item.major) + "</td>" +
        "<td>" + activeBadge(item.account_active) + "</td>" +
        '<td class="actions">' +
        '<button data-act="edit-student" data-id="' + item.student_id + '">编辑</button> ' +
        '<button class="danger" data-act="del-student" data-id="' + item.student_id + '">删除</button>' +
        "</td>" +
        "</tr>"
    );
    document.getElementById("student-table").innerHTML = table(
      ["编号", "学号", "姓名", "专业", "状态", ""],
      rows
    );
  }

  function renderTeachers() {
    const rows = state.teachers.map((item) =>
      "<tr>" +
        "<td>" + item.teacher_id + "</td>" +
        "<td>" + esc(item.teacher_number) + "</td>" +
        "<td>" + esc(item.name) + "</td>" +
        "<td>" + esc(item.department) + "</td>" +
        "<td>" + activeBadge(item.account_active) + "</td>" +
        '<td class="actions">' +
        '<button data-act="edit-teacher" data-id="' + item.teacher_id + '">编辑</button> ' +
        '<button class="danger" data-act="del-teacher" data-id="' + item.teacher_id + '">删除</button>' +
        "</td>" +
        "</tr>"
    );
    document.getElementById("teacher-table").innerHTML = table(
      ["编号", "工号", "姓名", "院系", "状态", ""],
      rows
    );
  }

  function resetStudentForm() {
    state.studentEditing = null;
    document.getElementById("student-form").reset();
    document.getElementById("student-form-title").textContent = "新建学生";
    document.getElementById("student-id").disabled = false;
    document.getElementById("student-active").checked = true;
  }

  function fillStudentForm(item) {
    state.studentEditing = item.student_id;
    document.getElementById("student-form-title").textContent = "编辑学生 #" + item.student_id;
    document.getElementById("student-id").value = item.student_id;
    document.getElementById("student-id").disabled = true;
    document.getElementById("student-number").value = item.student_number;
    document.getElementById("student-name").value = item.name;
    document.getElementById("student-major").value = item.major;
    document.getElementById("student-active").checked = item.account_active;
  }

  function resetTeacherForm() {
    state.teacherEditing = null;
    document.getElementById("teacher-form").reset();
    document.getElementById("teacher-form-title").textContent = "新建教师";
    document.getElementById("teacher-id").disabled = false;
    document.getElementById("teacher-active").checked = true;
  }

  function fillTeacherForm(item) {
    state.teacherEditing = item.teacher_id;
    document.getElementById("teacher-form-title").textContent = "编辑教师 #" + item.teacher_id;
    document.getElementById("teacher-id").value = item.teacher_id;
    document.getElementById("teacher-id").disabled = true;
    document.getElementById("teacher-number").value = item.teacher_number;
    document.getElementById("teacher-name").value = item.name;
    document.getElementById("teacher-department").value = item.department;
    document.getElementById("teacher-active").checked = item.account_active;
  }

  async function submitStudent(event) {
    event.preventDefault();
    const student = {
      student_id: Number(document.getElementById("student-id").value),
      student_number: document.getElementById("student-number").value.trim(),
      name: document.getElementById("student-name").value.trim(),
      major: document.getElementById("student-major").value.trim(),
      account_active: document.getElementById("student-active").checked,
    };
    try {
      if (state.studentEditing === null) {
        await api.post("/api/admin/students", student);
      } else {
        await api.put("/api/admin/students/" + state.studentEditing, student);
      }
      toast("学生已保存");
      resetStudentForm();
      await loadStudents();
    } catch (error) {
      report(error);
    }
  }

  async function submitTeacher(event) {
    event.preventDefault();
    const teacher = {
      teacher_id: Number(document.getElementById("teacher-id").value),
      teacher_number: document.getElementById("teacher-number").value.trim(),
      name: document.getElementById("teacher-name").value.trim(),
      department: document.getElementById("teacher-department").value.trim(),
      account_active: document.getElementById("teacher-active").checked,
    };
    try {
      if (state.teacherEditing === null) {
        await api.post("/api/admin/teachers", teacher);
      } else {
        await api.put("/api/admin/teachers/" + state.teacherEditing, teacher);
      }
      toast("教师已保存");
      resetTeacherForm();
      await loadTeachers();
    } catch (error) {
      report(error);
    }
  }

  // ---------------------------------------------------------------- 规则

  function renderRule() {
    const rule = state.rule || {};
    document.getElementById("rule-open").textContent = rule.registration_open ? "开放" : "关闭";
    document.getElementById("rule-round").textContent = rule.round_opened ? "已开放过（上限锁定）" : "未开放";
    document.getElementById("rule-period").textContent =
      (rule.opens_at || "—") + " → " + (rule.closes_at || "—");
    document.getElementById("rule-credits").textContent = rule.maximum_credits;
    document.getElementById("rule-opens").value = rule.opens_at || "";
    document.getElementById("rule-closes").value = rule.closes_at || "";
    document.getElementById("rule-limit").value = rule.maximum_credits;
  }

  async function savePeriod(event) {
    event.preventDefault();
    try {
      await api.put("/api/admin/registration-period", {
        opens_at: document.getElementById("rule-opens").value,
        closes_at: document.getElementById("rule-closes").value,
      });
      toast("选课时间已保存");
      await Promise.all([loadRule(), loadClasses()]);
    } catch (error) {
      report(error);
    }
  }

  async function saveLimit(event) {
    event.preventDefault();
    try {
      await api.put("/api/admin/credit-limit", {
        maximum_credits: Number(document.getElementById("rule-limit").value),
      });
      toast("学分上限已保存");
      await loadRule();
    } catch (error) {
      report(error);
    }
  }

  async function closeRegistration() {
    if (!window.confirm("关闭本轮选课？关闭后学生不能再选课。")) {
      return;
    }
    try {
      await api.post("/api/admin/registration-close", {});
      toast("本轮选课已关闭");
      await Promise.all([loadRule(), loadClasses()]);
    } catch (error) {
      report(error);
    }
  }

  // ---------------------------------------------------------------- 事件绑定

  function bindActions(host) {
    host.addEventListener("click", async (event) => {
      const button = event.target.closest("button[data-act]");
      if (!button) {
        return;
      }
      const id = Number(button.dataset.id);
      const action = button.dataset.act;
      if (action === "edit-course") {
        const entry = state.courses.find((item) => item.course.course_id === id);
        if (entry) {
          fillCourseForm(entry.course, entry.teaching_classes);
        }
      } else if (action === "del-course") {
        if (!window.confirm("删除课程 #" + id + "？仍有教学班或选课记录时会被拒绝。")) {
          return;
        }
        try {
          await api.del("/api/admin/courses/" + id);
          toast("课程已删除");
          await Promise.all([loadCourses(), loadClasses()]);
        } catch (error) {
          report(error);
        }
      } else if (action === "edit-class") {
        const item = state.classes.find((row) => row.teaching_class_id === id);
        if (item) {
          fillClassForm(item);
        }
      } else if (action === "capacity") {
        await changeCapacity(id);
      } else if (action === "del-class") {
        if (!window.confirm("删除教学班 #" + id + "？")) {
          return;
        }
        try {
          await api.del("/api/admin/classes/" + id);
          toast("教学班已删除");
          await Promise.all([loadClasses(), loadCourses()]);
        } catch (error) {
          report(error);
        }
      } else if (action === "edit-student") {
        const item = state.students.find((row) => row.student_id === id);
        if (item) {
          fillStudentForm(item);
        }
      } else if (action === "del-student") {
        if (!window.confirm("删除学生 #" + id + "？有选课历史时会被拒绝，请改用停用。")) {
          return;
        }
        try {
          await api.del("/api/admin/students/" + id);
          toast("学生已删除");
          await loadStudents();
        } catch (error) {
          report(error);
        }
      } else if (action === "edit-teacher") {
        const item = state.teachers.find((row) => row.teacher_id === id);
        if (item) {
          fillTeacherForm(item);
        }
      } else if (action === "del-teacher") {
        if (!window.confirm("删除教师 #" + id + "？仍带班时会被拒绝。")) {
          return;
        }
        try {
          await api.del("/api/admin/teachers/" + id);
          toast("教师已删除");
          await loadTeachers();
        } catch (error) {
          report(error);
        }
      }
    });
  }

  function fillSelects() {
    const courseOptions = state.courses
      .map(
        (entry) =>
          '<option value="' + entry.course.course_id + '">' +
          esc(entry.course.course_code) + " " + esc(entry.course.course_name) + "</option>"
      )
      .join("");
    document.getElementById("class-course").innerHTML = courseOptions;
    document.getElementById("course-course-label").textContent = "";
    const teacherOptions = state.teachers
      .map(
        (item) =>
          '<option value="' + item.teacher_id + '">' +
          esc(item.name) + "（" + esc(item.teacher_number) + "）" +
          (item.account_active ? "" : " · 已停用") +
          "</option>"
      )
      .join("");
    document.getElementById("class-teacher").innerHTML = teacherOptions;
    document.getElementById("first-class-teacher").innerHTML = teacherOptions;
  }

  async function main() {
    const user = await api.session();
    if (!user) {
      document.getElementById("identity").textContent = "未登录";
      document.getElementById("guard").innerHTML =
        '<p class="empty">未登录或会话已过期。登录页由组长（Yuhua）的公共骨架提供。</p>';
      return;
    }
    document.getElementById("identity").textContent =
      user.name + "（" + user.account_number + " · " + user.role + "）";
    if (user.role !== "Administrator") {
      document.getElementById("guard").innerHTML =
        '<p class="empty">本页面只对管理员开放，当前角色是 ' + esc(user.role) + "。</p>";
      return;
    }
    document.getElementById("content").hidden = false;
    document.querySelector(".tabs").addEventListener("click", (event) => {
      const button = event.target.closest("button[data-tab]");
      if (button) {
        showTab(button.dataset.tab);
      }
    });
    document.getElementById("course-form").addEventListener("submit", submitCourse);
    document.getElementById("class-form").addEventListener("submit", submitClass);
    document.getElementById("student-form").addEventListener("submit", submitStudent);
    document.getElementById("teacher-form").addEventListener("submit", submitTeacher);
    document.getElementById("rule-period-form").addEventListener("submit", savePeriod);
    document.getElementById("rule-limit-form").addEventListener("submit", saveLimit);
    document.getElementById("rule-close").addEventListener("click", closeRegistration);
    document.getElementById("course-reset").addEventListener("click", resetCourseForm);
    document.getElementById("class-reset").addEventListener("click", resetClassForm);
    document.getElementById("student-reset").addEventListener("click", resetStudentForm);
    document.getElementById("teacher-reset").addEventListener("click", resetTeacherForm);
    document.getElementById("add-course-meeting").addEventListener("click", () =>
      document.getElementById("course-meetings").appendChild(meetingRow(null))
    );
    document.getElementById("add-class-meeting").addEventListener("click", () =>
      document.getElementById("class-meetings").appendChild(meetingRow(null))
    );
    document.getElementById("refresh").addEventListener("click", async () => {
      await loadAll();
      fillSelects();
      toast("已刷新");
    });

    bindActions(document.body);
    await loadAll();
    fillSelects();
    resetCourseForm();
    resetClassForm();
    resetStudentForm();
    resetTeacherForm();
    showTab("courses");
  }

  main().catch(report);
})();
