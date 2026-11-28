# 教师 / 管理员 HTTP 路由清单（Zhu Ziliang）

对照《Team4_HTTP_团队总手册》表 6 逐条列全：同一路径的 GET 与 POST 分别算一条。
角色列只写“必需角色”；未登录一律 401，角色不符一律 403。写操作另需 `X-CSRF-Token`。

## 1. 教师（3 条）

| # | 方法 | 路径 | 请求字段 | 角色 | 业务方法 | 成功 data |
|---|---|---|---|---|---|---|
| 1 | GET | `/api/teacher/classes` | 无 | Teacher | `TeacherService::assigned_teaching_classes` | 本人教学班摘要数组 |
| 2 | GET | `/api/teacher/classes/{id}/summary` | 路径 `id` | Teacher | `TeacherService::class_summary` | 一个班级摘要（人数/容量/余量/时间地点） |
| 3 | GET | `/api/teacher/classes/{id}/roster` | 路径 `id` | Teacher | `TeacherService::class_roster` | 名单（学号、姓名，按学号升序） |

读他人班级：403 `PermissionDenied`，响应不含名单数据。

## 2. 管理员（21 条）

| # | 方法 | 路径 | 请求字段 | 角色 | 业务方法 | 成功 data |
|---|---|---|---|---|---|---|
| 4 | GET | `/api/admin/courses` | 无 | Administrator | `assigned_teaching_classes(admin)` 后按课程分组 | 课程数组，每项含 `course` 与 `teaching_classes` |
| 5 | POST | `/api/admin/courses` | `course{course_id,course_code,course_name,credits}`、`first_teaching_class{teaching_class_id,course_id,teacher_id,capacity,meetings[]}` | Administrator | `create_course_with_teaching_class` | `{course_id, teaching_class_id}` |
| 6 | PUT | `/api/admin/courses/{id}` | `course_id`、`course_code`、`course_name`、`credits` | Administrator | `update_course` | `{course_id}` |
| 7 | DELETE | `/api/admin/courses/{id}` | 路径 `id` | Administrator | `remove_course` | `null` |
| 8 | GET | `/api/admin/classes` | 无 | Administrator | `assigned_teaching_classes(admin)` | 全部教学班摘要数组 |
| 9 | POST | `/api/admin/classes` | `teaching_class_id`、`course_id`、`teacher_id`、`capacity`、`meetings[]` | Administrator | `create_teaching_class` | `{teaching_class_id}` |
| 10 | PUT | `/api/admin/classes/{id}` | 同上五个字段，`teaching_class_id` 必须等于路径 | Administrator | `update_teaching_class` | `{teaching_class_id}` |
| 11 | PATCH | `/api/admin/classes/{id}/capacity` | `capacity` | Administrator | `set_teaching_class_capacity` | `{teaching_class_id, capacity, active_enrollments, remaining_seats}` |
| 12 | DELETE | `/api/admin/classes/{id}` | 路径 `id` | Administrator | `remove_teaching_class` | `null` |
| 13 | GET | `/api/admin/students` | 无 | Administrator | `SystemContext::students()` | 学生数组（不含密码哈希） |
| 14 | POST | `/api/admin/students` | `student_id`、`student_number`、`name`、`major`、`account_active` | Administrator | `create_student` | `{student_id}` |
| 15 | PUT | `/api/admin/students/{id}` | 同上五个字段，`student_id` 必须等于路径 | Administrator | `update_student` | `{student_id}` |
| 16 | DELETE | `/api/admin/students/{id}` | 路径 `id` | Administrator | `remove_student` | `null` |
| 17 | GET | `/api/admin/teachers` | 无 | Administrator | `SystemContext::teachers()` | 教师数组（不含密码哈希） |
| 18 | POST | `/api/admin/teachers` | `teacher_id`、`teacher_number`、`name`、`department`、`account_active` | Administrator | `create_teacher` | `{teacher_id}` |
| 19 | PUT | `/api/admin/teachers/{id}` | 同上五个字段，`teacher_id` 必须等于路径 | Administrator | `update_teacher` | `{teacher_id}` |
| 20 | DELETE | `/api/admin/teachers/{id}` | 路径 `id` | Administrator | `remove_teacher` | `null` |
| 21 | GET | `/api/admin/registration-rule` | 无 | Administrator | `SystemContext::rule()` + `round_opened()` | `registration_open`、`maximum_credits`、`opens_at`、`closes_at`、`round_opened`、`open_now` |
| 22 | PUT | `/api/admin/registration-period` | `opens_at`、`closes_at`（`YYYY-MM-DDTHH:MM`） | Administrator | `set_registration_period` | 更新后的规则 |
| 23 | PUT | `/api/admin/credit-limit` | `maximum_credits` | Administrator | `set_maximum_credits` | 更新后的规则 |
| 24 | POST | `/api/admin/registration-close` | 空对象 `{}` | Administrator | `close_registration` | 更新后的规则 |

`meetings[]` 每项：`day_of_week`（1–7）、`start_minute`、`end_minute`、`room`；数组不能为空。

## 3. 覆盖核对（对照团队总手册表 6）

| 表 6 条目 | 本文档 | 状态 |
|---|---|---|
| `GET/POST /api/teacher/classes` 等教师 3 条 | 1–3 | 覆盖 |
| `GET POST /api/admin/courses` | 4–5 | 覆盖 |
| `PUT DELETE /api/admin/courses/{id}` | 6–7 | 覆盖 |
| `GET POST /api/admin/classes` | 8–9 | 覆盖 |
| `PUT DELETE /api/admin/classes/{id}` | 10、12 | 覆盖 |
| `PATCH /api/admin/classes/{id}/capacity` | 11 | 覆盖 |
| `GET POST /api/admin/students` | 13–14 | 覆盖 |
| `PUT DELETE /api/admin/students/{id}` | 15–16 | 覆盖 |
| `GET POST /api/admin/teachers` | 17–18 | 覆盖 |
| `PUT DELETE /api/admin/teachers/{id}` | 19–20 | 覆盖 |
| `GET /api/admin/registration-rule` | 21 | 覆盖 |
| `PUT /api/admin/registration-period` | 22 | 覆盖 |
| `PUT /api/admin/credit-limit` | 23 | 覆盖 |
| `POST /api/admin/registration-close` | 24 | 覆盖 |

结论：表 6 的 21 条管理员路由与 3 条教师路由全部实现，无遗漏、无额外路径。
不实现的相邻路径：`/api/courses`、`/api/classes/{id}/seats`、`/api/me/*`（学生侧，Kai / Yuhua），`/api/auth/*`（登录，Yuhua）。

## 4. 公共接口对齐结果（2026-11-28，按组长方案）

| 待对齐点 | 采用 |
|---|---|
| 头文件与命名空间 | `session.hpp`，类型与函数放在 `course_registration` 命名空间 |
| `with_business_lock()` 回调返回类型 | 返回 `void`，业务响应用捕获变量带出；它返回的 `OperationResult` 只表示加锁后的身份 / 角色检查，不替代回调里的业务结果（满班仍是 409） |
| 持锁后的失败映射 | 停用 / 删除 / 账号不匹配 → 401 `AuthenticationRequired`；角色不符 → 403 `PermissionDenied`；业务冲突保持 409；保存失败 500 保留实际 `Storage*`；其他内部异常 500 `InternalError`（`ResultSource::Internal`） |
| `require_session()` 的续期 | 只有通过检查的 API 请求才刷新空闲时间；静态页面与图片不续期；最长 8 小时从登录成功起算、不延长 |
| `require_csrf()` | GET / HEAD / OPTIONS 直接通过；写操作（含退出登录、改密）必查；登录接口单独处理 |
| `context_snapshot()` | 保留给锁外只读；锁内改用新增的 `LockedServices::context()` 直接读取，离开回调前把要保留的数据复制走，响应不含密码哈希 |
| 加锁位置 | 正式路由在锁内调用 `dispatch()`；`dispatch_locked()` 只留给独立服务器与自测，避免重复加锁 |

对应的接入代码在 `source_code/http_teacher_admin_session_routes.cpp`（`COURSE_REGISTRATION_WITH_SESSION=ON`
时编译），调用链是 `require_session()` → `require_csrf()` → `with_business_lock()` → `dispatch()`。

接入注意：`with_business_lock()` 失败时不能一律当 401。本模块按 `PermissionDenied` → 403、`Storage*` → 500 保留原码、其余 → `ResultSource::Internal`（500 `InternalError`）处理；若锁内识别到身份失效，请在返回值里给出可区分的结果，我再补 401 分支。`PUT /api/auth/password` 只由公共 HTTP 服务注册，本模块注册 `/api/auth/*` 会重复，自测里已断言这里返回 404。

密码相关的公共服务（Argon2id 生成、校验、本人改密）由**张宇华**负责；本模块只在页面提供入口，
`PUT /api/auth/password` 的会话撤销与会话语义也以公共实现为准。
