# 持久化设计（JSON 快照）

> 负责人：Zhang Yikai。模块：`header_files/persistence_service.hpp`、
> `source_code/persistence_service.cpp`、`automated_tests/persistence_tests.cpp`。
> 本次同时改到两处共享头文件，改动清单见 `interface_change_request.md` 第七节，请 Yuhua / Zhu 复核。

## 一、目标

当前 `SystemContext` 全部在内存里，进程退出即丢失。本模块补一层持久化：

- 启动时把 `state.json` 载入 `SystemContext`；
- 每次成功修改后把整份快照写回；
- 写失败 / 数据损坏时**不破坏原数据**，把原因返回调用方。

## 二、决策：单个 JSON 文件，不用 SQLite

| 方案 | 结论 |
|---|---|
| 单个 JSON 快照 | ✅ 采用 |
| SQLite | 不采用（建表、迁移、链接库，对课设规模收益不大） |

理由：状态本来就在 `SystemContext` 里；JSON 可读、可 diff、可进仓库；
规模小，读一次、写一次就够，不需要事务数据库。实现不引入第三方库，
自带一个只够本文件使用的 JSON 读写子集。

## 三、文件位置与启动

- 默认路径：`data/state.json`（相对工作目录；`data/` 进仓库，`build/` 不进）。
- 辅助文件：`state.json.tmp`（写入中）、`state.json.bak`（上一版）。
- 启动流程（`initialize_storage()`）：
  1. 文件存在 → 读取 → 解析 → 校验 → 载入；
  2. 文件不存在 → 视为首次运行，用内置 seed（课程 / 教学班 / 三类人员 / 默认规则）
     初始化，并**立即落盘**；
  3. 目录不存在时按 `StorageOptions::create_dirs` 决定是否创建。

## 四、JSON 格式

```json
{
  "schema_version": 1,
  "registration_rule": {
    "registration_open": false,
    "maximum_credits": 18,
    "opens_at": "2026-11-20T09:00",
    "closes_at": "2026-11-27T17:00"
  },
  "round_opened": false,
  "courses": [
    { "course_id": 1, "course_code": "SE101", "course_name": "Software Engineering", "credits": 3 }
  ],
  "teaching_classes": [
    {
      "teaching_class_id": 10, "course_id": 1, "teacher_id": 100, "capacity": 2,
      "meetings": [ { "day_of_week": 1, "start_minute": 540, "end_minute": 600, "room": "A101" } ]
    }
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

**不保存**派生值：`remaining_seats`、`enrollment_count`、`timetable`、`active_enrollments`
都在载入后由 `RegistrationService` / `StudentQueryService` 现算。
**不保存** `fixed_now_`（测试专用）。

## 五、接口

```cpp
struct StorageOptions {
    std::string path = "data/state.json";
    bool create_dirs = true;
};

OperationResult initialize_storage(const StorageOptions&, SystemContext& out);  // 启动
OperationResult load_state(const StorageOptions&, SystemContext& out);          // 载入
OperationResult save_state(const SystemContext&, const StorageOptions&);        // 保存
OperationResult restore_state(const StorageOptions&, SystemContext& out);       // 从 .bak 恢复
SystemContext make_seed_context();                                              // 种子数据
```

`ResultCode` 末尾新增：`StorageIoError`、`StorageParseError`、
`StorageSchemaMismatch`、`StorageIntegrityError`、`StorageBackupMissing`。

## 六、失败处理

| 场景 | 处理 |
|---|---|
| 文件不可写 / 目录不存在且不允许创建 | 写 `.tmp` 失败 → 不动内存，返回 `StorageIoError` + 路径 |
| 写入中途崩溃 | 只有 `.tmp` 不完整，`state.json` 与 `.bak` 保持原样 |
| JSON 损坏 | 不替换 `out_context`，返回 `StorageParseError` |
| schema 版本不认识 | 返回 `StorageSchemaMismatch` |
| 编号重复 / 引用缺失 | 返回 `StorageIntegrityError`，不写任何文件 |
| 没有 `.bak` 时恢复 | 返回 `StorageBackupMissing` |

写入顺序：序列化 → 写 `state.json.tmp` → 备份旧文件为 `.bak` → 原子替换
（Windows 用 `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)`，其他平台用
`filesystem::rename`）。**任何一步失败都不删原文件。**

## 七、调用方（Yuhua / Zhu）怎么接

每次可能落盘的操作，按"临时状态 → 保存 → 提交"走：

1. `SystemContext staging = context;`（深拷贝）；
2. 在 staging 上跑业务检查 / 调用 `RegistrationService::enroll()` 等；
3. `save_state(staging, options)`；
4. 保存成功后才 `context = std::move(staging)`，返回成功；
5. 任一步失败：丢弃 staging，`context` 原样，返回原因。

这样"磁盘成功"和"内存提交"是同一个原子点，崩溃最多丢最后一次未提交的改动。
单进程无并发问题；以后起 Web 服务时用一把 mutex 包住整段即可。

## 八、这次改到的共享头文件（需复核）

1. `RegistrationService::restore_enrollments(std::vector<Enrollment>)`
   —— 载入时直接恢复原始选课记录，**跳过业务检查**（记录在保存前已校验过，
   用 `enroll()` 重放会因时段/容量变化失败）。
2. `SystemContext::round_opened()` / `restore_round_opened(bool)`
   —— 读取/恢复"本轮是否开放过"的历史事实（原来只有 `observe_registration_round()`
   会置 true，没有 setter，载入不回来）。

两处都是**追加式**改动，不改已有函数体。详细 diff 见 `interface_change_request.md` 第七节。

## 九、测试

`automated_tests/persistence_tests.cpp` 覆盖：

- seed 保存/载入往返（课程、教学班、三类人员、规则、round_opened）；
- active + dropped 选课记录往返，余量现算正确；
- 再次保存生成 `.bak`，`restore_state()` 回退到上一版；
- 首次运行自动初始化；
- 无 `.bak` 时返回 `StorageBackupMissing`；
- 损坏 JSON → `StorageParseError` 且不破坏原状态；
- `schema_version` 不认识 → `StorageSchemaMismatch`；
- 学号重复（大小写不同也算）→ `StorageIntegrityError`；
- 选课记录引用不存在的教学班 → `StorageIntegrityError`；
- 父路径是文件导致保存失败 → `StorageIoError` 且不留 `.tmp`。

`.\build_and_test.ps1` 已加入 `persistence_tests`，全部通过时打印
`ALL BUILD AND TESTS PASSED`。

## 十、待确认

- [ ] 持久化方案（JSON 快照）是否认可？
- [ ] 是否同意追加 `restore_enrollments` 与 `round_opened` / `restore_round_opened`？
- [ ] 默认路径用 `data/state.json` 还是 `build/data/state.json`？
- [ ] 集成时是否采用第七节的"临时状态 → 保存 → 提交"流程？
