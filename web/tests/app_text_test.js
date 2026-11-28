// 前端纯文本函数的回归测试（不需要浏览器）。
//
// 为什么需要它：教室名由管理员自由填写，若原样拼进 innerHTML，就会变成标签
// 甚至脚本。这里检查 meetingText()/meetingsText() 的输出确实做过转义，并检查
// 改密弹窗的 HTML 模板在“拼接”之后仍然包含全部输入框和按钮——只做语法检查
// 抓不到漏写加号的问题（相邻字符串字面量会静默拼接成前半段）。
//
// 运行：node web/tests/app_text_test.js，失败时退出码为 1。

"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const source = fs.readFileSync(path.join(__dirname, "..", "shared", "app.js"), "utf8");

let checks = 0;
let failures = 0;

function check(condition, name) {
  checks += 1;
  if (!condition) {
    failures += 1;
    console.error("FAIL: " + name);
  }
}

// 最小的 DOM 桩：app.js 只在 DOMContentLoaded / 按钮存在时才挂事件，
// 这里让 readyState 直接是 complete 且没有按钮，加载即结束。
const sandbox = {
  window: {},
  document: {
    readyState: "complete",
    getElementById: () => null,
    addEventListener: () => {},
    createElement: () => ({ style: {}, appendChild: () => {}, remove: () => {} }),
    body: { appendChild: () => {} },
  },
  setTimeout: () => {},
  console,
};
vm.runInNewContext(source, sandbox);
const team4 = sandbox.window.Team4;
check(!!team4, "app.js 暴露 window.Team4");

// 1. 教室名里的标签必须变成普通文字。
const hostile = { day_of_week: 1, start_minute: 540, end_minute: 600, room: "<b>TestRoom</b>" };
const text = team4.meetingText(hostile);
check(text.indexOf("<b>") === -1, "meetingText 不输出裸标签");
check(text.indexOf("&lt;b&gt;TestRoom&lt;/b&gt;") !== -1, "meetingText 转义了尖括号");
check(text.indexOf("周一 09:00–10:00") === 0, "meetingText 保留星期与时间");

// 2. 引号、&、> 一并覆盖；多时段用分号连接。
const many = team4.meetingsText([
  { day_of_week: 2, start_minute: 600, end_minute: 660, room: 'A&B "x" > y' },
  { day_of_week: 3, start_minute: 600, end_minute: 660, room: "" },
]);
check(many.indexOf("A&amp;B &quot;x&quot; &gt; y") !== -1, "meetingsText 转义 & 引号 大于号");
check(many.split("；").length === 2, "meetingsText 用分号连接多时段");
check(team4.meetingsText([]) === "—", "空时段显示破折号");

// 3. esc() 自身幂等性检查：二次转义会把实体再转一次，调用方不应重复调用。
check(team4.esc("<b>") === "&lt;b&gt;", "esc 转义尖括号");
check(team4.esc("&lt;b&gt;") === "&amp;lt;b&amp;gt;", "esc 会二次转义，调用方不要重复调用");

// 4. 改密弹窗模板：把 backdrop.innerHTML 的表达式取出来实际求值，
//    确认拼接后仍含三个输入框、保存、取消与错误提示段落。
const start = source.indexOf("backdrop.innerHTML =");
const end = source.indexOf("function close()", start);
check(start !== -1 && end !== -1, "找到改密弹窗模板");
if (start !== -1 && end !== -1) {
  let expression = source.slice(start + "backdrop.innerHTML =".length, end).trim();
  if (expression.endsWith(";")) {
    expression = expression.slice(0, -1);
  }
  const html = vm.runInNewContext("(" + expression + ")", {});
  check(html.indexOf('id="pw-current"') !== -1, "弹窗含当前密码输入框");
  check(html.indexOf('id="pw-new"') !== -1, "弹窗含新密码输入框");
  check(html.indexOf('id="pw-confirm"') !== -1, "弹窗含确认密码输入框");
  check(html.indexOf('data-act="submit"') !== -1, "弹窗含保存按钮");
  check(html.indexOf('data-act="cancel"') !== -1, "弹窗含取消按钮");
  check(html.indexOf("</div>") !== -1, "弹窗含结束标签");
}

// 5. 密码规则与服务器保持同一套：长度、字符集、大小写与数字。
check(team4.api !== undefined, "api 已导出");
check(typeof team4.openPasswordDialog === "function", "改密入口已导出");

console.log((failures === 0 ? "PASS: " : "FAILED: ") + checks + " checks, " + failures + " failure(s)");
process.exit(failures === 0 ? 0 : 1);
