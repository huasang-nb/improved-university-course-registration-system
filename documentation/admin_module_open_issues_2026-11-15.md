# 管理员模块：接口、持久化接入与待确认事项

- 最后更新：2026-11-19（替换此前指向旧分支/旧提交的说明）
- 负责人：Zhu Ziliang（教师端、管理员维护、权限、静态模型）
- 分支：`feature/zhu-ziliang-admin-persistence`（基于 `origin/feature/yikai-persistence`）
- 最新提交：以本轮推送后的 HEAD 为准（见提交说明与 PR 链接）

> 本文件只描述**当前状态**。已经完成并复核的历史改动（账号查重、课程删除级联、
> Dropped 悬空检查）保留结论，不再重复"待实施"的描述。

## 1. 持久化：现状与入口

存储由 Kai 的 `persistence_service`（JSON 快照、原子写、备份恢复）提供，
管理员模块不再"等接口"。

| 能力 | 位置 | 状态 |
|---|---|---|
| 保存整份状态 | `save_state(SystemContext&, StorageOptions)` | 已接入管理员修改流程 |
| 载入 / 恢复 | `load_state` / `restore_state` | 由启动与恢复流程调用 |
| 整份校验 | `validate_state` | Kai 侧实现，与维护入口规则对齐 |
| 本轮锁定事实 | `SystemContext::round_opened()` / `restore_round_opened()` | 随快照保存与恢复 |
| 管理员修改入口 | `PersistentAdministratorService` | 本轮新增，页面与 HTTP 接口应调用它 |

管理员修改流程（`persistent_administration_service.cpp`）：

1. 可信会话身份必须是"当前存在且启用"的管理员（账号、编号、`account_active` 三者都要对）；
2. 在**临时 `SystemContext` 副本**上执行原有 `AdministratorService` 的全部业务校验；
3. 业务通过后调用 `save_state()`；
4. 保存成功才用副本替换正式共享状态；业务拒绝返回原结果码，保存失败返回
   `change not applied: <storage error>`。两种失败都不改正式内存与文件。

覆盖入口：课程 + 首班创建、课程改/整体删、教学班增改删、容量、时段、学分上限、
关闭报名、学生/教师增改删（含启停状态）。

## 2. 课程与首个教学班：原子创建（已实现）

```cpp
OperationResult create_course_with_teaching_class(
    const UserIdentity& user, const Course& course,
    const TeachingClass& first_teaching_class);
```

- 先校验课程字段、编号与代码唯一，再校验首班课程引用、编号唯一、容量、时间、
  教师存在与启用、教师排课冲突；全部通过才写入，任一失败两者都不落库。
- **旧的单独 `create_course()` 已从公开接口移除**（不是注释），正常业务无法再创建零教学班课程。
- 测试与演示改用新入口；损坏数据由测试用底层 `apply_course_change` 构造。
- 恢复/导入历史数据由持久化层整体构造 `SystemContext`，不经过这个入口，因此没有新增导入接口。

## 3. 删除课程：级联规则（已确认并实现）

| 情形 | 处理 | 返回 |
|---|---|---|
| 没有任何选退课记录 | 删课程 + 其全部教学班 | `Success`，消息 `removed together with N teaching class(es)` |
| 有 Active 记录 | 拒绝 | `CatalogInUse`，给出 `(N active, M dropped)` |
| 只有 Dropped 历史 | 拒绝 | `CatalogInUse`，消息说明"退课不会让课程变成可删除" |

`remove_teaching_class()` 的"最后一个班不可单独删"保持不变。

## 4. 账号规则：跨角色唯一（已实现并复核）

- 学号 / 工号 / 管理员账号在三种角色之间唯一，**比较口径 = 去首尾空白 + 转小写 + 整串相等**
  （共享 `normalize_account_key()`；维护入口、恢复校验、登录匹配共用）。
- 新增与修改都检查；修改**只排除被修改的那一个对象**（角色 + 编号），不排除整个角色。
- 管理员账号参与查重（只读）；停用账号仍占用号码。
- `check_data_integrity()` 会报历史重名；也报告 Dropped 记录缺学生或缺教学班、零教学班课程等。

## 5. 一致性检查覆盖

零教学班课程、跨角色重名账号、Active 记录缺学生/缺教学班、Dropped 记录缺学生/缺教学班、
容量小于有效人数、同班重复有效记录、同课程多班、学生课表冲突、教师排课冲突。
正常的 Dropped 记录（教学班仍在）不算问题。

## 6. 仍待确认 / 不在本次范围

| 事项 | 状态 |
|---|---|
| 原班教师被停用后，管理员能否修改该班时间与容量 | **待确认**。当前实现按"不扩大限制"处理：原教师不变时允许维护（容量下限、时间冲突等原有检查继续生效）；新建教学班或改派给停用教师仍拒绝。**未经批准，不要写成已批准** |
| 内部数字 ID 是否跨角色唯一 | 未确认，保持现状（允许不同角色相同数字 ID） |
| 管理员账号维护页面 | 不在本次范围 |
| 课程归档 / 停用 | 不在本次范围 |

## 7. 测试与构建

- `build_and_test.ps1`：g++ 目标，本机 GCC 8.1 无法编译持久化目标（`<filesystem>`）。
- MSVC 2022 实测：8 个目标编译通过；5 套测试通过
  （管理员 483 项、管理员持久化 89 项，选课核心 / 学生查询 / Kai 的持久化全绿）。
- CMake（VS 2022 生成器）：`Release` 与 `Debug` 均构建通过，`ctest` 5/5 通过，
  其中包含 `persistent_administration`。
- 证据与命令：`documentation/admin_test_evidence_2026-11-19.txt`、
  `documentation/admin_persistence_integration.md`。
