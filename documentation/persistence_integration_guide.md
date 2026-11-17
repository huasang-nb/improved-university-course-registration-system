# 选退课接入持久化：联调说明

> 分支 `feature/yikai-persistence`。模块：`persistence_service.hpp/.cpp`、`persistence_tests.cpp`。
> 共享头改动见 `interface_change_request.md` 第七节。

## 1. 代码版本与编译

- 分支：`feature/yikai-persistence`
- 新增文件：`header_files/persistence_service.hpp`、`source_code/persistence_service.cpp`、
  `automated_tests/persistence_tests.cpp`、`documentation/persistence_design.md`、本文件。
- 编译/测试（Windows + g++，已验证）：

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\build_and_test.ps1
  # 末行应为 ALL BUILD AND TESTS PASSED
  .\build\persistence_tests.exe
  ```

- CMake 目标：`persistence_service` / `persistence_tests`（已加，但本机未装 cmake，CMake 路径未实测）。

## 2. 接口与最小示例

```cpp
struct StorageOptions {
    std::string path = "data/state.json";
    bool create_dirs = true;
};

OperationResult initialize_storage(const StorageOptions&, SystemContext& out);  // 启动
OperationResult load_state(const StorageOptions&, SystemContext& out);          // 载入
OperationResult save_state(const SystemContext&, const StorageOptions&);        // 保存
OperationResult restore_state(const StorageOptions&, SystemContext& out);       // 从 .bak 恢复
SystemContext make_seed_context();
```

```cpp
StorageOptions options;                       // data/state.json
SystemContext context = make_seed_context();  // 换成现有 context
initialize_storage(options, context);

SystemContext staging = context;              // 1. 临时状态
// 2. 在 staging 上做业务修改（enroll / drop）
OperationResult saved = save_state(staging, options);   // 3. 保存
if (saved.code == ResultCode::Success) {
    context = std::move(staging);             // 4. 成功才提交
}

SystemContext loaded = make_seed_context();   // 5. 加载回来
load_state(options, loaded);
restore_state(options, loaded);               // 或从 .bak 恢复
```

## 3. 调用职责与状态变化

| 步骤 | 谁做 |
|---|---|
| 复制 `staging = context` | 调用方 |
| 在 staging 上调用 `enroll()` / `drop()` | 调用方（业务检查都在这一步） |
| `save_state(staging, options)` | 调用方调用，存储模块执行 |
| 成功后 `context = std::move(staging)` | 调用方 |
| 返回选退课成功 | 调用方 |

- `save_state` 的参数是 `const SystemContext&`，**成功或失败都不修改**传入的 context。
- `load_state` / `restore_state` / `initialize_storage` 失败时**都不替换** `out_context`。
- 因此保存失败时：共享 `SystemContext` 原样不变，直接返回失败原因即可。

## 4. 保存位置与启动

- 默认 `data/state.json`，相对**进程当前工作目录**。
- 指定路径：给 `StorageOptions::path` 赋值；模块不解析命令行/环境变量，由调用方决定最终路径。
- `create_dirs = true` 时自动创建父目录。
- 首次运行：`initialize_storage()` 文件不存在 → 用 `make_seed_context()` 初始化并立即落盘。

## 5. 失败结果与复现

| 情况 | 返回 | 复现 |
|---|---|---|
| 保存失败 | `StorageIoError` | `options.path = "build/blocked/state.json"`，且 `build/blocked` 是文件 |
| 读取失败 | `StorageIoError` | 指向不存在的文件 |
| JSON 损坏 | `StorageParseError` | 写入 `{ not json` |
| schema 不认识 | `StorageSchemaMismatch` | 写入 `{"schema_version": 99}` |
| 校验失败 | `StorageIntegrityError` | 重复 `student_number`（大小写不同也算）或引用不存在的教学班 |
| 无备份 | `StorageBackupMissing` | 只保存一次后直接 `restore_state` |

`automated_tests/persistence_tests.cpp` 对每种情况都有可重复的用例。

## 6. 恢复范围

已支持：`Active` / `Dropped` 选课记录、学生/教师/管理员的 `account_active`、
`round_opened`、课程与教学班（含 meetings）、`RegistrationRule`。
不保存：`fixed_now_`（测试专用）与派生值（余量、人数、课表，载入后现算）。
