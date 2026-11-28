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

## 4. 公共接口待对齐点（Yuhua 的 session 草案）

1. 头文件名与命名空间（我按 `session.hpp`、全局作用域预留）。
2. `with_business_lock()` 中 `fn` 的返回类型：我按 `void` + 捕获 `Response` 使用；
   若要求返回 `OperationResult`，回调内业务失败与锁失败需要可区分。
3. `require_session()` 是否顺带续期空闲时间（30 分钟口径依赖这一点），
   “无 Cookie”和“会话过期/账号停用”是否都返回 401 `AuthenticationRequired`。
4. `require_csrf()` 对 GET 是否 no-op；失败时 `ResultSource::Csrf` 对应 403 `CsrfRejected`。
5. `context_snapshot()` 拷贝整份数据（含哈希）：我在锁内直接用真实引用转换 JSON，
   只在需要脱离锁的只读路径使用快照。
