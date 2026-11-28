// 教师端页面：本人教学班列表、班级汇总、学生名单。
// 只使用 /api/teacher/*，越权由后端返回 403，这里不做二次判断。
(function () {
  "use strict";

  const { api, esc, report, meetingsText, table, toast } = window.Team4;

  const state = { classes: [], selected: null };

  function renderClasses() {
    const host = document.getElementById("class-list");
    if (!state.classes.length) {
      host.innerHTML = '<p class="empty">你目前没有教学班。</p>';
      return;
    }
    const rows = state.classes.map((item) => {
      const active = state.selected === item.teaching_class_id;
      return (
        "<tr>" +
        "<td>" + item.teaching_class_id + "</td>" +
        "<td>" + esc(item.course_code) + "</td>" +
        "<td>" + esc(item.course_name) + "</td>" +
        "<td>" + item.credits + "</td>" +
        "<td>" + meetingsText(item.meetings) + "</td>" +
        "<td>" + item.active_enrollments + " / " + item.capacity + "</td>" +
        "<td>" + item.remaining_seats + "</td>" +
        '<td class="actions"><button data-id="' + item.teaching_class_id + '">' +
        (active ? "刷新" : "查看") +
        "</button></td>" +
        "</tr>"
      );
    });
    host.innerHTML = table(
      ["编号", "课程代码", "课程名称", "学分", "时间地点", "已选/容量", "余量", ""],
      rows
    );
    host.querySelectorAll("button[data-id]").forEach((button) => {
      button.addEventListener("click", () => selectClass(Number(button.dataset.id)));
    });
  }

  async function loadClasses() {
    try {
      const data = (await api.get("/api/teacher/classes")).data;
      state.classes = data || [];
      renderClasses();
    } catch (error) {
      report(error);
    }
  }

  async function selectClass(id) {
    state.selected = id;
    renderClasses();
    document.getElementById("detail-title").textContent = "教学班 " + id + " 详情";
    try {
      const summary = (await api.get("/api/teacher/classes/" + id + "/summary")).data;
      const roster = (await api.get("/api/teacher/classes/" + id + "/roster")).data;

      document.getElementById("summary").innerHTML =
        '<div class="row">' +
        '<div><div class="hint">课程</div><div>' + esc(summary.course_code) + " " +
        esc(summary.course_name) + "（" + summary.credits + " 学分）</div></div>" +
        '<div><div class="hint">时间地点</div><div>' + meetingsText(summary.meetings) + "</div></div>" +
        '<div><div class="hint">选课人数</div><div>' + summary.active_enrollments + " / " +
        summary.capacity + "</div></div>" +
        '<div><div class="hint">余量</div><div>' + summary.remaining_seats + "</div></div>" +
        "</div>";

      const rows = roster.students.map((student) =>
        "<tr><td>" + esc(student.student_number) + "</td><td>" + esc(student.name) + "</td></tr>"
      );
      document.getElementById("roster").innerHTML =
        table(["学号", "姓名"], rows) +
        '<p class="hint">名单只包含当前有效（active）的选课记录，按学号排序。</p>';
    } catch (error) {
      report(error);
      document.getElementById("summary").innerHTML = "";
      document.getElementById("roster").innerHTML = "";
    }
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
    if (user.role !== "Teacher") {
      document.getElementById("guard").innerHTML =
        '<p class="empty">本页面只对教师开放，当前角色是 ' + esc(user.role) + "。</p>";
      return;
    }
    document.getElementById("content").hidden = false;
    await loadClasses();
    const refresh = document.getElementById("refresh");
    refresh.addEventListener("click", () => {
      loadClasses();
      if (state.selected !== null) {
        selectClass(state.selected);
      }
      toast("已刷新");
    });
  }

  main().catch(report);
})();
