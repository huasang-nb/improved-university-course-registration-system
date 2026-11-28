# 学生端 HTTP 接口与页面（Zhang Yikai）

对应团队手册 `Team4_HTTP_Zhang_Yikai_操作手册`。本阶段接一条完整学生流程：

```
登录 → 课程搜索 / 详情 / 名额 → 课表 / 我的选课 → 选课 / 退课
```

教师和管理员页面按手册顺序后续再接。

## 文件

| 文件 | 作用 |
|---|---|
| `source_code/student_http_server.cpp` | 会话/CSRF/JSON 外壳 + 学生路由 + 静态页面挂载 |
| `third_party/httplib.h` | cpp-httplib 单头文件（MIT，见 `third_party/httplib.LICENSE`） |
| `web/index.html` | 跳转到学生页面 |
| `web/student/index.html`、`app.js`、`style.css` | 学生页面 |
| `run_student_http_api.ps1` | HTTP 冒烟检查（已接入 `build_and_test.ps1`） |

> 公共骨架（会话、CSRF、JSON 外壳、静态页面）暂放在本文件，等 Yuhua 的
> `http_application` 就位后替换 server 部分，学生路由与页面保持不变。

## 运行

```powershell
powershell -ExecutionPolicy Bypass -File .\build_and_test.ps1

.\build\course_registration_http_server.exe `
    --state build\http_demo\state.json `
    --port 8080 `
    --web-root web `
    --demo-password Demo2026!
```

浏览器打开 <http://127.0.0.1:8080/student/index.html>。

- 演示账号：`S2023001`（仅未启用 libsodium、且显式传 `--demo-password Demo2026!` 时可用）
- `--state` 必须显式指定，不会写默认 `data/state.json`；
- 未启用 libsodium 时**必须显式传 `--demo-password`** 才允许本地演示，否则启动即拒绝；
- 定义 `COURSE_REGISTRATION_USE_SODIUM` 后忽略演示密码，改为校验
  `password_hash`；未设置密码、哈希损坏或密码错误一律拒绝登录。

## 接口

所有响应统一为：

```json
{ "code": "Success", "message": "...", "data": null }
```

成功时空列表为 `[]`，失败时 `data` 为 `null`。

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/auth/login` | `{account_number, password}`；成功返回 `user` 与 `csrf_token`，并下发 `team4_session` Cookie |
| GET | `/api/auth/me` | 当前学生与 `csrf_token` |
| POST | `/api/auth/logout` | 需要 `X-CSRF-Token`；删除会话并清除 Cookie |
| PUT | `/api/auth/password` | `{current_password, new_password}`；需要 CSRF；按会话账号改密 |
| GET | `/api/courses?keyword=&course_code=&credits=&teacher_id=` | 课程搜索（空结果返回 `[]`） |
| GET | `/api/courses/{id}` | 课程详情（含教学班） |
| GET | `/api/classes/{id}/seats` | 教学班容量、人数、余量 |
| GET | `/api/me/timetable` | 当前学生课表 |
| GET | `/api/me/enrollments` | `active`、`dropped`、`records` |
| POST | `/api/me/enrollments` | `{teaching_class_id}`；需要 CSRF；成功 `data` 为 `null` |
| DELETE | `/api/me/enrollments/{class_id}` | 需要 CSRF；内部保留 Dropped 历史 |

### 状态码

| HTTP | 场景 |
|---|---|
| 200 | 查询/修改成功，搜索为空也是 200 |
| 400 | JSON 或参数非法 |
| 401 | 未登录、密码错误、会话失效 |
| 403 | 非学生角色、CSRF 校验失败 |
| 404 | 课程、教学班或有效选课记录不存在 |
| 409 | 重复选课、时间冲突、满班、学分超限、选课关闭 |
| 500 | 文件保存失败或内部数据异常 |

写操作（选课、退课、退出）必须带 `X-CSRF-Token`；浏览器页面从
`/api/auth/me` 取到后放在请求头里。

`PUT /api/auth/password` 复用 `password_service`：服务器按会话确定账号，
校验当前密码后写入新的 Argon2id 哈希并保存；不接受前端指定其他账号。
该接口需要 libsodium 构建，未启用时返回 501；当前密码错误返回 403。
新密码规则由密码服务统一（目前只检查非空与长度上限）。

## 并发与数据

- 进程内用一把 `std::mutex` 保护 `SystemContext` 的读写；
- 选课/退课继续走 `PersistentRegistrationService`（副本执行、保存成功才发布）；
- 同一个 JSON 文件只由本进程读写；多个进程同时写同一文件不在本阶段范围；
- `password_hash` 只留在服务器内存/文件里，响应只返回角色、编号和账号。

## 页面

- 页面加载先请求 `/api/auth/me`；未登录显示登录表单；
- 搜索、课表、我的选课都有空状态提示；
- 已登录页面提供“修改密码”表单，调用 `PUT /api/auth/password`，成功后清空输入；
- 选课/退课成功后重新查询余量、课表和状态；
- 使用 `textContent` 显示课程名与错误信息，避免把返回文本当 HTML。

## 冒烟检查

```powershell
powershell -ExecutionPolicy Bypass -File .\run_student_http_api.ps1
```

覆盖：服务启动与静态页面、未登录 401、错误密码 401、登录成功、
`/api/auth/me`、搜索命中/空结果、选课、课表、我的选课统计、重复选课 409、
缺少 CSRF 403、退课、退课后课表为空与统计变化、退出后旧会话 401。
