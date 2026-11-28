# 教师 / 管理员 HTTP 接口与页面（Zhu Ziliang）

本文件说明第 5 周交付的教师端、管理员端 HTTP 接口、前端页面、联调方式与验证结果。
登录、会话存储、CSRF 令牌签发与公共启动入口由组长（Yuhua）负责；本模块按
`documentation/decisions/0001-http-contract.md` 的约定接入，不自行发明会话机制。

## 1. 交付内容

| 层 | 文件 | 说明 |
| --- | --- | --- |
| 请求分发 | `header_files/http_support.hpp`、`header_files/http_teacher_admin_handlers.hpp`、`source_code/http_teacher_admin_handlers.cpp` | 与 HTTP 框架无关：路由正则、身份与 CSRF 校验、JSON 解析、业务调用、统一响应外壳。测试直接调 `dispatch()`。 |
| 框架适配 | `header_files/http_teacher_admin_routes.hpp`、`source_code/http_teacher_admin_routes.cpp` | 把 `httplib::Request` 转成 `http::Request`，把 `http::Response` 写回；在 `/api/(teacher\|admin)/.*` 上注册五个方法。 |
| 本地验证服务 | `source_code/http_teacher_admin_dev_server.cpp` | 仅供联调与页面走查：身份由命令行给出、不做密码校验、只监听 `127.0.0.1`。**不是**产品入口。 |
| 页面 | `web/teacher.html`、`web/admin.html`、`web/shared/*.js`、`web/shared/app.css` | 原生 JS + `fetch`，写操作自动带 `X-CSRF-Token`，错误按 `code` + `message` 展示。 |
| 自动测试 | `automated_tests/http_teacher_admin_tests.cpp` | 270 项检查：401/403/400/404/409/CSRF/停用账号/成功落盘/跨模块一致性。 |
| 依赖 | `third_party/httplib`、`third_party/nlohmann` | cpp-httplib v0.67.0、nlohmann/json v3.11.3，版本与 SHA-256 见 `third_party/README.md`。 |

## 2. 构建

```powershell
cmake -S . -B build\cmake-http -G "Visual Studio 17 2022" -A x64
cmake --build build\cmake-http --config Debug
ctest --test-dir build\cmake-http -C Debug --output-on-failure
```

- `http_teacher_admin_handlers`：静态库，只依赖 nlohmann/json。
- `http_teacher_admin_routes`：仅 `WIN32` 构建，额外依赖 cpp-httplib 与 `ws2_32`。
- `http_teacher_admin_tests`：`ctest` 名称 `http_teacher_admin`。
- `http_teacher_admin_dev_server`：仅 `WIN32`，本地联调用。

MinGW GCC 8 无法编译 cpp-httplib v0.67.0（`httplib.h` 直接 `#error` 不支持
Windows 8 及更早，且旧 SDK 缺少 `CreateFile2` 等符号），`build_and_test.ps1`
因此不包含 HTTP 目标；HTTP 部分只用 MSVC 2022 验证。

## 3. 接入方式（交给 Yuhua）

```cpp
#include "http_teacher_admin_routes.hpp"

course_registration::http::Services services{
    context,                 // 唯一的 SystemContext
    persistent_admin,        // PersistentAdministratorService
    teacher_service,         // TeacherService
    business_mutex};         // 一把覆盖「身份检查 + 业务 + 保存」的互斥量

register_teacher_admin_routes(server, services,
    [&session_store](const httplib::Request& request) {
        course_registration::http::SessionView view;
        view.identity = session_store.identity(request);  // Cookie team4_session
        view.csrf_ok = session_store.csrf_ok(request);    // X-CSRF-Token
        return view;
    });
```

约定：

1. `dispatch()` 自己在 `business_mutex` 内完成“身份复核 → 业务 → 保存 → 复制响应”，
   查询也加锁；密码校验等慢操作留在锁外（调用方自行处理）。
2. 会话身份每次请求都会重新核对：账号仍存在、`account_active` 为真、账号名
   （`normalize_account_key`）与会话一致，否则返回 401 `AuthenticationRequired`。
3. 写操作要求 `csrf_ok`，否则 403 `CsrfRejected`。读操作不要求 CSRF。
4. 响应统一为 `{"code": "...", "message": "...", "data": ...}`；失败时 `data` 为 `null`。
5. 人员响应（学生/教师）不含 `password_hash`。

## 4. 路由表

| 方法 | 路径 | 角色 | 成功 | 主要失败 |
| --- | --- | --- | --- | --- |
| GET | `/api/teacher/classes` | 教师 | 200 本人教学班摘要数组 | 401 |
| GET | `/api/teacher/classes/{id}/summary` | 教师 | 200 班级摘要 | 403 他人班级、404 不存在 |
| GET | `/api/teacher/classes/{id}/roster` | 教师 | 200 名单（仅 Active） | 403、404 |
| GET | `/api/admin/courses` | 管理员 | 200 `[{course, teaching_classes}]` | 401、403 |
| POST | `/api/admin/courses` | 管理员 | 200 `{course_id, teaching_class_id}` | 400、409 |
| PUT | `/api/admin/courses/{id}` | 管理员 | 200 `{course_id}` | 400、404、409 |
| DELETE | `/api/admin/courses/{id}` | 管理员 | 200 级联删除教学班 | 409 有选课记录 |
| GET | `/api/admin/classes` | 管理员 | 200 全部教学班摘要 | 401、403 |
| POST | `/api/admin/classes` | 管理员 | 200 `{teaching_class_id}` | 400、409 |
| PUT | `/api/admin/classes/{id}` | 管理员 | 200 `{teaching_class_id}` | 400、404、409 |
| PATCH | `/api/admin/classes/{id}/capacity` | 管理员 | 200 `{teaching_class_id, capacity}` | 409 低于已选人数 |
| DELETE | `/api/admin/classes/{id}` | 管理员 | 200 | 409 有选课记录/最后一个班 |
| GET | `/api/admin/students` | 管理员 | 200 学生数组（无 hash） | 401、403 |
| POST | `/api/admin/students` | 管理员 | 200 `{student_id}` | 400、409 |
| PUT | `/api/admin/students/{id}` | 管理员 | 200 `{student_id}` | 400、404、409 |
| DELETE | `/api/admin/students/{id}` | 管理员 | 200 | 409 有选课历史 |
| GET | `/api/admin/teachers` | 管理员 | 200 教师数组（无 hash） | 401、403 |
| POST | `/api/admin/teachers` | 管理员 | 200 `{teacher_id}` | 400、409 |
| PUT | `/api/admin/teachers/{id}` | 管理员 | 200 `{teacher_id}` | 400、404、409 |
| DELETE | `/api/admin/teachers/{id}` | 管理员 | 200 | 409 仍带班 |
| GET | `/api/admin/registration-rule` | 管理员 | 200 规则与 `round_opened`、`open_now` | 401、403 |
| PUT | `/api/admin/registration-period` | 管理员 | 200 时段 | 400 格式/先后 |
| PUT | `/api/admin/credit-limit` | 管理员 | 200 上限 | 400 已开放过 |
| POST | `/api/admin/registration-close` | 管理员 | 200 关闭 | 400 带字段 |

## 5. 状态码与错误码

| HTTP | 结果码 |
| --- | --- |
| 200 | `Success` |
| 400 | `InvalidInput`、`AuthenticationRequired`（缺会话时用 401，见下） |
| 401 | `AuthenticationRequired`（无会话或会话失效） |
| 403 | `PermissionDenied`、`CsrfRejected` |
| 404 | `RecordNotFound`、`TeachingClassNotFound`、`ActiveEnrollmentNotFound` |
| 409 | `AlreadyEnrolled`、`TimeConflict`、`TeachingClassFull`、`CapacityBelowEnrollment`、`CatalogInUse`、`DuplicateIdentifier`、`CreditLimitExceeded`、`RegistrationClosed` |
| 500 | `StorageIoError`、`StorageParseError`、`StorageSchemaMismatch`、`StorageIntegrityError`、`StorageBackupMissing` |

## 6. 本地联调

### 6.1 正式会话服务器（推荐用于复核）

`http_team4_web_server` 与公共 `SessionRuntime` 在同一个进程里，使用真实登录、Cookie 与 CSRF：

```powershell
# 1) 先给账号设初始密码（libsodium 动态包目录见文档第 2 节）
.\build\cmake-team4\Debug\password_setup.exe --state data\team4-state.json --account A2020001

# 2) 启动正式服务器（state 文件必须已存在，服务器不会替你创建）
.\build\cmake-team4\Debug\http_team4_web_server.exe `
    --host 127.0.0.1 --port 8080 --state data\team4-state.json --web-root web
```

打开 `http://127.0.0.1:8080/` 会用用户名或密码登录，再按角色进入 `/admin.html` 或
`/teacher.html`。容器里 `f` 与 `--state` 一起决定了数据文件，测试请用独立文件与虚构账号。

### 6.2 无会话的业务联调服务器（仅开发用）

`http_teacher_admin_dev_server` 按命令行指定身份并跳过真实认证，只用于快速检查业务：


```powershell
# 管理员（默认 http://127.0.0.1:8080/）
.\build\cmake-http\Debug\http_teacher_admin_dev_server.exe --role admin --id 900

# 教师（另开一个端口）
.\build\cmake-http\Debug\http_teacher_admin_dev_server.exe --role teacher --id 100 --port 8081
```

参数：`--host`、`--port`、`--state`（默认 `data/http-dev-state.json`）、
`--web-root`（默认 `web`）、`--role admin|teacher|student`、`--id`。
打开根路径会跳到 `/admin.html` 或 `/teacher.html`。

注意：

- 该服务把命令行指定的身份当成已登录，并视为 CSRF 已通过，**只用于本机**。
- 仓库路径含中文时，静态文件通过 8.3 短路径读取，避免 cpp-httplib 窄字符
  文件 API 打不开目录；首次创建状态文件时 `--state` 的父目录必须不存在或为
  新建目录（`initialize_storage` 对已存在目录会报“拒绝访问”，用默认值即可）。

## 7. 已验证的行为

- `ctest -C Debug`：`http_teacher_admin` 270 项全过（Release 同样）。
- 手动 HTTP 走查（dev server + `Invoke-WebRequest`）：
  - 管理员：`GET /api/admin/registration-rule` 200；`GET /api/admin/courses`、
    `/students`、`/classes` 200；`POST /api/admin/courses` 建课程 4 + 班 40 成功，
    `GET /classes` 立刻可见，`DELETE /courses/40` 先报“最后一个班”409，
    `DELETE /courses/4` 200 并级联删除班 40；状态文件落盘后重新载入一致。
  - 教师：`GET /api/teacher/classes` 200 只含本人班；`GET /classes/20/roster`
    403 `PermissionDenied`；管理员身份访问教师路由 403。

## 8. 修改我的密码（入口已预留，服务由张宇华提供）

密码的生成、校验与本人改密由**张宇华**负责的公共服务提供（Argon2id），三个角色共用一套流程；
本模块只在教师页与管理员页提供入口，不生成哈希、不保存任何密码。

- 页面位置：`web/teacher.html`、`web/admin.html` 顶栏的“修改我的密码”按钮，
  打开 `web/shared/app.js` 里的同一个弹窗（当前密码、新密码、确认新密码）。
  三个角色都调同一条接口，服务器按会话确定改谁的密码，不接受前端指定其他账号。
- 调用契约：`PUT /api/auth/password`，正文
  `{"current_password": "...", "new_password": "..."}`，需要有效会话与 `X-CSRF-Token`。
  成功 200；当前密码错误 400 `InvalidInput`；未登录或会话失效 401；新密码过短等规则由服务端判定并返回 400。
- 新密码规则（2026-11-28 与组长统一，前后端同一套，服务器必须再校验一次）：

  | 规则 | 说明 |
  |---|---|
  | 长度 | 至少 8 位 |
  | 字符集 | 只允许英文字母、数字和英文半角符号 |
  | 禁止字符 | 中文、中文标点、空格、制表符、换行等；不做 trim，发现就明确报错 |
  | 组合 | 至少一个大写字母、一个小写字母、一个数字；符号可用但不强制 |
  | 适用范围 | 只校验“准备设置的新密码”；当前密码按已有哈希核对，旧密码不合新规则也能登录和改密 |

- 前端在提交前用同一套规则提示（`web/shared/app.js` 的 `passwordRuleError()`），
  服务端仍是最终判定，页面不生成哈希、不保存密码。
- 错误码：当前密码错误 400 `InvalidInput`；新密码不合规则 / 字段非法 400 `InvalidInput`
  （说明不合哪一项，不回显密码）；会话过期、账号不存在、停用或身份不匹配 401
  `AuthenticationRequired`；角色或权限不足 403 `PermissionDenied`；CSRF 失败 403
  `CsrfRejected`；保存失败 500 且保留实际 `Storage*` 码；其他内部异常 500 `InternalError`。
- 顺序（2026-11-28 对齐）：密码保存成功 → 撤销该账号全部旧会话（含当前）→ **本次请求仍返回
  HTTP 200 + `Success`，并在同一响应里清除 Cookie** → 页面提示“密码修改成功，请重新登录”后回登录页。
  公共层不要在返回前再查一次会话、把成功响应覆盖成 401；后续用旧会话的新请求才返回 401。
  保存失败时不撤销会话，页面保留当前状态显示错误。
- 公共接口尚未上线时（404/501），页面提示“改密接口尚未接入”，不会假装保存成功。
- 未做（另一项确认）：管理员替学生/教师重置忘记的密码、是否强制首次登录改密；
  管理员人员列表里的“重置他人密码”按钮保持缺席。

## 9. 与公共会话骨架对接（已按 2026-11-28 方案对齐）

公共骨架提供：`require_session()`、`require_csrf()`、`with_business_lock()`、`LockedServices`、
`write_result()`，类型与函数都在 `course_registration` 命名空间，头文件 `session.hpp`。
本模块的正式接入代码在 `source_code/http_teacher_admin_session_routes.cpp`，由
`COURSE_REGISTRATION_WITH_SESSION=ON` 打开：

- 调用链：`require_session()` → `require_csrf()`（仅写方法）→ `with_business_lock()` → `dispatch()`。
- `dispatch()` 不自己加锁，调用方必须已持锁；`dispatch_locked()` 只留给独立服务器与自测，
  正式服务器不会再调它，避免同一把锁被重复获取。
- 失败映射：

  | 情况 | 状态码 | `code` |
  |---|---|---|
  | 无 Cookie、会话过期、账号停用/删除、账号不匹配 | 401 | `AuthenticationRequired` |
  | 角色不符 | 403 | `PermissionDenied` |
  | CSRF 缺失或不匹配 | 403 | `CsrfRejected` |
  | 业务冲突（满班、重号、容量不足、有选课记录等） | 409 | 原 `ResultCode` 名 |
  | 参数问题 | 400 | `InvalidInput` |
  | 记录不存在 | 404 | `RecordNotFound` 等 |
  | 存储失败（保存不成功） | 500 | 实际 `Storage*`（如 `StorageIoError`） |
  | 其他内部异常 | 500 | `InternalError`（详情只留在服务器端，不进响应） |

- 锁内读取用新增的 `LockedServices::context()`（`const SystemContext&`），
  `context_snapshot()` 保留给需要在锁外继续处理的情况；离开回调前把要保留的数据复制走，
  响应只逐字段挑选（`student_json`/`teacher_json` 等），不含密码哈希。
- `with_business_lock()` 的返回值只表示加锁后的身份 / 角色检查是否通过，不替代回调里的业务结果
  （满班仍由业务结果给 409）。
- 会话口径：空闲 30 分钟、登录后最长 8 小时；只有通过身份检查的 API 请求才刷新空闲时间，
  静态页面与图片不续期，最长时限从登录成功起算。

正式适配器的写法（`source_code/http_teacher_admin_session_routes.cpp`）：

```cpp
// 1) 会话：无 Cookie、过期、账号停用/改号都由公共层判定，失败即 401。
const SessionCheck session = require_session(raw);
if (!session.identity) { write_result(out, session.result, ResultSource::Session); return; }

// 2) CSRF：只对写方法检查，失败 403 CsrfRejected。
if (is_write_method(raw.method)) {
    const OperationResult csrf = require_csrf(raw);
    if (csrf.code != ResultCode::Success) { write_result(out, csrf, ResultSource::Csrf); return; }
}

// 3) 业务：公共层持锁后回调，回调里调用 dispatch()，不调用 dispatch_locked()。
http::Response response;
const OperationResult locked = with_business_lock(*session.identity, role_for_path(raw.path),
    [&](LockedServices& locked) {
        Services services{locked.context(), locked.admin_service(), locked.teacher_service()};
        response = dispatch(to_request(raw), services);
    });

// 4) 锁内身份失效/存储失败/内部异常按来源映射，不能一律当成 401。
if (locked.code != ResultCode::Success) {
    write_result(out, public_result(locked), result_source_for(locked.code));
    return;
}
write_response(out, response);
```

`Services` 里已经没有 `std::mutex&` 成员：锁由公共层持有，handler 若再拿一把锁就是重复加锁。
`result_source_for()` 的映射（`header_files/http_teacher_admin_session_routes.hpp`）：

| `ResultCode` | `ResultSource` | HTTP |
|---|---|---|
| `AuthenticationRequired` | `Session` | 401 |
| `PermissionDenied` | `Permission` | 403 |
| 五个 `Storage*` | `Business` | 500，保留错误码 |
| 其它 | `Internal` | 500 `InternalError` |

## 10. 复核后的修改（2026-11-29）

按组长 2026-11-28 的复核意见改了六处，全部有测试或脚本覆盖：

1. **改密弹窗打不开**：`web/shared/app.js` 的 `backdrop.innerHTML` 少写了一个 `+`，
   页面只收到提示文字前半段，保存/取消按钮没生成。已补上，并新增
   `web/tests/app_text_test.js`：把模板表达式取出来实际求值，断言三个输入框与两个按钮都在。
2. **教室名被当成 HTML**：`meetingText()`/`meetingsText()` 的输出契约改为“已转义、可直接放进
   `innerHTML`”，`room` 统一过 `esc()`；管理员教学班列表、教师教学班列表、教师班级详情三处都走
   这两个函数。测试用 `<b>TestRoom</b>`、`A&B "x" > y` 等字符串断言不出现裸标签。
3. **锁内身份失效返回 500**：公共层在锁内复核身份失败时回 `AuthenticationRequired`，
   适配器现在映射成 401；只有无法识别的失败才落到 `InternalError`（500）。
4. **保存失败泄露路径**：`public_result()` 把五个 `Storage*` 的 message 换成
   “storage failure: nothing was changed; check the state file and retry”，错误码不变；
   `from_result()` 与正式适配器写回都经过它。测试独占主文件后断言响应不含 `state.json`/`C:`。
5. **重复 JSON 字段**：`body_to_object()` 用 `json::parse` 回调按层查重，
   `{"capacity":0,"capacity":2}` 返回 400 `InvalidInput`，不执行业务、不保存。
   注意清的是 `depth + 1` 这一层：键事件比对象开始深一层，清错层会把上一个对象的键留下，
   正常请求（`course.course_id` 与 `first_teaching_class.course_id`）就会被误判。
6. **顶栏不假定 `name`**：`/api/auth/me` 的 user 只有 `role`/`user_id`/`account_number`，
   顶栏改为 `roleText(role) + 账号 + （用户编号 id）`。

## 11. 已知限制

- 学生端路由与选退课不在本模块；正式服务器只为教师与管理员页面提供入口，
  学生登录后页面会提示“学生页面尚未接入这个服务器”。
- 改密弹窗已接 `PUT /api/auth/password`，但界面行为只做了脚本级回归（模板、规则、错误提示），
  真实浏览器里的回车键、焦点顺序未做自动化验证。
- 学生端页面与选退课接口不在本模块（Kai / Yuhua）。
- 管理员账号维护页未做（手册列为待确认范围），当前只维护学生与教师资料。
- 停用教师的口径已确认（列表隐藏 + 直接提交班号拒绝，两者必须同时生效），
  但“新一轮”判定与选课拦截的接入方式仍待与 Yuhua / Kai 对齐。
- 排课时间只校验 `HH:MM` 形式的分钟数与先后，不校验学期起止日期。
