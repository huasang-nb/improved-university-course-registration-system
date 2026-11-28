// 教师端与管理员端共用的请求与渲染工具。
// 约定见 documentation/decisions/0001-http-contract.md：
//   - Cookie 会话，fetch 一律带 credentials: "include"；
//   - 写操作带 X-CSRF-Token，值来自 GET /api/auth/me；
//   - 响应外壳 {code, message, data}，失败时 data 为 null。
(function () {
  "use strict";

  let csrfToken = null;

  async function request(method, path, body) {
    const headers = {};
    if (body !== undefined) {
      headers["Content-Type"] = "application/json";
    }
    if (method !== "GET" && csrfToken) {
      headers["X-CSRF-Token"] = csrfToken;
    }
    let response;
    try {
      response = await fetch(path, {
        method,
        headers,
        credentials: "include",
        body: body === undefined ? undefined : JSON.stringify(body),
      });
    } catch (networkError) {
      const error = new Error("无法连接服务器：" + networkError.message);
      error.code = "NetworkError";
      throw error;
    }
    let payload;
    try {
      payload = await response.json();
    } catch (parseError) {
      payload = { code: "BadResponse", message: "服务器返回的不是 JSON", data: null };
    }
    if (!response.ok) {
      const error = new Error(payload.message || "HTTP " + response.status);
      error.code = payload.code || "HttpError";
      error.status = response.status;
      throw error;
    }
    return payload;
  }

  const api = {
    get: (path) => request("GET", path),
    post: (path, body) => request("POST", path, body === undefined ? {} : body),
    put: (path, body) => request("PUT", path, body),
    patch: (path, body) => request("PATCH", path, body),
    del: (path) => request("DELETE", path, {}),

    // 读取当前会话：拿身份与 CSRF；未登录返回 null（由页面决定跳转）。
    async session() {
      try {
        const payload = await request("GET", "/api/auth/me");
        const data = payload.data || {};
        csrfToken = data.csrf_token || null;
        return data.user || null;
      } catch (error) {
        if (error.status === 401) {
          return null;
        }
        throw error;
      }
    },
  };

  function esc(value) {
    return String(value === undefined || value === null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function toast(message, isError) {
    let host = document.getElementById("toast");
    if (!host) {
      host = document.createElement("div");
      host.id = "toast";
      document.body.appendChild(host);
    }
    const item = document.createElement("div");
    if (isError) {
      item.className = "error";
    }
    item.textContent = message;
    host.appendChild(item);
    setTimeout(() => item.remove(), isError ? 6000 : 3200);
  }

  // 统一处理失败：把 code 与 message 一起显示，便于按验收表核对。
  function report(error) {
    if (error && error.code === "AuthenticationRequired") {
      toast("会话已失效，请重新登录", true);
      return;
    }
    const code = error && error.code ? "[" + error.code + "] " : "";
    toast(code + (error && error.message ? error.message : "操作失败"), true);
  }

  const WEEKDAYS = ["", "周一", "周二", "周三", "周四", "周五", "周六", "周日"];

  function minuteText(minute) {
    const h = Math.floor(minute / 60);
    const m = minute % 60;
    return String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0");
  }

  function meetingText(meeting) {
    return (
      WEEKDAYS[meeting.day_of_week] +
      " " +
      minuteText(meeting.start_minute) +
      "–" +
      minuteText(meeting.end_minute) +
      (meeting.room ? " " + meeting.room : "")
    );
  }

  function meetingsText(meetings) {
    if (!meetings || !meetings.length) {
      return "—";
    }
    return meetings.map(meetingText).join("；");
  }

  // 时段编辑行：与接口的 meetings 字段一一对应。
  function meetingRow(meeting) {
    const row = document.createElement("div");
    row.className = "row meeting-row";
    row.innerHTML =
      '<label class="field">星期<select data-k="day_of_week">' +
      [1, 2, 3, 4, 5, 6, 7]
        .map((d) => '<option value="' + d + '">' + WEEKDAYS[d] + "</option>")
        .join("") +
      "</select></label>" +
      '<label class="field">开始<input data-k="start_minute" type="time" step="60"></label>' +
      '<label class="field">结束<input data-k="end_minute" type="time" step="60"></label>' +
      '<label class="field">教室<input data-k="room" type="text" maxlength="40"></label>' +
      '<button type="button" class="danger" data-act="remove-meeting">删除</button>';
    if (meeting) {
      row.querySelector('[data-k="day_of_week"]').value = String(meeting.day_of_week);
      row.querySelector('[data-k="start_minute"]').value = minuteText(meeting.start_minute);
      row.querySelector('[data-k="end_minute"]').value = minuteText(meeting.end_minute);
      row.querySelector('[data-k="room"]').value = meeting.room || "";
    }
    row.querySelector('[data-act="remove-meeting"]').addEventListener("click", () => row.remove());
    return row;
  }

  function minutesOf(text) {
    const parts = String(text || "").split(":");
    if (parts.length !== 2) {
      return null;
    }
    const h = Number(parts[0]);
    const m = Number(parts[1]);
    if (!Number.isInteger(h) || !Number.isInteger(m)) {
      return null;
    }
    return h * 60 + m;
  }

  function collectMeetings(host) {
    const meetings = [];
    for (const row of host.querySelectorAll(".meeting-row")) {
      const day = Number(row.querySelector('[data-k="day_of_week"]').value);
      const start = minutesOf(row.querySelector('[data-k="start_minute"]').value);
      const end = minutesOf(row.querySelector('[data-k="end_minute"]').value);
      const room = row.querySelector('[data-k="room"]').value.trim();
      if (start === null || end === null) {
        throw new Error("请填写完整的开始与结束时间");
      }
      meetings.push({ day_of_week: day, start_minute: start, end_minute: end, room });
    }
    if (!meetings.length) {
      throw new Error("至少需要一个上课时段");
    }
    return meetings;
  }

  function table(headers, rows) {
    const html = rows.length
      ? rows.join("")
      : '<tr><td class="empty" colspan="' + headers.length + '">暂无数据</td></tr>';
    return (
      "<table><thead><tr>" +
      headers.map((h) => "<th>" + esc(h) + "</th>").join("") +
      "</tr></thead><tbody>" +
      html +
      "</tbody></table>"
    );
  }

  function activeBadge(active) {
    return active
      ? '<span class="badge on">启用</span>'
      : '<span class="badge off">停用</span>';
  }

  // “修改我的密码”：与登录同一套公共接口（由组长骨架提供）。
  // 契约：PUT /api/auth/password {current_password, new_password}，
  // 成功 200；当前密码错误 400 InvalidInput；未登录/会话失效 401。
  // 接口尚未上线时（404/501）明确提示“未接入”，不做本地假成功。
  function openPasswordDialog() {
    const backdrop = document.createElement("div");
    backdrop.className = "modal-backdrop";
    backdrop.innerHTML =
      '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="pw-title">' +
      '<h3 id="pw-title">修改我的密码</h3>' +
      '<div class="form-row">' +
      '<label class="field">当前密码<input id="pw-current" type="password" autocomplete="current-password"></label>' +
      '<label class="field">新密码<input id="pw-new" type="password" autocomplete="new-password"></label>' +
      '<label class="field">确认新密码<input id="pw-confirm" type="password" autocomplete="new-password"></label>' +
      "</div>" +
      '<p class="hint">新密码至少 8 位；服务器校验当前密码后保存新哈希。</p>' +
      '<div class="form-actions">' +
      '<button type="button" class="secondary" data-act="cancel">取消</button>' +
      '<button type="button" data-act="submit">保存</button>' +
      "</div>" +
      "</div>";

    function close() {
      backdrop.remove();
      document.removeEventListener("keydown", onKeydown);
    }

    function onKeydown(event) {
      if (event.key === "Escape") {
        close();
      }
    }

    async function submit() {
      const current = backdrop.querySelector("#pw-current").value;
      const next = backdrop.querySelector("#pw-new").value;
      const confirm = backdrop.querySelector("#pw-confirm").value;
      if (!current || !next) {
        toast("请填写当前密码与新密码", true);
        return;
      }
      if (next !== confirm) {
        toast("两次输入的新密码不一致", true);
        return;
      }
      if (next.length < 8) {
        toast("新密码至少 8 位", true);
        return;
      }
      try {
        await api.put("/api/auth/password", {
          current_password: current,
          new_password: next,
        });
        close();
        // 改密成功后服务端会撤销该账号的全部旧会话（含当前这一个），
        // 所以这里不做“原地继续使用”，回到登录页重新认证。
        toast("密码已修改，请用新密码重新登录");
        setTimeout(() => window.location.reload(), 1200);
      } catch (error) {
        if (
          error.status === 404 ||
          error.status === 501 ||
          error.code === "NotImplemented"
        ) {
          toast("改密接口尚未接入：等组长的公共骨架上线后自动生效", true);
          return;
        }
        report(error);
      }
    }

    backdrop.addEventListener("click", (event) => {
      if (event.target === backdrop) {
        close();
      }
    });
    backdrop.querySelector('[data-act="cancel"]').addEventListener("click", close);
    backdrop.querySelector('[data-act="submit"]').addEventListener("click", submit);
    document.addEventListener("keydown", onKeydown);
    document.body.appendChild(backdrop);
    backdrop.querySelector("#pw-current").focus();
  }

  // 页面只要放一个 id="change-password" 的按钮，就会自动挂上入口。
  function installPasswordEntry() {
    const button = document.getElementById("change-password");
    if (!button) {
      return;
    }
    button.addEventListener("click", openPasswordDialog);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", installPasswordEntry);
  } else {
    installPasswordEntry();
  }

  window.Team4 = {
    api,
    esc,
    toast,
    report,
    openPasswordDialog,
    installPasswordEntry,
    meetingRow,
    collectMeetings,
    meetingsText,
    meetingText,
    table,
    activeBadge,
    WEEKDAYS,
  };
})();
