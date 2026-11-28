# Improved University Course Registration System

## 改进型大学选课系统 · Team 4

2026-11-19 持久化阶段整合候选版本。已整合三人的 C++17 业务模块、JSON 整库快照、选退课及管理员修改的持久化入口。最终浏览器页面、真实密码登录与会话、Web 并发协调仍待完成。

## 成员与接入版本

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

Windows PowerShell 中运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1 -Configuration Release
```

脚本查找 PATH 或 Visual Studio 安装中的 CMake，产物位于 `build/storage`。本次使用 Visual Studio 2022 / MSVC 19.42，Debug 9/9 套、Release 7/7 套通过。Release 排除两个依赖 assert 的旧测试，这两套以 Debug 结果为准。GCC 9+ 为现有文档支持范围，本轮未实测；本机 MinGW 8.1 不支持持久化目标。

| 测试 | 本轮结果 |
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

原班教师停用后，管理员能否继续改该班时间和容量，用户于 11 月 19 日选择继续待讨论。候选版本保留当前允许行为及已有检查，不将其记录为已确认政策；新建班或改派给停用教师仍拒绝。内部数字 ID 跨角色唯一、课程归档/停用、管理员账号维护页面均未在此次整合中扩展。

当前仍是同步单进程业务原型。上线 Web 前须让全部读写入口共同协调并发，完成真实登录、三端页面与浏览器完整流程；本次测试不代表并发或断电耐久性验收。运行数据、临时快照、备份、exe 与 build 目录不进入提交。

接口与运行细节见 [选退课接入](documentation/registration_storage_integration.md)、[管理员接入](documentation/admin_persistence_integration.md) 和 [共享存储](documentation/persistence_integration_guide.md)。
