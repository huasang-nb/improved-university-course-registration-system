# 接口提案：「课程 + 首个教学班」原子创建（待三人对齐）

状态：**提案，尚未实现**。提出人 Zhu Ziliang，日期 2026-11-07。
依据：URS 6.7「每门 Course 至少有一个 TeachingClass；创建课程时须提供教学班」，
以及 2026-11-07 的《业务逻辑与后续需求核对》第一节。

## 一、为什么需要新接口

现有 `create_course()` 只接收 `Course`，不带教学班，因此可以先落课程再落班；如果第二步失败
（时段非法、教师不存在、教师时段冲突、停用教师），就会留下一个没有教学班的课程。

这是"两个公开调用 + 无事务"的问题，靠网页表单必填解决不了：业务接口本身允许只写课程。
所以需要**一个调用同时校验并保存两者**，任一失败则两者都不写。

## 二、本次已经做的部分（不改接口）

同一分支 `feature/zhu-ziliang-admin-consistency` 已提交：

| 改动 | 位置 | 失败返回 |
|---|---|---|
| 删除课程最后一个教学班被拒绝 | `remove_teaching_class()` | `CatalogInUse` |
| 把课程最后一个教学班移到别的课程被拒绝 | `update_teaching_class()` | `CatalogInUse` |
| 一致性检查报告零教学班课程 | `check_data_integrity()` | 问题列表含 `course XXX has no teaching class` |

这三处都不改变函数签名，只加校验，因此无需等接口对齐。

## 三、提议的接口（三选一）

### 方案 A（推荐）：新增一个聚合入口，保留旧入口

```cpp
OperationResult create_course_with_teaching_class(
    const UserIdentity& user,
    const Course& course,
    const TeachingClass& first_teaching_class);
```

- 校验：`course` 的全部字段 + `first_teaching_class` 的全部字段（含 `course_id` 必须等于
  `course.course_id`、教学班编号唯一、时段合法、教师存在且账号启用、教师无排课冲突）。
- 成功：一次写入课程与首个教学班；**先全量校验，再一次性写**，不出现中间状态。
- 失败：返回既有结果码（`InvalidInput` / `DuplicateIdentifier` / `RecordNotFound` / `TimeConflict` /
  `PermissionDenied`），**课程与教学班都不写**。
- 旧的 `create_course()` 建议改为 `internal` 或标注为"仅供导入历史数据"，
  避免业务代码再走"只建课程"的路径。

### 方案 B：旧入口增加可选参数

```cpp
OperationResult create_course(const UserIdentity& user, const Course& course,
                              const TeachingClass* first_teaching_class = nullptr);
```

改动小，但 `nullptr` 语义弱、容易漏传，且无法表达"必须提供"的约束。不推荐。

### 方案 C：先建"草稿课程"再提交

需要引入草稿状态与提交步骤，等于新增一套状态机，超出本版范围。不推荐。

## 四、对存储与恢复的影响（需要 Yikai 确认）

1. 恢复入口应按"课程 + 其教学班"成批写入，或者恢复完成后立即跑一遍
   `check_data_integrity()`，把零教学班、孤儿记录当作恢复失败处理。
2. 保存时若课程与教学班分两条语句写入，需要同一事务或先写临时表；当前内存版没有这个问题，
   落盘时要一起考虑。
3. 恢复历史数据（`Dropped` 记录、停用账号）时必须保留原编号，不能再走 `enroll()` 重放。

## 五、需要三人回应的四个问题

1. 采用方案 A、B 还是 C？（建议 A）
2. `create_course()` 是否保留为公开接口？若保留，是否允许它产生零教学班课程
   （当前会，但一致性检查会报告）。
3. **删除课程的语义**：现在"删除课程"要求先没有教学班，而"每门课程至少一个教学班"又禁止删掉最后一个班，
   两者叠加后，正常数据下课程实际上删不掉。是否需要让 `remove_course()` 在没有选退课记录时
   **连同其教学班一起删除**？（这属于新增语义，需要你们先定。）
4. 跨角色账号唯一性：本次不做，等你（Yuhua）与 Yikai 定规则。我的分工是人员维护入口
   （`create_student` / `create_teacher` 等）按最终规则查重，你负责登录时的匹配与身份发放。
   需要明确：账号标识（学号/工号）是否全局唯一、数值 ID 是否要求跨角色唯一。

## 六、过渡期的临时约定

在新接口落地前：

- 网页/接口层必须先调 `create_course()` 再调 `create_teaching_class()`，第二步失败时**回滚第一步**
  （删除刚建的课程），否则会留下零教学班课程。
- `check_data_integrity()` 会报告这类课程，便于演示前自查。
- 演示与测试数据不构造零教学班课程。
