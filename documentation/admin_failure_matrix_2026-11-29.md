# 管理员模块失败路径验收（2026-11-23 ~ 11-29，第 4 周）

手册本周要求：测试**容量调整、已有选课记录、重复账号、无效时段、越权访问**，修复本人模块问题；
验收口径是“**每个失败操作都有清楚结果，且不破坏原数据**”。

本文件记录这五类失败场景的清单、期望返回码与消息、以及“原数据不变”的验证方式。
可执行版本在 `automated_tests/admin_failure_acceptance_tests.cpp`（309 项检查），
运行输出在 `documentation/admin_failure_acceptance_evidence_2026-11-29.txt`。

## 1. 验收口径怎么落到测试里

每个失败用例都做三件事：

1. **返回码**：`expect_failure(result, ResultCode::X, ...)`，同时要求 `message` 非空——
   调用方不能只拿到一个失败码、却不知道哪里错了。
2. **可读消息**：对关键场景再 `check_contains(message, "<关键词>")`，例如
   `dropping the records does not make the course deletable`、
   `deactivate the account instead of removing it`、`requires a different role`。
3. **原数据不变**：`capture_state()` 把 `SystemContext` 拼成一份快照——规则四字段、
   `round_opened`、全部课程/教学班（含每段上课时间与教室）、学生/教师/管理员（含
   `password_hash`）、全部选课记录（含 Active/Dropped 状态）。被拒操作前后各取一次，
   `check_unchanged()` 比较，不一致就打印前后差异。

也就是说，“失败不破坏原数据”不是靠人工观察，而是每一项失败都带一次全量比对。

另外每个套件结束都跑一次 `check_data_integrity()`，确认前面一连串被拒操作没有在
数据里留下悬空引用、超容量、同一课程多班之类的后遗症。

## 2. 五类失败场景

### 2.1 容量调整

| 场景 | 期望 | 关键消息 | 原数据 |
| --- | --- | --- | --- |
| 容量改大（2 → 5） | `Success` | `changed from 2 to 5` | 容量与余量同步变化 |
| 容量设为 0 | `InvalidInput` | `capacity must be at least 1` | 不变 |
| 容量设为负数 | `InvalidInput` | 同上 | 不变 |
| 容量低于已选人数（3 人时设 2） | `CapacityBelowEnrollment` | `no seat can be taken away from an existing student` | 容量、余量、选课记录都不变 |
| 容量等于已选人数 | `Success` | — | 余量为 0 |
| 教学班不存在 | `TeachingClassNotFound` | — | 不变 |
| 教师 / 学生调用容量调整 | `PermissionDenied` | `requires a different role` | 不变 |
| 走 `update_teaching_class` 把容量调低 | `CapacityBelowEnrollment` | `active enrollment(s) of teaching class` | 不变 |

两条容量下界消息不同（一条解释“不能从在读学生手里收回座位”，一条只报事实），
测试按实际实现分别断言，避免把措辞变化误判成功能回归。

### 2.2 已有选课记录

| 场景 | 期望 | 关键消息 |
| --- | --- | --- |
| 有 Active 记录时删课 | `CatalogInUse` | `dropping the records does not make the course deletable` |
| 有 Active 记录时删教学班 | `CatalogInUse` | `ask students to drop first` |
| 学生退课后（只剩 Dropped）删教学班 | `CatalogInUse` | `dropped enrollment record` + `archiving is out of the current scope` |
| 只剩 Dropped 时删课 | `CatalogInUse` | 同上（课程级联路径） |
| 有选课记录时删学生 | `CatalogInUse` | `deactivate the account instead of removing it` |
| 有 Active 记录时改教学班的 `course_id` | `InvalidInput` | `cannot be changed` |
| **只剩 Dropped 记录时改 `course_id`** | `InvalidInput` | `dropped enrollment record(s) kept as history` |
| 无记录的教学班改到别的课程 | `Success` | —（原课程还留着别的班） |
| 删课程最后一个教学班 | `CatalogInUse` | `a course must keep at least one teaching class` |
| 删没有教学班的课程 | `Success` | `removed together with 1 teaching class`（有班时）/ 直接删 |
| 删除仍带班的教师 | `CatalogInUse` | `still teaches N teaching class(es)` |
| 删除不带班的教师 | `Success` | — |

这一类的共同原则：**选课记录与教学班只增不迁**。管理员要让某门课不再开，只能新建/
改排，不能靠“删掉记录”或“把班挪到别的课程”把历史抹掉。

### 2.3 重复账号

| 场景 | 期望 | 关键消息 |
| --- | --- | --- |
| 学生用教师的工号注册 | `DuplicateIdentifier` | `already used by teacher` |
| 学生用管理员的账号 | `DuplicateIdentifier` | `already used by administrator` |
| 学号只差大小写/首尾空格（`" s2023001 "`） | `DuplicateIdentifier` | 归一化后比较，不区分大小写与空白 |
| 教师用学生的学号 | `DuplicateIdentifier` | `already used by student` |
| 同一角色重复 id / 重复编号 | `DuplicateIdentifier` | `already exists` |
| 空编号 / 空姓名 | `InvalidInput` | `must not be empty` |
| 更新学生时改成别人的编号 | `DuplicateIdentifier` | `is used by student_id` |
| 更新学生时改成自己的编号（大小写变体） | `Success` | 允许；归一化查询仍能找到本人 |
| 课程代码带空格重复（`" se101 "` vs `"SE101"`） | `DuplicateIdentifier` | 新建 `already exists` / 更新 `is used by course_id` |

### 2.4 无效时段

| 场景 | 期望 | 关键消息 |
| --- | --- | --- |
| 改成与该班已选学生课表冲突的时间 | `TimeConflict` | `already taken by student` |
| 改成与该教师其他班冲突的时间 | `TimeConflict` | 指出冲突的教学班 |
| 星期为 0 或 8 | `InvalidInput` | 合法范围 1–7 |
| 开始时间 = 结束时间 | `InvalidInput` | — |
| 开始时间 > 结束时间 | `InvalidInput` | — |
| 结束时间 1441（超过一天） | `InvalidInput` | — |
| 开始时间为负 | `InvalidInput` | — |
| 上课时间为空 | `InvalidInput` | — |
| 时段写成不存在的日期（如 2 月 30 日） | `InvalidInput` | `YYYY-MM-DDTHH:MM` 形式与真实日历 |
| 时段写成单位数小时（`T9:00`） | `InvalidInput` | 同上 |
| 开始与结束相同 | `InvalidInput` | `must start before it ends` |
| 开始晚于结束 | `InvalidInput` | 同上 |
| 合法边界（11-30T09:00 ~ 12-05T18:00） | `Success` | 窗口在起点开、终点关 |
| 本轮开放后再改学分上限 | `InvalidInput` | `fixed before this round of registration opens` |
| 关闭选课后仍尝试选课 | `RegistrationClosed` | — |

“失败的时段设置不能把本轮锁死”单独验证过：在 `registration_open = false` 的上下文里
连续四次非法时段设置都失败后，`set_maximum_credits` 仍然成功——说明失败路径没有
提前置上 `round_opened`。

### 2.5 越权访问

| 场景 | 期望 |
| --- | --- |
| 教师建课 / 删课 / 设报名时段 / 建学生 | `PermissionDenied` + `requires a different role` |
| 学生改学分上限 / 关闭选课 / 删教学班 | `PermissionDenied` |
| 教师列出本人教学班 | 只返回自己的班（10、11） |
| 教师读别人的教学班或名单 | `PermissionDenied`（消息含 `Permission denied`） |
| 另一位教师读该班 | `PermissionDenied` |
| 管理员读任意教学班 | `Success`（能读到别人的班与课程名） |
| 管理员读不存在的教学班 | `TeachingClassNotFound` |
| 学生调用三个教师查询入口 | `PermissionDenied` |

## 3. 本轮发现并修复的两个真实缺陷

这两个缺陷都是先写测试复现、再改实现，修复前该套件是 **12 项失败**，修复后 **309 项全过**。

### 3.1 只剩 Dropped 记录时仍可把教学班改挂到别的课程

- 现象：学生退课后，Dropped 记录会跟着教学班一起被迁到新课程，历史选课于是显示成
  另一门课（课程名、学分都变了），等同于改写历史；而删除路径对同样的记录是拒绝的，
  两条路径口径不一致。
- 修复：`update_teaching_class()` 在原有“有 Active 记录禁止改 `course_id`”之外，
  再统计该班的 Dropped 记录数，`dropped > 0` 时返回
  `InvalidInput`：`course_id of teaching class X cannot be changed from A to B
  while it has N dropped enrollment record(s) kept as history; create a new
  teaching class instead`。
- 连带调整：`teacher_and_administration_services_tests.cpp` 里原先断言“退课后可以改课程”
  的用例改为断言拒绝，并补了一个“完全没有记录的教学班仍可改课程”的正例
  （501 → 505 项检查）。

### 3.2 课程代码查重不去首尾空白

- 现象：查重只做了 `to_lower`，`" se101 "` 能绕过 `"SE101"`，课程目录里出现两个
  看起来一样的代码，后续查询与选课无法区分。
- 修复：`create_course_with_teaching_class()` 与 `update_course()` 的查重改用
  `normalize_account_key()`（去首尾空白 + 小写），与账号、登录匹配共用同一条规则；
  顺手删掉因此不再被引用的 `to_lower` helper。

## 4. 已知未覆盖 / 仍需与他人对齐

1. **同一教学班内部的时段重叠**仍然允许（两段课时间交叉）。这是此前确认过的口径，
   不属缺陷；如果后续要求“同一教学班内时段也不能重叠”，需要单开规则。
2. **跨角色的数字 id 唯一性**不做（学号/工号/管理员账号已跨角色唯一，数字主键各自独立）。
3. **HTTP / Web 接线**不在本人本阶段范围，失败结果目前以 `OperationResult` 返回，
   由调用方（未来的页面层）负责呈现。
4. 停用教师的历史记录保留策略、可选列表过滤与选课入口拒绝仍按
   `documentation/admin_module_open_issues_2026-11-15.md` 的状态执行
   （过滤与入口拒绝：已确认，待实现和联调；原班教师停用后改时间/容量：待讨论）。

## 5. 复现

```powershell
# 全部 6 个测试（MSVC 2022 + CMake）
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64
cmake --build build\cmake --config Debug
ctest --test-dir build\cmake -C Debug --output-on-failure

# 只跑失败路径验收（g++ 也可，本套不依赖 <filesystem>）
g++ -std=c++17 -Wall -Wextra -Iheader_files `
    source_code/course_registration_business_rules.cpp `
    source_code/teacher_and_administration_services.cpp `
    automated_tests/admin_failure_acceptance_tests.cpp -o build\acceptance.exe
.\build\acceptance.exe
```

预期：`PASS: 309 checks, 0 failure(s)`；`ctest` 6/6。
