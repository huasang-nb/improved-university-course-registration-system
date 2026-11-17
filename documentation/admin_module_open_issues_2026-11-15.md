# 管理员模块：保存接口与三项待确认方案

- 日期：2026-11-15
- 提出人：Zhu Ziliang（教师端、管理员维护、权限、静态模型）
- 状态：**待讨论**，其中第 2、3 节需要三人确认后才动接口
- 分支：`feature/zhu-ziliang-admin-consistency`（HEAD `f6cd49a`，已推送，未合入 main）
- 相关文档：[interfaces.md](interfaces.md)、[interface_change_request.md](interface_change_request.md)、
  [interface_proposal_course_with_teaching_class.md](interface_proposal_course_with_teaching_class.md)

## 0. 事实基线（2026-11-15 核对）

| 事项 | 核对结果 |
|---|---|
| 保存/恢复接口 | **目前不存在**。`main` 与 Yikai 分支上都没有 storage 相关文件 |
| 现有只读快照 | `RegistrationService::course_snapshot()`、`teaching_class_snapshot()`；`all_enrollments()` 只读 |
| 数据存放 | 全部在内存：`SystemContext` 持有人员，`RegistrationService` 持有课程、教学班、选课记录、规则 |
| 管理员账号维护 | **没有**入口（`AdministratorService` 只有学生、教师），跨角色查重目前三缺一 |
| 课程删除 | `remove_course()` 要求 0 个教学班；`remove_teaching_class()` 又禁止删最后一个班 → 正常数据下课程删不掉 |
| 测试 | `.\build_and_test.ps1` 全过；教师/管理员 **383 checks / 0 failures**；选课规则与学生查询两套全绿 |

## 1. 修改后怎样调用保存接口，哪些操作需要一起保存

管理员服务不直接碰存储：服务内部改内存，由 Web 控制器在**一次请求成功后**统一提交。

建议 Yikai 提供二选一：

```cpp
OperationResult save_all(const SystemContext& ctx);          // 整库快照，最简
// 或者
StorageTransaction tx = storage.begin();  ...  tx.commit();   // 事务式，失败回滚内存
```

需要**多项一起写**的操作（否则出现半写状态）：

| 操作 | 必须一次写完 | 原因 |
|---|---|---|
| 课程 + 首个教学班创建（待新增） | Course + TeachingClass | 只写课程会留下零教学班课程 |
| 删除课程（若采纳级联，见第 3 节） | Course + 其全部 TeachingClass | 否则留下孤儿教学班 |
| 设置选课时段 | `opens_at` / `closes_at` + `registration_open` + **本轮锁定事实** | `round_opened_` 是 `SystemContext` 私有内存，不持久化则重启后锁定丢失 |
| 改容量 / 改学分上限 | RegistrationRule 一条 | 单对象，但仍需在同一事务内 |
| 改派教师 / 改教学班 | TeachingClass 一条 | 单对象 |
| 人员维护 | Student / Teacher 一条 | 单对象 |
| 恢复历史数据 | 学生 + 其 Active/Dropped 记录 + 课程 + 教学班 + 规则 | 不能走 `enroll()` 重放，会按当前时段与容量重做校验 |

**对 Yikai 的硬要求**：若接口是"按对象增量写"（`save_course` / `save_teaching_class` 分开），
上表前两行无法保证原子性，需要至少提供整库或事务级入口。

## 2. 「课程 + 首个教学班」采用哪个接口方案

建议**方案 A**（详见 [interface_proposal_course_with_teaching_class.md](interface_proposal_course_with_teaching_class.md)）：

```cpp
OperationResult create_course_with_teaching_class(
    const UserIdentity& user,
    const Course& course,
    const TeachingClass& first_teaching_class);
```

- 先全量校验：课程字段、教学班编号唯一、`course_id` 一致、时段合法、教师存在且账号启用、无排课冲突；
- 再一次性写入；任一失败则**两者都不写**；
- 返回既有结果码：`InvalidInput` / `DuplicateIdentifier` / `RecordNotFound` / `TimeConflict` / `PermissionDenied`。

**旧入口 `create_course()` 的处理（分三步，不一次删干净）**

1. 保留，注释标注 `internal: 仅用于导入历史数据`；
2. Web 层与演示程序改调新接口，不再调用旧入口；
3. 等导入路径确定后再降为 private。

> 现在直接删会打断 3 处测试：`test_course_requires_at_least_one_teaching_class`
> 用 `create_course()` 构造了零教学班课程来演示一致性报告。

不推荐方案 B（可选参数 `nullptr`，语义弱、易漏传）与方案 C（草稿课程，等于新增状态机）。

## 3. 课程删除的死锁与建议

**建议：删课程走级联，删班仍禁止动最后一个班** —— 两者管的不是同一件事。

| 情形 | 处理 | 返回 |
|---|---|---|
| 课程下没有任何选退课记录 | 允许：一次事务内删课程 + 其全部教学班 | `Success`，消息写明"已连同 N 个教学班删除" |
| 记录全部为 Dropped | 拒绝（历史必须保留） | `CatalogInUse`，消息给出 active / dropped 条数 |
| 存在 Active 记录 | 拒绝 | `CatalogInUse`，提示先让学生退课 |
| 课程本来没有教学班（历史脏数据） | 允许直接删 | `Success` |

实现要点：`remove_course()` 先统计该课程的班数；为 0 直接删；大于 0 时用 `all_enrollments()`
判断这些班是否有记录，没有才逐个 `remove_teaching_class_record()` 再 `remove_course_record()`。
`remove_teaching_class()` 的"最后一个班不可删"规则保持不变。

备选：新增"停用课程"标志代替物理删除。这属于新增需求，本版不建议引入。

## 4. 跨角色账号查重：已实现（2026-11-17）

**更新：本节原为"待实施"，现已落地。** 实现见提交 `c0ffee1`（后续提交号以实际为准），
入口 `SystemContext::find_account_owner()`。

**改动前**：只做到各自角色内（student_number 只在学生里查，teacher_number 只在教师里查）。

**改动后**

| 入口 | 现在的检查 |
|---|---|
| `create_student` | 自身 id/学号唯一 + **跨角色查重**（学生、教师、管理员三类账号） |
| `create_teacher` | 自身 id/工号唯一 + **跨角色查重** |
| `update_student` / `update_teacher` | 角色内查重、排除自身 + **跨角色查重**（同样排除自身） |
| `check_data_integrity` | **新增**"account number X collides with Y"问题项，可检查历史重名 |
| 管理员账号 | 参与查重（读取 `administrators_`）；但仍**没有维护接口**，只能由种子数据/导入产生 |

- 账号比较**大小写不敏感**（`T2015007` 与 `t2015007` 视为同一账号）。
- 冲突统一返回 `DuplicateIdentifier`，消息形如
  `student_number T2015007 is already used by teacher T2015007`。

登录侧（Yuhua）：登录时按账号在三种角色里查，命中多条应报数据错误，不要静默取第一条。

**内部数字 ID**：继续待确认。若要求全局唯一，需统一 ID 空间与生成规则，属于另一件改动。

## 5. 分支、测试与配合事项

| 项 | 现状 |
|---|---|
| 分支 | `feature/zhu-ziliang-admin-consistency` |
| 最新提交 | `f6cd49a`（已推送；领先 main 1 个提交，**未合入 main**；main 当前 `b9b52df`） |
| 提交内容 | 课程至少保留一个教学班、停用教师不可改派、有选退课记录的学生禁止删除 |
| 测试 | `.\build_and_test.ps1` 全过：三套测试；教师/管理员 383 项断言、0 失败 |

**需要 Yuhua 确认**

1. 合并 `f6cd49a`（目前只在我的分支上）。
2. 课程删除语义：采纳级联方案（推荐）还是其他处理。
3. 跨角色账号查重按第 4 节落地；并确认登录侧命中多条记录时**报错**而非取第一条。
4. `round_opened_`（本轮锁定事实）是否随规则一起持久化——影响缺陷-010 在重启后的表现。

**需要 Yikai 配合**

1. 保存/恢复入口形态：整库快照还是事务；能否保证"多对象一次写"。
2. 恢复时成批写入课程+教学班、学生+记录、规则+锁定事实，恢复后跑 `check_data_integrity()` 兜底。
3. 原子创建接口落地后，导入路径不要再用 `create_course()` 单独建课程。
4. 新分支 `feature/zhang-yuhua-registration-acceptance` 的 574 项联调检查若调用了
   `create_course()`，需要一起改。

**我的排期**：原子创建接口（等三人确认）→ 删课程级联（等确认）→ 跨角色查重（等登录规则）。
三件都确认后按顺序提交，每件单独一个提交，便于分别复核。
