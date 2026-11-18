# 持久化设计（JSON 快照）

> 负责人：Zhang Yikai。模块：`header_files/persistence_service.hpp`、
> `source_code/persistence_service.cpp`、`automated_tests/persistence_tests.cpp`。
> 共享头改动见 `interface_change_request.md` 第七节，请 Yuhua / Zhu 复核。

## 一、目标

`SystemContext` 原先只在内存里，进程退出即丢失。本模块补一层持久化：

- 启动时把 `state.json` 载入 `SystemContext`；
- 保存时先校验整份数据，再原子写盘；
- 写失败 / 数据损坏时**不破坏原数据**，把原因返回调用方。

## 二、决策：单个 JSON 文件，不用 SQLite

| 方案 | 结论 |
|---|---|
| 单个 JSON 快照 | ✅ 采用 |
| SQLite | 不采用（建表、迁移、链接库，对课设规模收益不大） |

实现不引入第三方库，自带一个只够本文件使用的 JSON 读写子集
（含 `\uXXXX` 与 UTF-16 代理对）。

## 三、文件与启动

- 默认路径：`data/state.json`（相对进程当前工作目录）。
- 辅助文件：`state.json.tmp`（写入中）、`state.json.bak`（上一份有效版本）。
- 启动（`initialize_storage()`）：
  1. 文件存在 → 载入（解析 + 校验）；
  2. 文件不存在 → 用 `make_seed_context()` 准备种子数据，**写盘成功后**才替换
     `out_context`；
  3. 目录按 `StorageOptions::create_dirs` 决定是否创建。

## 四、JSON 格式

```json
{
  "schema_version": 1,
  "registration_rule": {
    "registration_open": false, "maximum_credits": 18,
    "opens_at": "2026-11-20T09:00", "closes_at": "2026-11-27T17:00"
  },
  "round_opened": false,
  "courses": [
    { "course_id": 1, "course_code": "SE101",
      "course_name": "Software Engineering", "credits": 3 }
  ],
  "teaching_classes": [
    { "teaching_class_id": 10, "course_id": 1, "teacher_id": 100, "capacity": 2,
      "meetings": [ { "day_of_week": 1, "start_minute": 540, "end_minute": 600, "room": "A101" } ] }
  ],
  "students": [
    { "student_id": 1001, "student_number": "S2023001", "name": "Alice Chen",
      "major": "Computer Science", "account_active": true }
  ],
  "teachers": [
    { "teacher_id": 100, "teacher_number": "T2015007", "name": "Dr. Wang",
      "department": "Software Engineering", "account_active": true }
  ],
  "administrators": [
    { "administrator_id": 900, "account_number": "A2020001", "name": "Admin Sun", "account_active": true }
  ],
  "enrollments": [
    { "student_id": 1001, "teaching_class_id": 10, "status": "active" }
  ]
}
```

**不保存**派生值（余量、人数、课表、active 列表）与 `fixed_now_`（测试专用）。

## 五、接口

```cpp
struct StorageOptions {
    std::string path = "data/state.json";
    bool create_dirs = true;
};

OperationResult initialize_storage(const StorageOptions&, SystemContext& out);
OperationResult load_state(const StorageOptions&, SystemContext& out);
OperationResult save_state(SystemContext&, const StorageOptions&);   // 非 const
OperationResult restore_state(const StorageOptions&, SystemContext& out);
OperationResult validate_state(const SystemContext&, std::vector<std::string>& problems);
SystemContext make_seed_context();
```

`save_state` 取非 const 引用，是为了在导出前先调用
`SystemContext::observe_registration_round()`，把"本轮已开放"的事实落盘。

**副作用**：该调用会更新**传入对象**的 `round_opened`。因此调用方必须传
临时状态（`staging`），业务操作与保存都成功后，才把 `staging` 发布为正式共享状态；
失败时丢弃 `staging`，正式状态不受影响。

## 六、保存与恢复的原子性 / 备份安全

**保存**（`save_state`）：

1. `observe_registration_round()` → 更新**传入对象**的 `round_opened`（副作用）；
2. `validate_state()` → 不通过则返回 `StorageIntegrityError`，不写文件；
3. 序列化 → 写 `state.json.tmp`；
4. 旧主文件**仍能解析并通过校验**时，才复制为 `.bak`（否则保留旧 `.bak`）；
5. `MoveFileEx(REPLACE_EXISTING)`（Windows）或 `rename` 原子替换。

**恢复**（`restore_state`）：

1. 读 `.bak` → 解析 → 校验；
2. 写 `state.json.tmp` → 原子替换主文件；
3. **不再触碰 `.bak`**（不执行"主文件 → 备份"这一步）。

因此：主文件损坏或恢复失败时，`.bak` 始终有效，可以反复重试；
即使主文件被占用导致替换失败，`.bak` 也不会被破坏。

## 七、加载校验规则（`validate_state`）

加载不能重放 `enroll()`，但必须按现有业务入口的规则验证整份数据：

**课程**
- `course_id > 0`；`course_code`、`course_name` 非空白；`credits > 0`；
- `course_code` 不区分大小写唯一；
- 每个 `Course` 至少有一个 `TeachingClass`。

**教学班**
- `teaching_class_id > 0`；`capacity > 0`；至少一段 `meetings`；
- 每段 meeting：day∈[1,7]、0 ≤ start < end ≤ 1440；
- `course_id`、`teacher_id` 必须存在；
- 同一教师的不同教学班不得在同一时段重叠（与 `create_teaching_class` 一致）。

**三类人员**
- 内部数字 ID > 0，且只在各自集合内唯一（不要求跨角色全局唯一）；
- 学号 / 工号 / 管理员账号、姓名非空白；
- 账号按 `normalize_account_key`（去掉首尾空白再转小写）比较，**角色内与跨角色**
  都不得重复；停用账号（`account_active=false`）仍占用其账号；
  维护入口的 `find_*_by_number` 与后续登录匹配使用同一函数。

**选课记录**
- 引用的学生、教学班必须存在（**Active 与 Dropped 都检查**）；
- 同一学生同一教学班不得有多个 `active`；
- 同一学生不得同时选同一门 `Course` 的两个教学班（Dropped 不算当前选课）；
- 每个教学班有效选课数 ≤ 容量；
- 同一学生的不同教学班之间不得同一天时段重叠（**同一教学班内部的重叠仍允许**）；
- 每个学生的有效选课学分合计 ≤ `maximum_credits`。

**选课规则**
- `maximum_credits > 0`；
- `opens_at` / `closes_at` 要么都为空（手动开关），要么都是合法的
  `YYYY-MM-DDTHH:MM`（按真实日历，含闰年）且 `opens_at < closes_at`；
  只设置一个视为非法。

全部通过后才替换运行状态；`save_state` 与 `load_state` 使用同一套校验。


## 八、调用方流程（选退课接入）

1. `SystemContext staging = context;`（临时副本）
2. `OperationResult changed = enroll()/drop()`；失败 → 丢弃 staging，返回 changed；
3. `OperationResult saved = save_state(staging, options)`（先 observe + 校验）；
   失败 → 丢弃 staging，返回 saved，`context` 不变；
4. 两步都成功 → `context = std::move(staging)`。

## 九、编译支持（实测）

- **实际验证**：GCC 13.2.0（Windows），命令
  `powershell -ExecutionPolicy Bypass -File .\build_and_test.ps1`，
  输出 `ALL BUILD AND TESTS PASSED`；`-DNDEBUG` 下 `persistence_tests` 同样通过。
- **MSVC**：按 Zhu / Yuhua 的复测可以通过（本机未实测）。
- **MinGW 8.1.0**：仍在 `<filesystem>` 头文件处编译失败，追加 `-lstdc++fs`
  不能解决；因此本模块要求 **GCC 9+ / MSVC 2017+**，**不支持 MinGW 8.1**。
- **CMake**：目标已加（GCC < 9 自动链接 `stdc++fs`），但本机没有 cmake，**未实测**。

## 十、测试

`automated_tests/persistence_tests.cpp` 的关键检查使用 `CHECK` 宏（不依赖
`assert`），发布构建也不会跳过。覆盖：

- 保存/载入往返（Active/Dropped、`account_active`、`round_opened`）；
- 主文件损坏时恢复成功、`.bak` 不被覆盖、可重复恢复；
- 主文件被占用时恢复失败且 `.bak` 保留，释放后可再次恢复；
- 七条业务约束各自被拒绝，且无效状态不会被 `save_state` 写入；
- 学分累加溢出（`2147483647 + 5`）被拒绝、单门课已超上限被拒绝、
  总学分恰好等于上限通过、超过上限被拒绝（保存与加载共用校验）；
- 载入拒绝违反约束的 JSON；
- 整数超出 `int` 范围被拒绝（不静默截断）；
- `\uD83D\uDE00` 正确解析为 4 字节 UTF-8，再次保存仍有效；
- 自动开放后保存 → 重启 → 学分上限仍锁定；
- 首次初始化失败保留调用方状态；保存/读取/损坏/schema/无备份等失败路径。

## 十一、待确认

- [ ] 是否同意 `save_state` 改为非 const（为了保存前 observe）？
- [ ] 是否同意账号统一为 trim+lower（已改 `find_*_by_number`，见
      `interface_change_request.md` 第八节）？
- [ ] 累计学分已改用 `long long`；核心 `enroll()` 与管理员的
      `update_course()` 仍是 `int` 累加，存在同样的溢出，需按分工另行处理。
- [ ] 默认路径 `data/state.json` 是否合适？
