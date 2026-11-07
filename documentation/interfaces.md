# 三人共享接口约定（草案，待 Zhu Ziliang / Zhang Yuhua / Zhang Yikai 确认）

> 状态：**草案**。本文件按团队说明第 4 节的要求，把语言、存储、数据字段、模块接口、
> 演示数据与修改流程写清楚，供三人 11 月 1 日前确认。确认后本节标题去掉"草案"，
> 之后任何对共享数据结构的改动都按第五节流程走。
>
> 起草：Zhu Ziliang。依据：Task 2 最终 URS（FR-01 至 FR-19）、Zhang Yuhua 第一周起步代码、
> 仓库现有 `documentation/TEAM_INTERFACE_AGREEMENT_DRAFT.txt`。

## 一、语言、构建、存储与 Web 方案

| 项目 | 约定 | 理由与状态 |
|---|---|---|
| 语言 | C++17 | 与 Yuhua 起步代码一致；三人已倾向 C++，本文件按此执行，如有变更在确认会上正式改 |
| 编译器 | g++ ≥ 8（本机 8.1.0 已验证），MSVC 不作要求 | 组员机器均为 MinGW-w64；源码只用标准库，未使用平台 API |
| 构建 | 首选 `g++` 直接编译（`build_and_test.ps1`）；同时保留 `CMakeLists.txt` | 部分组员未安装 CMake，直接 g++ 可保证人人能编译 |
| 编码风格 | `snake_case` 类型/文件、`camelCase` 方法、命名空间 `course_registration` | 沿用 Yuhua 起步代码，避免重命名造成冲突 |
| 数据存储 | 阶段一：内存（示例数据初始化）。阶段二：单一存储对象 + 文件/SQLite 持久化 | **唯一事实来源**：课程目录、教学班、选课记录、选课规则都由 `RegistrationService` 持有；其他模块只通过接口读写 |
| Web 方案 | 阶段三：单头文件 `cpp-httplib` 提供 HTTP/JSON 接口，静态 HTML 页面调用 | 避免引入框架与包管理器；`cpp-httplib` 为单头文件，可直接放进 `third_party/`。**待三人确认** |
| 时间表示 | 上课时间 `day_of_week` 取 1–7（周一 = 1），`start_minute`/`end_minute` 为当天 0 点起的分钟数 | 与 Yuhua 现有 `ClassMeeting` 一致 |
| 时间戳表示 | 字符串 `"YYYY-MM-DDTHH:MM"`，固定宽度可直接比较先后 | 用于选课时段；避免引入 `chrono` 时区问题 |

## 二、共同数据字段与类型

以下定义以 `header_files/course_registration_business_rules.hpp` 为准，**任何模块不得另建不兼容版本**。

| 结构 | 字段（类型） | 说明 |
|---|---|---|
| `Course` | `int course_id`、`string course_code`、`string course_name`、`int credits` | 课程目录信息；`course_code` 全库唯一（比较时忽略大小写） |
| `ClassMeeting` | `int day_of_week`、`int start_minute`、`int end_minute`、`string room` | 一次上课时段；要求 `0 <= start < end <= 1440`、`1 <= day <= 7` |
| `TeachingClass` | `int teaching_class_id`、`int course_id`、`int teacher_id`、`int capacity`、`vector<ClassMeeting> meetings` | 学生实际选的对象；容量属于教学班 |
| `EnrollmentStatus` | `Active`、`Dropped` | 只区分有效与已退课 |
| `Enrollment` | `int student_id`、`int teaching_class_id`、`EnrollmentStatus status` | 学生与教学班之间的记录；`(student_id, teaching_class_id)` 至多一条 `Active` |
| `RegistrationRule` | `bool registration_open`、`int maximum_credits`、`string opens_at`、`string closes_at` | 后两个字段为 Zhu 追加（见 `interface_change_request.md`）；为空表示未设置时段，此时以 `registration_open` 为准 |
| `OperationResult` | `ResultCode code`、`string message` | 所有接口统一返回；`message` 用人可读英文写明原因与关键数字 |
| `Role` | `Student`、`Teacher`、`Administrator` | 由 Zhu 定义，与登录结果对接 |
| `Student` | `int student_id`、`string student_number`、`string name`、`string major`、`bool account_active` | `student_id` 与 `Enrollment::student_id` 同一含义；`student_number` 全库唯一 |
| `Teacher` | `int teacher_id`、`string teacher_number`、`string name`、`string department`、`bool account_active` | `teacher_id` 与 `TeachingClass::teacher_id` 同一含义；`teacher_number` 全库唯一 |
| `Administrator` | `int administrator_id`、`string account_number`、`string name`、`bool account_active` | 管理员基础资料 |
| `UserIdentity` | `int user_id`、`string account_number`、`Role role` | 登录后由 Yuhua 的登录模块交给其他模块 |

**编号统一用 `int`**，学号/工号/课程代码用 `string`。`user_id` 与 `student_id`/`teacher_id`/
`administrator_id` 共用一套编号空间，便于与 `Enrollment` 对齐。

### 名额的统一算法（必须一致）

```
active_count(tc)  = 满足 status == Active 且 teaching_class_id == tc 的记录数
remaining_seats   = TeachingClass.capacity - active_count(tc)
```

**不保存剩余名额**，任何模块每次现算。这样教师名单、学生课表和选课检查不会出现不同数字。
名额不足、容量下调、退课恢复都由此公式自然得出。

## 三、模块接口（输入、输出、失败原因）

### 3.1 登录（Zhang Yuhua）

| 接口 | 输入 | 输出 |
|---|---|---|
| `LoginService::login(account_number, password)` | 账号（学号/工号/管理员账号）与密码 | `Success` + `UserIdentity`；失败 `RecordNotFound`（账号不存在）、`InvalidInput`（密码错误，**不区分账号是否存在**的提示可按 URS 要求调整）、`PermissionDenied`（账号停用，`account_active == false`） |

登录只负责认证与发放 `UserIdentity`，不负责业务权限；业务权限由 `PermissionRules` 判断。

### 3.2 课程搜索与详情（Zhang Yikai）

| 接口 | 输入 | 输出 |
|---|---|---|
| `search_courses(keyword, filters)` | 关键词、可选筛选（课程代码、学分、教师） | `Success` + `vector<CourseView>`（含课程与其教学班摘要） |
| `course_detail(course_id)` | 课程编号 | `Success` + `CourseView`；`RecordNotFound` |

`CourseView` 中的 `remaining_seats` 必须走 3.5 的统一算法。

### 3.3 选课（Zhang Yuhua）

`RegistrationService::enroll(student_id, teaching_class_id)`，检查顺序固定：

1. 开放时间 → `RegistrationClosed`
2. 同一 Course 的其他教学班 → `AlreadyEnrolled`
3. 同一天上课时间重叠 → `TimeConflict`
4. 学分上限 → `CreditLimitExceeded`
5. 剩余名额 → `TeachingClassFull`

补充：教学班不存在 → `TeachingClassNotFound`。任一步失败都不新增记录、不减少名额。

时段判断只有一个入口：`RegistrationService::registration_open_now()`。它按当前时间与 `opens_at`/`closes_at` 现算（两个比较条件），未设置时段时沿用 `registration_open`；`enroll()` 与 `drop()` 都调用它，所以时段结束后不需要任何额外刷新就会拒绝请求。

### 3.4 退课（Zhang Yuhua）

`RegistrationService::drop(student_id, teaching_class_id)`：把 `Active` 改为 `Dropped`，
名额随即恢复；`RegistrationClosed` / `ActiveEnrollmentNotFound` 为失败原因。

### 3.5 课表与人数（Zhang Yikai / Zhu Ziliang 共用）

| 接口 | 输入 | 输出 |
|---|---|---|
| `RegistrationService::active_enrollments(student_id)` | 学生编号 | `vector<Enrollment>`（仅 `Active`） |
| `RegistrationService::enrollment_count(teaching_class_id)` | 教学班编号 | 有效人数 `int` |
| `RegistrationService::remaining_seats(teaching_class_id)` | 教学班编号 | 剩余名额；教学班不存在返回 `-1` |
| `TeacherService::class_roster(identity, teaching_class_id)` | 身份、教学班编号 | `Success` + `ClassRoster`（只含 `Active`）；`PermissionDenied`、`TeachingClassNotFound` |

### 3.6 教师查询（Zhu Ziliang，UC09）

| 接口 | 输入 | 输出 |
|---|---|---|
| `TeacherService::assigned_teaching_classes(identity, out)` | 教师身份 | `Success` + `vector<TeachingClassSummary>`（本人负责的教学班） |
| `TeacherService::class_summary(identity, id, out)` | 身份、教学班编号 | `Success` + 摘要；`PermissionDenied`（他人教学班）、`TeachingClassNotFound` |
| `TeacherService::class_roster(identity, id, out)` | 身份、教学班编号 | `Success` + 名单；同上失败原因 |

### 3.7 管理员维护（Zhu Ziliang，UC13–UC16）

| 接口 | 失败原因 |
|---|---|
| `create_course` / `update_course` / `remove_course` | `PermissionDenied`、`InvalidInput`、`DuplicateIdentifier`、`RecordNotFound`、`CatalogInUse`（仍有教学班）、`CreditLimitExceeded`（改学分使学生超限） |
| `create_teaching_class` / `update_teaching_class` / `remove_teaching_class` | `PermissionDenied`、`InvalidInput`（非法时段/容量≤0/停用教师）、`DuplicateIdentifier`、`RecordNotFound`（课程或教师不存在）、`TimeConflict`（教师或已选学生冲突）、`CatalogInUse`（仍有选课记录，或会删掉/移走课程最后一个教学班）、`CapacityBelowEnrollment` |
| `set_teaching_class_capacity` | `PermissionDenied`、`InvalidInput`、`TeachingClassNotFound`、`CapacityBelowEnrollment` |
| `set_registration_period` / `set_maximum_credits` / `close_registration` | `PermissionDenied`、`InvalidInput`（格式错误或起止颠倒；本轮已开放过仍试图改学分上限） |
| `create_student` / `update_student` / `remove_student`、`create_teacher` / `update_teacher` / `remove_teacher` | `PermissionDenied`、`InvalidInput`（空字段）、`DuplicateIdentifier`（编号或学号/工号重复）、`RecordNotFound`、`CatalogInUse`（学生有任何选退课记录 / 教师仍负责教学班） |
| `check_data_integrity(identity, problems)` | `PermissionDenied`；发现不一致时返回 `InvalidInput` 并逐条列出问题 |

### 3.7.1 管理员维护必须遵守的业务规则（2026-11-07 确认）

| 规则 | 现状 |
|---|---|
| 每门 Course 至少保留一个 TeachingClass（URS 6.7） | 删除或移走课程最后一个教学班返回 `CatalogInUse`；`check_data_integrity` 会报告零教学班课程。**创建课程时"课程 + 首个教学班"原子保存尚未实现**，方案见 `documentation/interface_proposal_course_with_teaching_class.md`，等三人对齐后再改接口 |
| 改派教师时检查账号状态 | 停用教师（`account_active = false`）既不能新建班，也不能改派；失败后原任课教师保留 |
| 有选退课记录的学生禁止删除 | 有任何 `Enrollment` 记录（含全部为 `Dropped`）都返回 `CatalogInUse`；需要限制使用时改为停用账号 |
| 同一教学班内部允许时段重叠 | 已确认行为，不做"班级内部自冲突"校验；跨教学班的学生时间冲突与教师排课冲突仍然生效 |
| 学分上限的修改时机 | 本轮开放过（含暂停、结束、预约到点自动开放）之后禁止修改 |

### 3.8 结果码清单

`Success`、`RegistrationClosed`、`AlreadyEnrolled`、`TimeConflict`、`CreditLimitExceeded`、
`TeachingClassFull`、`TeachingClassNotFound`、`ActiveEnrollmentNotFound`（以上为 Yuhua 原有），
`PermissionDenied`、`InvalidInput`、`RecordNotFound`、`DuplicateIdentifier`、
`CapacityBelowEnrollment`、`CatalogInUse`（以上为 Zhu 追加）。

## 四、演示数据格式与测试方法

### 4.1 示例数据（内存版）

| 对象 | 内容 |
|---|---|
| 学生 | 1001 `S2023001` Alice Chen；1002 `S2023002` Bob Liu；1003 `S2023003` Cara Wu |
| 教师 | 100 `T2015007` Dr. Wang（软件工程）；200 `T2016011` Dr. Li（数学）；300 `T2018003` Dr. Zhao（计算机） |
| 管理员 | 900 `A2020001` Admin Sun |
| 课程 | 1 `SE101` Software Engineering 3 学分；2 `MA101` Mathematics 4 学分；3 `CS101` Programming 5 学分 |
| 教学班 | 10：课程 1 / 教师 100 / 容量 2 / 周一 09:00–10:00 A101；11：课程 1 / 教师 300 / 周二 09:00–10:00 A102；20：课程 2 / 教师 200 / 周一 09:30–10:30 B201；30：课程 3 / 教师 300 / 周三 14:00–15:00 C301 |
| 规则 | `registration_open = true`、`maximum_credits = 18`（**示例值，不是甲方确认的固定数字**）、时段未设置 |

同一门课程有两个教学班（10、11）用于验证"同一课程只能选一个教学班"；10 与 20 时段重叠
用于验证时间冲突；30 为 5 学分用于验证学分上限。

### 4.2 测试运行

```powershell
.\build_and_test.ps1
```

或按 README 手动编译三套测试。判定标准：三套测试均返回 0；Zhu 的测试会打印
`PASS: <n> checks, 0 failure(s)`，另外两套打印各自的通过信息。每周把该输出作为飞书"测试证据"。

### 4.3 每周共同场景（必跑）

管理员设容量为 1 → 学生 A 选课成功 → 余量 0、A 课表出现课程、教师人数 1 →
学生 B 因满员被拒绝 → A 退课后余量恢复、课表与教师人数同步更新。

该场景已固化为 `test_weekly_joint_scenario`（自动化），并在命令行演示中输出，
可直接用于组内验收。

## 五、共享数据结构与接口的修改流程

1. **谁可以改**：`course_registration_business_rules.hpp` /
   `course_registration_business_rules.cpp` 由 Zhang Yuhua 作为主要维护人；
   `teacher_and_administration_services.hpp/.cpp` 由 Zhu Ziliang 维护；
   `docs/interfaces.md` 与数据字段表由三人共同确认后生效。
2. **改动方式**：
   - 只能**追加**（新字段带默认值、新结果码加在枚举末尾、新增接口），不得改变已有字段含义、
     枚举顺序或 `enroll()` 的检查顺序。破坏性改动必须三人会议确认。
   - 追加后必须保证原有文件在 `-Wall -Wextra` 下**零警告**编译，且另外两套测试仍通过。
3. **通知与复核**：在个人分支提交，附 `interface_change_request.md`；在飞书 @ 维护人与复核人；
   复核轮换按团队说明 —— Yuhua 的改动由 Yikai 复核，Yikai 的由 Ziliang 复核，Ziliang 的由 Yuhua 复核。
   复核人需从最新 `main` 拉取、从头编译、运行三套测试后才可合并。
4. **合并前门槛**：`main` 可编译；三套测试通过；接口表已更新；截图或测试输出已贴进飞书。

## 六、仍需三人确认的问题

1. 存储方案：文件（JSON/CSV）还是 SQLite？谁在哪个分支上先落地？
2. Web 方案：`cpp-httplib` + 静态页面是否接受？接口路径与 JSON 字段命名是否按本文件第三节。
3. 学分上限的默认值与是否允许管理员修改（URS 未给出数字，不能写成甲方要求）。
4. 登录失败提示是否可以区分"账号不存在"和"密码错误"（涉及安全与 URS 的具体措辞）。
5. 教学班删除策略：当前"仍有选课记录（含已退课历史）即拒绝"，是否需要归档/软删除。
