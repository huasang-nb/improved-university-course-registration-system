# 选退课接入持久化：联调说明

> 分支 `feature/yikai-persistence`。模块：`persistence_service.hpp/.cpp`、`persistence_tests.cpp`。
> 共享头改动见 `interface_change_request.md` 第七节。

## 1. 代码版本与编译

- 新增文件：`header_files/persistence_service.hpp`、`source_code/persistence_service.cpp`、
  `automated_tests/persistence_tests.cpp`、`documentation/persistence_design.md`、本文件。
- 编译/测试：

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\build_and_test.ps1
  # 末行应为 ALL BUILD AND TESTS PASSED
  .\build\persistence_tests.exe
  ```

- 编译器：**GCC 9+ / MSVC 2017+**（本机实测 GCC 13.2.0；MinGW 8.1 在 `<filesystem>`
  头文件处编译失败，不支持）；CMake 目标已加但**未实测**。
- CMake 目标：`persistence_service` / `persistence_tests`（GCC < 9 自动链接 `stdc++fs`）。

## 2. 接口与最小示例

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

```cpp
StorageOptions options;                       // data/state.json
SystemContext context = make_seed_context();  // 换成现有 context
initialize_storage(options, context);

SystemContext staging = context;              // 1. 临时状态（副本）
OperationResult changed = /* 在 staging 上 enroll / drop */;
if (changed.code != ResultCode::Success) {
    return changed;                           // 2. 业务失败：丢弃 staging
}

OperationResult saved = save_state(staging, options);   // 3. observe + 校验 + 写盘
if (saved.code != ResultCode::Success) {
    return saved;                             // 4. 保存失败：丢弃 staging，context 不变
}
context = std::move(staging);                 // 5. 两步都成功才发布

SystemContext loaded = make_seed_context();   // 6. 加载回来
load_state(options, loaded);
restore_state(options, loaded);               // 或从 .bak 恢复
```

> `save_state` 会更新**传入的 staging** 的 `round_opened`；正式共享的 `context`
> 在发布前不会被改动。失败时 `context` 原样，只有 staging 被丢弃。

## 3. 调用职责与状态变化

| 步骤 | 谁做 |
|---|---|
| 复制 `staging = context` | 调用方 |
| 在 staging 上调用 `enroll()` / `drop()` | 调用方 |
| `save_state(staging, options)` | 调用方调用，存储模块执行（先 observe、再校验、再写盘） |
| 成功后 `context = std::move(staging)` | 调用方 |
| 返回选退课成功 | 调用方 |

- `save_state` 会修改**传入的 staging** 的 `round_opened`（observe 的副作用），
  不会改其他状态；失败时不写文件。正式共享的 `context` 在发布前不受影响。
- `load_state` / `restore_state` / `initialize_storage` 失败时都不替换 `out_context`。
- 因此保存失败时：丢弃 staging 即可，共享 `SystemContext` 原样不变。

## 4. 保存位置与启动

- 默认 `data/state.json`，相对**进程当前工作目录**。
- 指定路径：给 `StorageOptions::path` 赋值；模块不解析命令行/环境变量。
- `create_dirs = true` 时自动创建父目录。
- 首次运行：`initialize_storage()` 用种子数据初始化，**写盘成功后才发布**。

## 5. 失败结果与复现

| 情况 | 返回 | 复现 |
|---|---|---|
| 保存失败（父路径是文件） | `StorageIoError` | `options.path = "build/blocked/state.json"`，且 `build/blocked` 是文件 |
| 读取失败 | `StorageIoError` | 指向不存在的文件 |
| JSON 损坏 | `StorageParseError` | 写入 `{ not json` |
| schema 不认识 | `StorageSchemaMismatch` | 写入 `{"schema_version": 99}` |
| 业务约束不通过 | `StorageIntegrityError` | 超容量、重复账号、时间冲突、学分超限、课程无教学班等 |
| 学分累加溢出 | `StorageIntegrityError` | 一门课 `credits = 2147483647`，再选一门 5 学分课（总学分用 long long 累加） |
| 无备份 | `StorageBackupMissing` | 只保存一次后直接 `restore_state` |

约束不通过时 `save_state` 不会写盘；`load_state` 不会替换 `out_context`。
每种情况在 `persistence_tests.cpp` 都有可重复用例（关键检查用 `CHECK`，不依赖 assert）。

## 6. 恢复范围

已支持：`Active` / `Dropped` 选课记录、学生/教师/管理员的 `account_active`、
`round_opened`、课程与教学班（含 meetings）、`RegistrationRule`。
不保存：`fixed_now_`（测试专用）与派生值（余量、人数、课表，载入后现算）。

**备份安全**：恢复只读 `.bak`，写回主文件时不再覆盖 `.bak`；
主文件损坏或被占用导致恢复失败时，`.bak` 仍然有效，可以再次恢复。
