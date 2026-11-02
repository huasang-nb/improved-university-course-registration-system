# Improved University Course Registration System

Software Engineering Task 1 project for an improved university course registration system.

Team 4 选课系统。本仓库当前包含 **Zhang Yuhua 第一周起步代码**（选课/退课业务规则）与
**Zhu Ziliang 的教师/管理员模块**（UC09、UC13、UC14、UC15、UC16），并附带 Task 3
中 Zhu Ziliang 负责的对象图与 5 张时序图。

- 需求依据：Task 2 最终 URS，范围 FR-01 至 FR-19。
- 不在当前范围：课程推荐、特殊选课审批、教师申请与审批、邮件通知、候补、批量导入、复杂统计。
- 语言：C++17（团队优先考虑 C++，最终方案见 `documentation/interfaces.md`）。
- 存储：当前为内存版，课程目录与选课记录只有一份，由 `RegistrationService` 持有。

## 一、目录结构

```
header_files/
  course_registration_business_rules.hpp      Yuhua：Course / TeachingClass / Enrollment / 规则与选课服务
  teacher_and_administration_services.hpp     Zhu：账号、角色权限、教师查询、管理员维护
source_code/
  course_registration_business_rules.cpp      Yuhua：enroll() / drop() / 名额计算
  course_registration_console_demonstration.cpp  Yuhua：第一周演示
  teacher_and_administration_services.cpp     Zhu：教师/管理员实现
  teacher_and_administration_demonstration.cpp   Zhu：命令行演示 + 每周共同场景
automated_tests/
  course_registration_business_rules_tests.cpp   Yuhua：选课规则测试
  teacher_and_administration_services_tests.cpp  Zhu：UC09/13/14/15/16 + 每周场景
documentation/
  TEAM_INTERFACE_AGREEMENT_DRAFT.txt          Yuhua：最初的接口草案
  interfaces.md                               Zhu：三人接口约定草案（待三人确认）
  interface_change_request.md                 Zhu：对共享头文件的增量改动申请
docs/task3/
  umlkit.py, make_diagrams.py                 生成 Task 3 图与可编辑源文件的脚本
  figures/*.drawio, figures/*.png             6 张图的可编辑源文件与高清导出
  FIGURES.md                                  逐图中英双语说明
build_and_test.ps1                            本机编译 + 运行全部测试
CMakeLists.txt                                可选（已安装 CMake 时使用）
```

## 二、编译与运行

需要支持 C++17 的 g++（本机验证版本：`g++.exe (x86_64-posix-seh) 8.1.0`）。

### 一次性跑完编译与全部测试

```powershell
.\build_and_test.ps1
```

脚本会依次编译并运行两套测试，最后打印 PASS/FAIL，可直接作为每周测试证据。

### 手动编译

```powershell
# 1) Yuhua 的选课规则测试
g++ -std=c++17 -Wall -Wextra -Iheader_files `
    source_code/course_registration_business_rules.cpp `
    automated_tests/course_registration_business_rules_tests.cpp `
    -o build/course_registration_business_rules_tests.exe

# 2) Zhu 的教师/管理员测试
g++ -std=c++17 -Wall -Wextra -Iheader_files `
    source_code/course_registration_business_rules.cpp `
    source_code/teacher_and_administration_services.cpp `
    automated_tests/teacher_and_administration_services_tests.cpp `
    -o build/teacher_and_administration_services_tests.exe
```

运行：

```powershell
.\build\course_registration_business_rules_tests.exe   # 输出 All registration tests passed
.\build\teacher_and_administration_services_tests.exe  # 输出 PASS: <n> checks, 0 failure(s)
```

### 命令行演示

```powershell
g++ -std=c++17 -Iheader_files `
    source_code/course_registration_business_rules.cpp `
    source_code/teacher_and_administration_services.cpp `
    source_code/teacher_and_administration_demonstration.cpp `
    -o build/teacher_and_administration_demonstration.exe
.\build\teacher_and_administration_demonstration.exe
```

演示按团队说明第 5 节的顺序输出：管理员设容量 1 → 学生 A 选课成功 →
余量 0 / A 课表出现课程 / 教师人数 1 → 学生 B 满员被拒 → A 退课后余量、
课表与人数同步更新，随后再演示 UC13/14/15/16 的成功与失败路径。

命令行程序只用于阶段验证。按计划在 11 月 16 日至 22 日接入 Web 页面或 HTTP 接口，
最终系统以 URS 的 Web 范围为交付标准。

## 三、本周实现范围（Zhu Ziliang）

| 用例 | 内容 | 主要入口 |
|---|---|---|
| UC09 View Assigned Teaching Classes | 教师只读本人负责的教学班、名单与人数；越权返回 `PermissionDenied` | `TeacherService::assigned_teaching_classes` / `class_roster` / `class_summary` |
| UC13 Manage Courses and Teaching Classes | 管理员新增/修改/删除 Course 与 TeachingClass；检查引用关系、重复编号、非法时段、教师时段冲突 | `AdministratorService::create_course` / `update_course` / `remove_course` / `create_teaching_class` / `update_teaching_class` / `remove_teaching_class` |
| UC14 Set Teaching Class Capacity | 改容量前检查现有有效选课人数，不低于人数才允许 | `AdministratorService::set_teaching_class_capacity` |
| UC15 Set Registration Period | 设置开始/结束时间，拒绝无效时段，并换算当前是否开放 | `AdministratorService::set_registration_period` / `set_maximum_credits` / `close_registration` |
| UC16 Manage Student / Teacher Information | 维护学生/教师账号，拒绝重复编号或学号工号、空字段 | `AdministratorService::create_student` / `update_student` / `remove_student` / `create_teacher` / `update_teacher` / `remove_teacher` |
| 数据一致性 | 课程/教学班/选课记录关系、容量与人数、重复选课、课表冲突 | `AdministratorService::check_data_integrity` |

权限规则：教师只能读本人教学班；管理员可读全部并可维护目录、容量、时段与人员；学生不能通过
教师入口读取教学班。规则集中在 `PermissionRules`，待与组长的登录结果接线。

## 四、与另外两位模块的接口

- 选课与退课仍由 `RegistrationService::enroll` / `drop` 负责，检查顺序与名额计算未改动。
- 名单、人数、余量一律来自同一份有效 `Enrollment` 记录：`remaining_seats = capacity - active count`，
  不另存数字，因此教师端、学生端与管理员端不会出现不同人数。
- 管理员对目录/规则的改动通过 `SystemContext` 写回 `RegistrationService`，选课记录保持不变。
- 对共享头文件只有**追加式**改动（新增字段、结果码、只读接口），详见
  `documentation/interface_change_request.md`，需 Zhang Yuhua 复核后合并。

## 五、Task 3 图

```powershell
python docs/task3/make_diagrams.py
```

生成 `docs/task3/figures/` 下 6 张图的 `.drawio`（可编辑源文件）与 `.png`（300 dpi 级别高清图），
逐图说明见 `docs/task3/FIGURES.md`。`.drawio` 可用 draw.io / diagrams.net 直接打开继续修改。

## 六、当前未完成项

- 登录模块（Yuhua）尚未接入，`UserIdentity` 由测试与演示手工构造。
- Web 页面/HTTP 接口尚未开始（计划 11 月 16 日至 22 日）。
- 数据持久化尚未开始；当前每次运行由示例数据初始化。
- 学分上限的数字只作为示例数据，URS 未规定具体值，不得写成甲方要求。
- 教学班删除采用"仍有选课记录则拒绝"的策略，归档能力不在当前范围。
