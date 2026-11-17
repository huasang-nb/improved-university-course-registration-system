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

## 六、保存与恢复的原子性 / 备份安全

**保存**（`save_state`）：

1. `observe_registration_round()` → 更新 `round_opened`；
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

加载不能重放 `enroll()`，但必须验证整份数据符合现有约束：

- 每个 `Course` 至少有一个 `TeachingClass`；
- 每个 `TeachingClass` 至少有一段 `meetings`，且 day∈[1,7]、start<end；
- 账号唯一：学生学号、教师工号、管理员账号在**角色内与跨角色**都不得重复
  （大小写不敏感）；内部数字 ID 只要求角色内唯一；
- 每个教学班的有效选课数 ≤ 容量；
- 同一学生同一教学班不得有多个 `active`；
- 同一学生的不同教学班之间不得同一天时段重叠（**同一教学班内部的重叠仍允许**）；
- 每个学生的有效选课学分合计 ≤ `maximum_credits`；
- 选课记录引用的学生、教学班必须存在；教学班引用的课程、教师必须存在。

全部通过后才替换运行状态。

## 八、调用方流程（选退课接入）

1. `SystemContext staging = context;`
2. 在 staging 上调用 `enroll()` / `drop()`；
3. `save_state(staging, options)`（该调用会先 observe + 校验）；
4. 成功后才 `context = std::move(staging)`；
5. 失败丢弃 staging，`context` 不变。

## 九、编译支持

- **GCC 8.1+**：`std::filesystem` 需要链接 `stdc++fs`；
  `build_and_test.ps1` 已对持久化目标自动追加 `-lstdc++fs`，
  CMake 在 GCC < 9 时自动链接 `stdc++fs`。
- **MSVC 2017+**：直接编译，无需额外链接。
- 本机用 GCC 13.2 验证；MinGW 8.1 的链接问题按上述方式处理。

## 十、测试

`automated_tests/persistence_tests.cpp` 的关键检查使用 `CHECK` 宏（不依赖
`assert`），发布构建也不会跳过。覆盖：

- 保存/载入往返（Active/Dropped、`account_active`、`round_opened`）；
- 主文件损坏时恢复成功、`.bak` 不被覆盖、可重复恢复；
- 主文件被占用时恢复失败且 `.bak` 保留，释放后可再次恢复；
- 七条业务约束各自被拒绝，且无效状态不会被 `save_state` 写入；
- 载入拒绝违反约束的 JSON；
- 整数超出 `int` 范围被拒绝（不静默截断）；
- `\uD83D\uDE00` 正确解析为 4 字节 UTF-8，再次保存仍有效；
- 自动开放后保存 → 重启 → 学分上限仍锁定；
- 首次初始化失败保留调用方状态；保存/读取/损坏/schema/无备份等失败路径。

## 十一、待确认

- [ ] 是否同意 `save_state` 改为非 const（为了保存前 observe）？
- [ ] 七条校验规则是否符合团队约定（尤其"同一教学班内部重叠允许"）？
- [ ] 默认路径 `data/state.json` 是否合适？
