// 登录页：只调用公共认证接口 POST /api/auth/login。
// 成功后按角色跳到对应页面；学生页面由 Kai 接入，这里给出明确提示而不是假装跳转。
// 登录接口本身不要求 CSRF Token，但服务器会校验 Origin/Referer（同源），
// 所以这里始终用同源 fetch，不手工设置 Origin 头。
(function () {
  "use strict";

  const { api, report, toast } = window.Team4;

  const form = document.getElementById("login-form");
  const errorBox = document.getElementById("login-error");

  function showError(text) {
    errorBox.textContent = text;
  }

  function landingFor(role) {
    if (role === "Administrator") {
      return "/admin.html";
    }
    if (role === "Teacher") {
      return "/teacher.html";
    }
    return null;
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    showError("");
    const account = document.getElementById("account").value.trim();
    const password = document.getElementById("password").value;
    if (!account || !password) {
      showError("请填写账号和密码");
      return;
    }
    try {
      const payload = await api.post("/api/auth/login", {
        account_number: account,
        password: password,
      });
      const user = (payload && payload.data && payload.data.user) || {};
      const target = landingFor(user.role);
      if (target) {
        window.location.replace(target);
        return;
      }
      toast("学生页面尚未接入这个服务器，请使用学生端入口");
    } catch (error) {
      if (error.code === "AuthenticationRequired") {
        showError("账号或密码不正确，或账号已停用");
        return;
      }
      if (error.code === "CsrfRejected") {
        showError("登录来源校验失败：请通过服务器地址打开本页面，不要用 IP 或别名访问");
        return;
      }
      showError(error.message || "登录失败");
      report(error);
    }
  });
})();
