# Improved University Course Registration System

## 改进型大学选课系统 · Team 4

当前为**三人阶段整合后的教师 / 管理员模块版本**，状态更新于 **2026 年 11 月 29 日**。已接入三人的核心业务模块、控制台演示、教师与管理员 HTTP 接口及页面、公共会话登录与本人改密。公共骨架来自组长 `c7c5080`，本分支在 `cb51bce` 合入；教师 / 管理员接口与页面在 `5a3b5c5`。Debug 与 Release 各 19 套自动化测试全部通过。学生页面与选退课路由仍由 Kai / Yuhua 提供。

| 成员 | 本次成果 | 原始提交 |
| --- | --- | --- |
| Zhang Yuhua | 选退课持久化、学生身份检查、选课学分溢出修复、验收与整合 | `18b44c1`，含 `bf689e8` 验收 |
| Zhang Yikai | 课程查询、课表、共享 JSON 保存、恢复与完整性校验 | `88f8d99` 及此前查询提交 |
| Zhu Ziliang | 管理员修复、课程与首班原子创建、管理员持久化入口、CMake 配置 | `fb41e2f`，含 `9d1c07c`、`d59c475` 及此前修复 |

采用合并提交保留各人原始历史。第一周说明保存在 [历史 README](documentation/week1_integration_README_2026-11-06.md)，其中状态与测试数属于 11 月 6 日。

## 共享状态与入口

三端共用一个 `SystemContext`。人数、余量和课表从 Active 记录派生，Dropped 历史保留。

- 学生修改调用 `PersistentRegistrationService::enroll/drop`。
- 管理员修改调用 `PersistentAdministratorService`；新课程使用 `create_course_with_teaching_class`。
- 查询继续使用 `StudentQueryService`、`TeacherService` 读取同一上下文。
- 修改请求先检查当前身份，在副本执行业务，保存成功后发布；失败不发布副本。
- 身份必须由后续可信登录会话提供；演示中的 UserIdentity 不构成真实认证。
- 保存与恢复使用 Kai 的 `initialize_storage/load_state/save_state/restore_state`，默认 `data/state.json`。
- 登录账号跨角色查重，统一去首尾空白并转小写；没有增加内部数字 ID 跨角色唯一要求。

## 构建与验收

## 项目结构

```text
header_files/        选课规则、教师管理员、学生查询的结构与接口声明
source_code/         三个业务模块实现及各自的控制台演示
automated_tests/     九套业务、持久化、HTTP、会话与端到端测试
documentation/      接口说明、决策记录、失败矩阵、HTTP 说明和测试资料
web/                 登录页、教师端、管理员端页面（原生 JS + fetch）与前端回归脚本
third_party/         cpp-httplib、nlohmann/json（版本与哈希见其 README）
docs/task3/         Task 3 图、可编辑源文件与配套说明
weekly/             阶段开发记录
build_and_test.ps1   编译全部测试与演示，并运行当前环境可用的测试
CMakeLists.txt       CMake 构建配置（MSVC 2022 或 g++）
README.md           项目概况、运行方法与当前状态
```

组织仓库最初的说明保存在 [organization_repository_original_README.md](documentation/organization_repository_original_README.md)，原始提交也保留在 Git 历史中。

## 编译、测试与演示

### 环境要求

- Windows PowerShell。
- 目标编译器 **MSVC 2022（19.40+）+ CMake 3.21+**：持久化相关的两个测试用到
  `<filesystem>`，MinGW GCC 8 在 Windows 上会因 `path` 的宽字符类型编译失败，
  所以完整 19 个测试请用 CMake + Visual Studio 2022 生成器。
- 只想跑业务逻辑（不含持久化）时，`g++`（GCC 8.1+）也够用：`build_and_test.ps1`
  会自动跳过在此环境下无法编译的持久化目标。

### 推荐：CMake + MSVC 2022（全部 19 个测试）

```powershell
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 `
    -DCOURSE_REGISTRATION_USE_SODIUM=ON -DCOURSE_REGISTRATION_WITH_SESSION=ON `
    "-DSODIUM_ROOT=<libsodium 包根目录>"
cmake --build build\cmake --config Debug
ctest --test-dir build\cmake -C Debug --output-on-failure
```

预期 19/19 通过。业务与持久化：`course_registration_business_rules`、
`teacher_and_administration_services`（505 项）、`student_query_and_timetable`、`persistence`、
`persistent_administration`（134 项）、`admin_failure_acceptance`（309 项）、
`registration_credit_limit`、`registration_cross_module_acceptance`、
`registration_storage_integration`、`shared_persistence_acceptance`、`student_interface`、
`login_service`、`password_service`、`session`。
HTTP 与页面：`http_teacher_admin`（274 项 handler 检查）、
`http_team4_integration`（166 项真实会话端到端）、`web_text_regression`（18 项前端回归，
需要 Node.js，未安装时不注册该项）。

需要密码哈希（Argon2id）时按 `documentation/account_onboarding_and_libsodium_build.md`
开启 `-DCOURSE_REGISTRATION_USE_SODIUM=ON`，验收范围仅限 Windows x64 + MSVC 2022
官方**动态**包。

### 备选：g++ 脚本（业务逻辑部分）

```powershell
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1 -Configuration Release
```

脚本会创建 `build` 目录，编译各测试与演示程序并运行可用的测试，全部通过时返回
退出码 `0`。当前各套件的检查数：教师与管理员 505 项、管理员持久化 134 项、
失败路径验收 309 项；业务规则、学生查询与课表、持久化三套以“全部通过”为结果，
不单独打印计数（后两套中的持久化目标需要 MSVC 才能编译）。

### 演示程序

```powershell
.\build\course_registration_console_demonstration.exe      # 选课与退课
.\build\teacher_and_administration_demonstration.exe       # 教师、管理员及选退课联动
.\build\student_query_demonstration.exe                    # 学生搜索、余量与课表
```

### 网页与真实登录

```powershell
# 先给账号设初始密码（写进独立的状态文件，不要用真实凭据）
.\build\cmake-team4\Debug\password_setup.exe --state data\team4-state.json --account A2020001

# 同进程一个 SessionRuntime：登录、当前身份、退出、本人改密 + 教师/管理员路由
.\build\cmake-team4\Debug\http_team4_web_server.exe `
    --host 127.0.0.1 --port 8080 --state data\team4-state.json --web-root web
```

浏览器打开 `http://127.0.0.1:8080/`，按角色进入 `/admin.html` 或 `/teacher.html`；
顶栏“修改我的密码”调用公共 `PUT /api/auth/password`，成功后回到登录页。

## 本次整合与验证

本次阶段整合接入以下开发记录，采用合并提交保留历史，不通过重新上传文件替代成员原有提交：

| 来源分支 | 本次接入版本 |
| --- | --- |
| 原选课核心、学生查询、Kai 持久化 | Debug 均通过 |
| 教师与管理员 | 483 checks，0 failures |
| 选课学分溢出回归 | 10 checks 通过 |
| 原跨模块选退课验收 | 574 checks，9 场景通过 |
| 选退课存储验收 | 315 checks，7 场景通过 |
| 管理员持久化 | 89 checks，0 failures |
| 三端共享持久化 | 26 checks 通过 |

## 当前边界与待确认

启用官方动态 libsodium 后，现在可构建真实密码校验服务和本机 `password_setup` 工具。
本机密码设置作为暂行方案，最终账号初始化和重置办法仍待讨论，后续可以更换。
编译、使用步骤及失败处理见 [本机密码设置说明](documentation/local_password_setup.md)。

公共会话与本人改密实现见 [公共 HTTP 接入说明](documentation/session_integration.md)。
打开 `COURSE_REGISTRATION_WITH_SESSION` 和 libsodium 后，可构建统一登录、当前身份、退出和本人改密 API，
并使用 `run_session_acceptance.ps1` 验证。`session.hpp` 位于 `header_files`，命名空间为 `course_registration`。
教师 / 管理员路由与页面的接入方式、路由表与复核结果见
[教师 / 管理员 HTTP 接口](documentation/http_teacher_admin.md) 与
[复核回复](documentation/review_2026-11-29_http_session.md)。学生页面与选退课路由仍待 Kai / Yuhua 接入。

原班教师停用后，管理员能否继续改该班时间和容量，用户于 11 月 19 日选择继续待讨论。候选版本保留当前允许行为及已有检查，不将其记录为已确认政策；新建班或改派给停用教师仍拒绝。内部数字 ID 跨角色唯一、课程归档/停用、管理员账号维护页面均未在此次整合中扩展。

业务采用单进程共享状态。公共 HTTP 入口已提供共同加锁，组员路由仍须按说明接入同一运行对象，并完成三端浏览器流程及整合后的并发回归。本阶段不代表性能或断电耐久性验收。运行数据、临时快照、备份、exe 与 build 目录不进入提交。

| 编号 | 问题 | 当前状态 |
| --- | --- | --- |
| 001 | 时段结束后仍允许选课 | 已修复并复测通过，请求时重新判断当前时间 |
| 002 | 修改教学班所属课程绕过重复选课与学分约束 | 已修复并复测通过，有有效选课时拒绝修改 `course_id` |
| 003 | 手动关闭后刷新时段又重新开放 | 已修复并复测通过，手动关闭会清空原时段 |
| 004 | 教师关键词与教师筛选由不同教学班满足 | 已在 `5940ee2` 修复，组合搜索测试通过 |
| 005 | 查询失败后保留上一次的课程与余量结果 | 已在 `fee6666` 修复，失败清空及回归测试通过 |
| 006 | 删除学生或教师后，成功提示可能显示另一人的编号 | 已修复并复测通过，删除前保存编号，不再读取失效指针 |
| 007 | 教师 `class_summary()` 失败时仍保留旧结果，与学生查询约定不一致 | 已修复并复测通过，越权及找不到教学班时清空输出；调用方仍检查结果码 |
| 008 | 时段刷新接口的头文件注释仍要求请求前手动刷新 | 已同步头文件和文档，早期方案明确标为废弃 |
| 009 | 时段校验接受不存在的日期，如 `2026-02-30` | 已修复并复测通过，按月份和闰年检查天数，非法日期不改变原规则 |
| 010 | 本轮选课开放后，管理员仍可修改学分上限 | 已在 `f5b1cdb` 完成修复并复测：本轮首次开放后锁定上限，暂停、结束或改到未来均不能解锁，与是否有人选课无关 |

010 的第一轮修复仍可通过改期绕过；本次在覆盖旧时段前先保存已开放事实，覆盖后再观察新时段，相关回归测试通过。首次开放后拒绝修改上限，既有选课记录保持不变。学分上限的具体数值仍由项目需求及配置决定，演示中的数字不代表固定业务要求。

## 第 4 周（11/23–11/29）失败路径验收与修复

按手册要求覆盖容量调整、已有选课记录、重复账号、无效时段、越权访问五类失败场景，
共 309 项检查；每个被拒操作都断言返回码、可读消息，并比对操作前后的
`SystemContext` 全量快照，确认“失败不破坏原数据”。

- 场景清单与口径：[管理员模块失败矩阵](documentation/admin_failure_matrix_2026-11-29.md)
- 运行输出：[失败路径验收证据](documentation/admin_failure_acceptance_evidence_2026-11-29.txt)

本轮测试新发现并修复两个缺陷：

| 编号 | 问题 | 当前状态 |
| --- | --- | --- |
| 011 | 只剩 Dropped 记录时仍可把教学班改挂到别的课程，历史选课随之改写 | 已修复，`dropped > 0` 时拒绝改 `course_id`；旧用例改为断言拒绝，并补“无记录班级可改课程”正例 |
| 012 | 课程代码查重只做小写转换、不去首尾空白，`" se101 "` 可绕过 `"SE101"` | 已修复，查重改用 `normalize_account_key()`，与账号规则一致；删除不再使用的 `to_lower` |

验收结果（MSVC 2022 / CMake 3.30.5，Debug 与 Release 各一轮）：
`ctest` 6/6；失败路径验收 309 项、教师与管理员 505 项、管理员持久化 134 项全部通过。

## 第 5 周（11/30–12/2）HTTP 接口与页面

按两份 HTTP 手册实现教师端与管理员端的接口和页面，接入点只依赖组长提供的
「会话身份 + CSRF 结果」回调，其余（路由、权限、JSON 校验、持久化保存）都在本模块内完成。

- 路由：教师 3 条、管理员 16 条，统一 `/api/teacher`、`/api/admin` 前缀与
  `{code, message, data}` 外壳，状态码映射见
  [HTTP 接口与页面说明](documentation/http_teacher_admin.md)。
- 页面：`web/teacher.html`（本人教学班、班级摘要、学生名单）、`web/admin.html`
  （课程与教学班增删改、容量、学生/教师资料、选课规则）。
- 测试：`http_teacher_admin` 270 项，覆盖 401/403/404/400/409、CSRF、停用账号旧会话、
  未知字段、64 KiB 上限、URL 与正文编号不一致，以及“写入成功后重新载入仍一致”。
- 本地联调：`http_teacher_admin_dev_server.exe --role admin --id 900`（仅本机、
  不做密码校验，产品入口仍由组长提供）。
- 公共会话骨架对接：`source_code/http_teacher_admin_session_routes.cpp`
  （`-DCOURSE_REGISTRATION_WITH_SESSION=ON`，需要张宇华的 `session.hpp`），
  调用链 `require_session()` → `require_csrf()` → `with_business_lock()` → `dispatch()`，
  401/403/409/500 的职责划分见 `documentation/http_teacher_admin_routes.md` 第 4 节。
- 修改本人密码：页面已预留入口，统一调 `PUT /api/auth/password`；密码的生成、校验与改密
  公共服务由张宇华负责，本模块不生成哈希、不保存密码。
- 契约与确认项：`documentation/decisions/0001-http-contract.md`。

本轮 ctest 7/7（新增 `http_teacher_admin`），Debug 与 Release 各一轮。

## 后续工作

1. 维护现有回归测试，后续接口或业务规则变更时同步更新文档。
2. 接入登录、账号状态校验及可信的用户身份（组长负责），把权限规则接到登录结果上。
3. 教师端、管理员端页面已在本模块提供（`web/`），待组长登录与会话骨架就绪后联调；
   学生端页面与选退课接口由 Kai / Yuhua 负责。
4. 学生可选列表过滤停用教师、选课入口拒绝停用教师班级：已确认，待实现和联调；
   原班教师停用后允许改时间/容量（保留容量下界与冲突检查，2026-11-28 已确认）。
5. 每周向组织仓库同步复核后的阶段成果，并更新测试、缺陷与开发记录。

Task 3 中 Zhu 的图与说明见 [图清单](docs/task3/FIGURES.md)，Yikai 的图、源文件和说明见 [member_b](docs/task3/member_b/README.md)。已有文档和图仍需随实际接口变更同步维护。
