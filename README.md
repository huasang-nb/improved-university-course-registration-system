# improved-university-course-registration-system
# 大学选课系统 | University Course Registration System

这是 Team 4 的软件工程课程项目。本仓库用于记录小组分阶段开发大学选课系统的过程。

## 当前进度

当前版本是选课业务的 C++ 内存原型，使用程序中准备的示例数据演示选课和退课。数据保存在内存中，程序结束后不会保存。

已实现的功能：

- 学生选择教学班，并依次检查选课是否开放、是否重复选择同一课程、上课时间是否冲突、是否超过学分上限，以及教学班是否有剩余名额。
- 学生退课：符合条件时将选课记录标记为已退课。
- 查询教学班的有效选课人数、剩余名额，以及学生的有效选课记录。
- 提供控制台演示程序和业务规则测试。

选课检查不通过时，程序返回相应原因，不改变选课记录。剩余名额根据教学班容量和有效选课人数计算。

目前尚未实现完整的登录、课程搜索、个人课表、教师和管理员功能、数据库保存及网页界面，后续将由小组继续开发。示例中的学分上限只是测试数据。

## 文件说明

| 文件或文件夹 | 作用 |
| --- | --- |
| `header_files/` | 存放数据结构和业务功能的声明，建议先看这里。 |
| `source_code/` | 存放选课、退课的具体实现和控制台演示程序。 |
| `automated_tests/` | 存放业务规则测试代码。 |
| `documentation/` | 存放小组接口约定草稿，具体内容有待组员讨论确认。 |
| `CMakeLists.txt` | 使用 CMake 构建项目的配置文件。 |
| `PROJECT_OVERVIEW_AND_BUILD_INSTRUCTIONS.txt` | 原有项目说明和编译方法。 |
| `CODE_FILES_READING_ORDER_AND_CURRENT_SCOPE.txt` | 代码阅读顺序和当前开发范围。 |

## 如何运行

下面的命令适用于 Windows，需先安装支持 C++17 的 g++ 编译器，并确保终端可以使用 `g++`。在 VS Code 中打开项目文件夹，然后在项目根目录的终端运行：

```powershell
g++ -std=c++17 -Iheader_files source_code/course_registration_business_rules.cpp source_code/course_registration_console_demonstration.cpp -o course_registration_console_demonstration.exe
.\course_registration_console_demonstration.exe
```

运行测试：

```powershell
g++ -std=c++17 -Iheader_files source_code/course_registration_business_rules.cpp automated_tests/course_registration_business_rules_tests.cpp -o course_registration_business_rules_tests.exe
.\course_registration_business_rules_tests.exe
```

生成的 `.exe` 文件是本机编译结果，不需要提交到 GitHub。

## 建议阅读顺序

1. 阅读 `header_files/course_registration_business_rules.hpp`，了解课程、教学班和选课记录等数据结构，以及可以调用的功能。
2. 阅读 `source_code/course_registration_business_rules.cpp`，了解选课和退课规则。
3. 运行并阅读 `source_code/course_registration_console_demonstration.cpp`，看看示例数据和运行结果。
4. 阅读 `automated_tests/course_registration_business_rules_tests.cpp`，了解各规则的测试例子。

## 小组协作

组员在各自的分支上开发并提交代码，推送到 GitHub 后创建 Pull Request，方便其他组员查看和讨论。合并前一起检查代码能否编译、运行以及与其他功能是否衔接。每周在飞书表格中如实记录代码进度和相关链接。
