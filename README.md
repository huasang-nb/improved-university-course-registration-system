# Improved University Course Registration System

## 改进型大学选课系统 · Team 4

本项目是 Team 4 的软件工程课程项目，面向学生、教师和管理员，围绕课程查询、选课退课、个人课表、教学班查询和课程管理等场景展开开发。

项目通过统一的课程、教学班和选课记录，检查重复选课、上课时间冲突、学分超限和名额不足等问题，目标是使学生端的选课结果、教师端的名单与人数、管理员端的容量设置保持一致。

当前处于 **C++ 业务原型与分模块开发阶段**，使用内存数据运行，尚未形成完整界面和持久化系统。

## 功能与进度

以下状态更新于 **2026 年 11 月 4 日**。“已有原型”表示代码已提交到开发分支，仍需整合与验收。

| 使用者 | 功能 | 当前状态 |
| --- | --- | --- |
| 学生 | 选课、退课；重复课程、时间冲突、学分上限和容量检查 | 已有原型与测试 |
| 学生 | 查询有效选课记录、教学班剩余名额 | 已有业务接口 |
| 学生 | 课程搜索、课程详情、个人课表展示 | 后续开发与接入 |
| 教师 | 查看本人教学班、选课人数和学生名单 | 已有分支原型与测试，待整合 |
| 管理员 | 维护课程、教学班、容量和学生／教师资料 | 已有分支原型与测试，待整合 |
| 管理员 | 设置选课时段 | 已有原型，请求时的时间判断仍需完善 |
| 三类用户 | 登录与身份认证、完整界面、数据持久化 | 后续开发与接入 |

### 阶段成果所在分支

目前 `main` 尚未合入上述业务代码。查看或运行阶段成果时，请选择相应开发分支。

| 分支 | 内容 |
| --- | --- |
| [`zhang-yuhua-registration-rules`](https://github.com/huasang-nb/improved-university-course-registration-system/tree/zhang-yuhua-registration-rules) | 选课、退课、名额查询、控制台演示和业务规则测试 |
| [`feature/zhu-ziliang-teacher-admin`](https://github.com/huasang-nb/improved-university-course-registration-system/tree/feature/zhu-ziliang-teacher-admin) | 在选课原型基础上增加教师查询、管理员维护和共享数据接口 |

## 核心业务设计

`Course` 表示课程，如“软件工程”；`TeachingClass` 表示该课程具体开设的教学班，包含教师、容量和上课时间。一门课程可以开设多个教学班，学生选择的是具体教学班。

选课原型按以下顺序检查业务条件：

1. 选课是否开放。
2. 是否已选择同一课程的教学班。
3. 是否与当前已选教学班的上课时间冲突。
4. 选课后的总学分是否超过配置上限。
5. 教学班是否有剩余名额。

系统同时检查目标教学班是否存在。任一检查失败，返回原因并保持选课记录不变；全部通过后，建立或恢复有效选课记录。

退课时，将有效记录由 `Active` 改为 `Dropped`。各模块根据有效记录计算人数与名额：

```text
剩余名额 = 教学班容量 − 当前有效选课人数
```

示例数据中的容量和学分上限用于演示与测试，不代表已确认的固定业务要求。

## 技术与项目结构

当前使用 **C++17、标准库容器和内存数据**，提供控制台演示、自动化测试及 CMake 构建配置。数据库和完整用户界面将在后续阶段接入。

开发分支的基础目录如下；教师与管理员扩展分支还包含相应模块文件及测试。

```text
improved-university-course-registration-system/
├── header_files/        数据结构与业务接口声明
├── source_code/         业务实现与控制台演示
├── automated_tests/     业务规则与模块测试
├── documentation/       接口约定与开发说明
├── CMakeLists.txt       CMake 构建配置
└── README.md            项目介绍与运行指南
```

## 快速运行

以下步骤适用于 Windows PowerShell。需要安装 Git 和支持 C++17 的 g++ 编译器，并确保终端可以使用 `git`、`g++`。

### 1. 获取选课原型

```powershell
git clone --branch zhang-yuhua-registration-rules https://github.com/huasang-nb/improved-university-course-registration-system.git
cd improved-university-course-registration-system
```

### 2. 编译并运行演示

在项目根目录执行：

```powershell
g++ -std=c++17 -Iheader_files source_code/course_registration_business_rules.cpp source_code/course_registration_console_demonstration.cpp -o course_registration_console_demonstration.exe
.\course_registration_console_demonstration.exe
```

演示使用一个容量为 1 的教学班：选课后余量为 0，退课后余量恢复为 1。预期输出：

```text
Enrollment successful
Remaining seats: 0
Drop successful
Remaining seats after drop: 1
```

### 3. 运行选课业务测试

```powershell
g++ -std=c++17 -Iheader_files source_code/course_registration_business_rules.cpp automated_tests/course_registration_business_rules_tests.cpp -o course_registration_business_rules_tests.exe
.\course_registration_business_rules_tests.exe
```

通过时输出 `All registration tests passed`。测试覆盖选课成功、满员拒绝、同一课程重复选课、时间冲突、学分超限、退课恢复名额、重复退课以及关闭选课后的请求拒绝等场景。

### 4. 查看教师与管理员原型

在同一仓库切换到扩展分支：

```powershell
git switch feature/zhu-ziliang-teacher-admin
.\build_and_test.ps1
```

该分支的脚本可构建选课、教师和管理员模块的测试及演示程序。在 2026 年 11 月 4 日对提交 `341e480` 的检查中，原选课测试通过，教师与管理员测试输出 `PASS: 177 checks, 0 failure(s)`。该结果对应当时版本中的已有测试，完整业务流程仍需进一步验收。

## 当前限制与后续计划

- 数据由示例代码初始化，程序退出后不会保存；尚未提供数据库或文件持久化。
- 登录与完整操作界面尚未接入，目前通过控制台和测试调用业务接口。
- 各模块仍需统一接入同一份共享数据，完成完整流程联调。
- 已识别的整合问题包括：选课请求需要及时更新时段状态；已有有效选课记录时，修改教学班所属课程需要遵守重复选课与学分约束。

后续将完成课程查询与课表、登录与角色接入、数据保存、完整界面和三类用户的流程验收，并随实际开发更新本说明。

## 团队与开发记录

| 成员 | 主要负责范围 |
| --- | --- |
| Zhang Yuhua | 选课与退课业务、后续登录接入、代码整合与协调 |
| Zhang Yikai | 课程查询、课程详情、个人课表与相关数据接入 |
| Zhu Ziliang | 教师查询、管理员维护与角色权限规则 |

团队采用个人分支开发，通过提交记录和 Pull Request 保留改动与复核过程，并在飞书记录每周代码进度、测试结果和证据链接。具体贡献以实际提交和复核记录为准。

欢迎通过本仓库的 [Issues](https://github.com/huasang-nb/improved-university-course-registration-system/issues) 提出问题或建议。