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

## 8. 已知限制

- 未接入登录：会话身份、CSRF 令牌、登录页与启动参数由组长骨架提供。
- 学生端页面与选退课接口不在本模块（Kai / Yuhua）。
- 管理员账号维护页未做（手册列为待确认范围），当前只维护学生与教师资料。
- 停用教师的口径已确认（列表隐藏 + 直接提交班号拒绝，两者必须同时生效），
  但“新一轮”判定与选课拦截的接入方式仍待与 Yuhua / Kai 对齐。
- 排课时间只校验 `HH:MM` 形式的分钟数与先后，不校验学期起止日期。
