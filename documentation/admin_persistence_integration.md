# 管理员修改的持久化接入（Zhu Ziliang，2026-11-19）

## 1. 为什么需要这一层

`AdministratorService` 直接修改共享内存：一次操作成功即生效，落盘失败时无法回滚。
本层把"业务校验"与"保存"合成一个整体，页面和后续接口应调用它，而不是直接
`new AdministratorService`。

## 2. 接口

```cpp
#include "persistent_administration_service.hpp"

StorageOptions options;                    // 默认 data/state.json
SystemContext context = /* initialize_storage / load_state 得到的正式状态 */;

PersistentAdministratorService admin(context, options);

// 示例：课程 + 首个教学班一起创建
OperationResult r = admin.create_course_with_teaching_class(
    session_identity,                       // 来自可信登录会话，不是请求自称
    Course{4, "PH101", "Physics", 3},
    TeachingClass{40, 4, 100, 2, {{4, 600, 660, "A201"}}});
```

覆盖的入口（全部走同一流程）：

| 类别 | 方法 |
|---|---|
| 课程与教学班 | `create_course_with_teaching_class`、`update_course`、`remove_course`、`create_teaching_class`、`update_teaching_class`、`remove_teaching_class`、`set_teaching_class_capacity` |
| 选课规则 | `set_registration_period`、`set_maximum_credits`、`close_registration` |
| 人员资料 | `create_student`、`update_student`、`remove_student`、`create_teacher`、`update_teacher`、`remove_teacher` |

只读访问：`admin.context()` 返回正式共享状态，教师端与学生端读取的就是同一份。

## 3. 执行流程与失败语义

1. **身份校验**：`actor.role` 必须是管理员，且账号能在当前管理员名单中找到、
   编号一致、`account_active == true`。教师、学生、伪造账号、已停用管理员一律
   `PermissionDenied`，不进入业务逻辑。
2. **临时状态**：复制正式 `SystemContext`，在副本上执行原 `AdministratorService` 的全部校验。
3. **业务拒绝**：直接返回原结果码（如 `CatalogInUse`、`CreditLimitExceeded`），正式状态与文件都不变。
4. **保存**：调用 `save_state(staging, options)`（Kai 的原子写：临时文件 + 备份 + 替换）。
5. **发布**：只有保存成功才用副本替换正式状态；保存失败返回
   `"change not applied: <storage error>"`，正式状态与文件都不变。

## 4. 多对象与历史状态

- 课程与首个教学班：任一校验失败，两者都不写入；保存失败同样两者都不发布。
- 删除课程：无选退课记录时课程与其全部教学班整份保存；有 Active 或 Dropped 记录时拒绝，
  提示明确说明"退课不会让课程变成可删除"。
- 设置时段 / 关闭 / 改期：`save_state` 会先观察"本轮是否已开放"，该历史事实随状态写入，
  重启后 `set_maximum_credits` 仍然被拒绝（有测试覆盖）。

## 5. 编译环境

| 环境 | 状态 |
|---|---|
| MSVC 2022（19.4x，`cl /std:c++17 /utf-8`） | **实测通过**：8 个目标全部编译，5 套测试通过 |
| GCC 13.2 | Kai 在其机器上实测通过（持久化模块要求 GCC 9+） |
| GCC 8.1（MinGW-w64） | **不支持**：`<filesystem>` 头文件编译失败，仅能编译非持久化目标 |

`/utf-8` 是必需的：源码含中文注释，缺省代码页（936）会误读。

## 6. 测试命令与结果（MSVC 2022）

```powershell
# 逐目标编译（示例：管理员持久化测试）
cl /nologo /utf-8 /std:c++17 /EHsc /Iheader_files /Fo:build\msvc\ /Fe:build\msvc\persist_admin_tests.exe `
   source_code\course_registration_business_rules.cpp `
   source_code\teacher_and_administration_services.cpp `
   source_code\persistence_service.cpp `
   source_code\persistent_administration_service.cpp `
   automated_tests\persistent_administration_tests.cpp

# 运行
.\build\msvc\persist_admin_tests.exe
```

2026-11-19 实测：

| 目标 | 结果 |
|---|---|
| `course_registration_business_rules_tests` | All registration tests passed（exit 0） |
| `teacher_and_administration_services_tests` | **483 checks, 0 failures**（exit 0） |
| `student_query_and_timetable_tests` | All student query and timetable tests passed（exit 0） |
| `persistence_tests`（Kai） | All persistence tests passed（exit 0） |
| `persistent_administration_tests`（本层） | **89 checks, 0 failures**（exit 0） |
| 三个演示程序 | exit 0 |
| 上述两套自研测试的 `-DNDEBUG` 构建 | 分别 483 / 89 checks，0 failures |

失败路径已验证会返回非零：故意把学分累计改回 `int` 时，测试打印 `FAIL` 并返回 1。

## 7. 新增测试覆盖

| 场景 | 断言重点 |
|---|---|
| 原子创建成功 | 正式状态已发布；重新加载后课程编号/代码/学分、教学班关联/教师/容量/时间一致 |
| 课程非法、首班非法 | 返回 `InvalidInput`；课程与教学班数量不变；文件字节不变 |
| 保存路径不可写 | 返回 `"change not applied: ..."`；学生未创建；原文件字节不变 |
| 主文件被占用 | 保存失败、正式状态不变；解锁后同一操作成功（证明失败只与占用有关） |
| 身份校验 | 学生/教师/未知账号/编号不符 → `PermissionDenied`；停用管理员 → `PermissionDenied` |
| 整体删除 | 无历史时成功且重新加载后课程与班都消失、其他课程保留；Active / Dropped 时拒绝 |
| 时段与锁定 | 设置时段并保存后重新加载，`round_opened` 仍为真，改学分上限仍被拒绝 |
| 人员资料 | 新增/改启停/删除后重新加载，编号、姓名、启停状态一致 |
| 跨端一致 | 管理员改容量后，教师端 `class_summary`、`remaining_seats` 与重新加载后的结果一致 |

## 8. 仍未完成 / 未确认

- **停止教师的原班时间与容量**能否修改：政策未确认；当前实现不扩大限制（允许）。
- **内部数字 ID** 是否跨角色唯一：未确认，未改动。
- **管理员账号维护界面**：不在本次范围（只读参与查重）。
- `build_and_test.ps1` 走 g++，本机 8.1 无法编译持久化目标；在 GCC 9+/MSVC 环境可直接跑通。
  本文件第 6 节给出 MSVC 的等价命令。
