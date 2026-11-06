# Improved University Course Registration System

## 改进型大学选课系统 · Team 4

本项目是 Team 4 的软件工程课程小组项目，面向学生、教师和管理员，提供课程查询、选课退课、个人课表、教学班查询及课程管理等功能。

当前为**第一周 C++ 内存业务原型的三人阶段整合版本**，状态更新于 **2026 年 11 月 6 日**。已接入三人的核心业务模块、控制台演示和自动化测试；完整网页、登录认证和数据持久化尚未完成。

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
- 学生查询接口失败时清空输出，但调用方仍须先检查 `OperationResult::code`，仅在成功时展示结果。教师查询的失败输出约定尚待统一，见缺陷-007。
- 课程搜索同时指定教师姓名关键词与教师筛选时，教师匹配必须来自同一个教学班；课程代码或名称本身命中关键词时，再按教师筛选收窄教学班。
- 登录尚未接入，`UserIdentity` 目前由测试与演示构造。后续请求入口须验证学生存在且账号启用，并使用登录身份对应的学生编号，防止替他人选退课。

接口资料见 [共享接口说明](documentation/interfaces.md)、[学生查询模块说明](documentation/student_query_module.md) 和 [接口变更说明](documentation/interface_change_request.md)。部分早期接口注释仍待同步，以本次状态说明及实际代码为准。

## 项目结构

```text
header_files/        选课规则、教师管理员、学生查询的结构与接口声明
source_code/         三个业务模块实现及各自的控制台演示
automated_tests/     三套业务测试
documentation/      接口说明、变更说明和测试资料
docs/task3/         Task 3 图、可编辑源文件与配套说明
weekly/             阶段开发记录
build_and_test.ps1   编译全部测试与演示，并运行三套测试
CMakeLists.txt      可选的 CMake 构建配置
README.md           项目概况、运行方法与当前状态
```

组织仓库最初的说明保存在 [organization_repository_original_README.md](documentation/organization_repository_original_README.md)，原始提交也保留在 Git 历史中。

## 编译、测试与演示

### 环境要求

- Windows PowerShell。
- 支持 C++17 的 g++，并确保终端能执行 `g++ --version`。脚本也支持通过环境变量 `CXX` 指定编译器路径。
- 当前通过项目脚本直接编译，不要求安装 CMake。

在项目根目录打开 PowerShell，执行：

```powershell
.\build_and_test.ps1
```

脚本会创建 `build` 目录，编译三套测试和三个演示程序，再运行三套测试。全部通过时返回退出码 `0`，并输出：

```text
All registration tests passed
PASS: 233 checks, 0 failure(s)
All student query and timetable tests passed
ALL BUILD AND TESTS PASSED
```

`233` 是当前教师管理员测试的检查数，后续增加测试后可能变化。

编译完成后，可分别运行：

```powershell
# 选课与退课演示
.\build\course_registration_console_demonstration.exe

# 教师、管理员及选退课联动演示
.\build\teacher_and_administration_demonstration.exe

# 学生搜索、余量和课表演示
.\build\student_query_demonstration.exe
```

项目同时保留 `CMakeLists.txt`，但本次验收使用上述 g++ 脚本，尚未验证 CMake 构建流程。

## 本次整合与验证

本次阶段整合接入以下开发记录，采用合并提交保留历史，不通过重新上传文件替代成员原有提交：

| 来源分支 | 本次接入版本 |
| --- | --- |
| `zhang-yuhua-registration-rules` | `88f0ccd` |
| `feature/zhu-ziliang-teacher-admin` | `d8700ce`，已包含在 Yikai 分支的历史中 |
| `feature/zhang-yikai-student-query` | `5940ee2` |

2026 年 11 月 6 日，在组织仓库本地整合目录重新编译并运行三套现有测试，全部通过。其中教师管理员测试通过 233 项检查。此前对接入版本的补充回归与联调通过 44 项检查，覆盖搜索组合、查询失败清空结果、选退课联动、容量和时段规则等场景。

联调验证的主要流程为：管理员设置容量为 1，学生 A 选课成功，学生 B 因满员被拒绝；教师名单和人数、学生余量与课表保持一致；A 退课后名额恢复，名单和课表同步更新。

补充检查也复现了下列待处理问题。测试通过仅表示已有断言通过，不代表所有业务规则或输入场景都已覆盖。

## 缺陷与待确认事项

编号对应团队缺陷跟踪记录。以下状态属于本次整合版本，后续修复需在复测后更新。

| 编号 | 问题 | 当前状态 |
| --- | --- | --- |
| 001 | 时段结束后仍允许选课 | 已修复并复测通过，请求时重新判断当前时间 |
| 002 | 修改教学班所属课程绕过重复选课与学分约束 | 已修复并复测通过，有有效选课时拒绝修改 `course_id` |
| 003 | 手动关闭后刷新时段又重新开放 | 已修复并复测通过，手动关闭会清空原时段 |
| 004 | 教师关键词与教师筛选由不同教学班满足 | 已在 `5940ee2` 修复，组合搜索测试通过 |
| 005 | 查询失败后保留上一次的课程与余量结果 | 已在 `fee6666` 修复，失败清空及回归测试通过 |
| 006 | 删除学生或教师后，成功提示可能显示另一人的编号 | 待修复，删除后继续读取原记录指针 |
| 007 | 教师 `class_summary()` 失败时仍保留旧结果，与学生查询约定不一致 | 待统一约定并补测，建议失败时清空输出；调用方必须检查结果码 |
| 008 | 时段刷新接口的头文件注释仍要求请求前手动刷新 | 待同步文档与注释，实际选退课已自行判断时间 |
| 009 | 时段校验接受不存在的日期，如 `2026-02-30` | 待修复，需按月份及闰年检查实际天数 |
| 010 | 本轮选课开放后，管理员仍可修改学分上限 | 业务规则已确定，实现待修改：本轮开放前可设置，一旦开放过，暂停或结束后也不得修改 |

006～010 尚未完成修复，不能将本次阶段代码视为最终验收版本。学分上限的具体数值仍由项目需求及配置决定，演示中的数字不代表固定业务要求。

## 后续工作

1. 完成 006～010 的修改、接口说明同步与回归测试。
2. 接入登录、账号状态校验及可信的用户身份。
3. 接入网页和请求入口，验证三类用户的完整操作流程。
4. 增加数据持久化，解决程序退出后内存数据不保存的问题。
5. 每周向组织仓库同步复核后的阶段成果，并更新测试、缺陷与开发记录。

Task 3 中 Zhu 的图与说明见 [图清单](docs/task3/FIGURES.md)，Yikai 的图、源文件和说明见 [member_b](docs/task3/member_b/README.md)。已有文档和图仍需随实际接口变更同步维护。
