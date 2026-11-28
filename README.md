# Improved University Course Registration System

## 改进型大学选课系统 · Team 4

本项目是 Team 4 的软件工程课程小组项目，面向学生、教师和管理员，提供课程查询、选课退课、个人课表、教学班查询及课程管理等功能。

当前为**三人阶段整合后的管理员模块加固版本**，状态更新于 **2026 年 11 月 29 日**。已接入三人的核心业务模块、控制台演示和六套自动化测试；管理员模块的持久化、凭据保留、失败路径验收与 libsodium 构建约束已完成。完整网页、登录认证仍未接入。

## 团队分工

| 成员 | 主要负责范围 |
| --- | --- |
| Zhang Yuhua（组长） | 选课、退课和核心业务规则，后续登录接入，以及代码整合与协调 |
| Zhang Yikai | 课程搜索与详情、剩余名额查询、个人课表及对应测试 |
| Zhu Ziliang | 教师查询、管理员维护、角色权限规则及共享数据接口 |

具体改动及修复过程以 Git 提交记录为准，团队通过飞书记录进度、测试和缺陷。

## 当前功能

| 模块 | 已有业务能力 | 主要入口 |
| --- | --- | --- |
| 学生选课与退课 | 检查选课时段、同课程重复选课、上课时间冲突、学分上限和容量；退课释放名额，支持重新选课 | `RegistrationService::enroll` / `drop` |
| 学生课程查询 | 按课程代码、名称或教师姓名搜索，按课程代码、学分、教师筛选，查看课程详情 | `StudentQueryService::search_courses` / `course_detail` |
| 名额与课表 | 查询教学班容量、有效人数和剩余名额；根据有效选课记录生成本人课表 | `StudentQueryService::view_available_seats` / `view_timetable` |
| 教师查询 | 查看本人教学班、选课人数与学生名单；管理员可以查看全部教学班 | `TeacherService::assigned_teaching_classes` / `class_summary` / `class_roster` |
| 管理员维护 | 维护课程、教学班、容量、选课时段、学分上限和学生教师资料，检查部分数据一致性 | `AdministratorService` |

以上功能通过测试和演示程序调用，尚未构成可供真实用户使用的完整系统。已知问题及未完成项见下文。

## 共享数据与接口约定

`Course` 表示课程，`TeachingClass` 表示该课程的具体教学班。学生选择的是教学班，`Enrollment` 保存学生与教学班之间的选退课记录。

- 三个模块使用同一个 `SystemContext` 对象。课程、教学班、选课记录和规则由其中的 `RegistrationService` 持有，不另建可独立修改的数据源。
- 教师名单和人数、学生余量及课表都依据有效（`Active`）选课记录计算。退课后的 `Dropped` 记录保留，但不计入当前人数和课表。
- 剩余名额按 `capacity - active_enrollments` 计算，不另存一个需要同步的名额计数器。
- 选课和退课会自行判断当前是否开放，不需要调用方先手动刷新时段。页面及其他模块读取实时状态时使用 `context.registration_window_open()`，不要将保存的 `registration_open` 字段当作实时状态。
- 学生查询接口与教师 `class_summary()` 失败时清空输出，但调用方仍须先检查 `OperationResult::code`，仅在成功时展示结果。
- 课程搜索同时指定教师姓名关键词与教师筛选时，教师匹配必须来自同一个教学班；课程代码或名称本身命中关键词时，再按教师筛选收窄教学班。
- 登录尚未接入，`UserIdentity` 目前由测试与演示构造。后续请求入口须验证学生存在且账号启用，并使用登录身份对应的学生编号，防止替他人选退课。

接口资料见 [共享接口说明](documentation/interfaces.md)、[学生查询模块说明](documentation/student_query_module.md) 和 [接口变更说明](documentation/interface_change_request.md)。时段刷新文档中的早期方案已明确标记为废弃，当前选退课按请求时刻自行判断。

## 项目结构

```text
header_files/        选课规则、教师管理员、学生查询的结构与接口声明
source_code/         三个业务模块实现及各自的控制台演示
automated_tests/     七套业务、持久化与 HTTP 接口测试（含第 4 周失败路径验收）
documentation/      接口说明、决策记录、失败矩阵、HTTP 说明和测试资料
web/                 教师端、管理员端页面（原生 JS + fetch）
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
  所以完整 7 个测试请用 CMake + Visual Studio 2022 生成器。
- 只想跑业务逻辑（不含持久化）时，`g++`（GCC 8.1+）也够用：`build_and_test.ps1`
  会自动跳过在此环境下无法编译的持久化目标。

### 推荐：CMake + MSVC 2022（全部 7 个测试）

```powershell
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64
cmake --build build\cmake --config Debug
ctest --test-dir build\cmake -C Debug --output-on-failure
```

预期 7/7 通过：`course_registration_business_rules`、`teacher_and_administration_services`、
`student_query_and_timetable`、`persistence`、`persistent_administration`、
`admin_failure_acceptance`（第 4 周的失败路径验收，309 项检查）、
`http_teacher_admin`（第 5 周的教师/管理员 HTTP 接口，270 项检查）。

需要密码哈希（Argon2id）时按 `documentation/account_onboarding_and_libsodium_build.md`
开启 `-DCOURSE_REGISTRATION_USE_SODIUM=ON`，验收范围仅限 Windows x64 + MSVC 2022
官方**动态**包。

### 备选：g++ 脚本（业务逻辑部分）

```powershell
.\build_and_test.ps1
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

## 本次整合与验证

本次阶段整合接入以下开发记录，采用合并提交保留历史，不通过重新上传文件替代成员原有提交：

| 来源分支 | 本次接入版本 |
| --- | --- |
| `zhang-yuhua-registration-rules` | `88f0ccd` |
| `feature/zhu-ziliang-teacher-admin` | `f5b1cdb`，含 006～010 的修复及补充测试 |
| `feature/zhang-yikai-student-query` | `5940ee2` |

2026 年 11 月 6 日，在组织仓库本地整合目录重新编译并运行三套现有测试，全部通过。其中教师管理员测试通过 326 项检查。实际整合版本的补充回归与联调通过 44 项检查，学分上限锁定通过 32 项检查，覆盖搜索组合、查询失败清空结果、选退课联动、容量和时段规则，以及无人或有人选课时改期仍不得解除学分上限锁定的场景。

联调验证的主要流程为：管理员设置容量为 1，学生 A 选课成功，学生 B 因满员被拒绝；教师名单和人数、学生余量与课表保持一致；A 退课后名额恢复，名单和课表同步更新。

此前登记的 001～010 已按复现步骤完成修复与复核，状态见下表。测试通过仅表示已有断言通过，不代表所有业务规则或输入场景都已覆盖。

## 缺陷修复与复核

编号对应团队缺陷跟踪记录。以下状态属于本次整合版本，后续修复需在复测后更新。

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
