# 账号开户与 libsodium 构建接入（Zhu Ziliang）

- 最后更新：2026-11-21
- 状态：构建接入已实测；密码校验与凭据入口仍由 Yuhua 负责

## 1. 本轮代码事实

| 项 | 现状 |
|---|---|
| 凭据字段 | `Student` / `Teacher` / `Administrator` 末尾各有一个 `std::string password_hash{}` |
| 快照格式 | 采用 Kai 已验收的实现：**读 v1/v2，统一写 v2**；v2 写 `password_hash`，v1 缺该字段按空串处理 |
| 资料更新 | `update_student` / `update_teacher` 从原记录恢复 `password_hash`，只更新资料字段 |
| 新建账号 | `create_student` / `create_teacher` 清空调用方传入的哈希；密码只能走后续独立入口 |
| 未实现 | 凭据设置/重置入口、首次强制改密、激活令牌、`LoginService` 真实校验 |

## 2. 首个管理员怎样设置密码（建议）

不要把明文写进仓库、JSON 或命令行参数。

**方案 A（推荐，不需要新增字段）**

1. 由**父脚本**在调用初始化程序前设置 `TEAM4_BOOTSTRAP_ADMIN_PASSWORD`；
2. 初始化程序读取一次、Argon2id 哈希、写入管理员记录；
3. 该变量由**设置它的父脚本负责清除**（例如父脚本在子进程退出后立即 unset），
   初始化程序本身不打印明文、不写日志。

**方案 B（需要 `must_change_password`）**：首次初始化生成一次性密码，只打印一次，
首次登录强制改密。字段与流程尚未实现，范围待定。

## 3. 管理员新增人员如何衔接密码设置

创建时不写密码（已实现），账号处于"无密码 = 不可登录"。后续可选一次性密码或激活令牌，
范围待定；无论选哪种，设置/重置都必须是独立入口，`update_*` 已不承担该职责。

## 4. libsodium 构建接入（可选，默认关闭）

### MSVC 2022

```powershell
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium
cmake --build build\cmake --config Release
cmake --build build\cmake --config Debug
```

`SODIUM_ROOT` 指向官方 `libsodium-*-msvc.zip` 的解压目录。配置阶段会检查：

- `SODIUM_ROOT` 是否存在、`include/sodium.h` 是否存在；
- 架构（`CMAKE_SIZEOF_VOID_P` = 8 → `x64`，4 → `Win32`）对应的库文件是否存在；
- 用 `try_compile` **实际链接一次**，失败则直接报错，不会出现"配置成功、构建才失败"。

链接策略：

| 包内内容 | 行为 |
|---|---|
| 有 `dynamic` 目录（Debug/Release 都有） | **优先动态库**：`SHARED IMPORTED`，`IMPORTED_IMPLIB` 链接、`IMPORTED_LOCATION` 指向 DLL，构建后自动把 `libsodium.dll` 拷到可执行文件旁；不定义 `SODIUM_STATIC` |
| 只有 `static` | 定义 `SODIUM_STATIC`，并由 `course_registration_link_sodium()` 把使用者的 `MSVC_RUNTIME_LIBRARY` 设为 `/MT`，避免 **LNK4098**（静态库用 /MT 构建，与默认 /MD 混用会冲突） |

实测（2026-11-21，动态库与"仅静态库"两种包都试过）：Release / Debug 均链接通过，
`libsodium_probe` 在两种配置下都通过，无 LNK4098。

### Debian / Ubuntu

`libsodium-dev` 提供 `libsodium.pc`，CMake 不附带 `FindSodium` 模块，因此本工程优先用 pkg-config：

```bash
sudo apt install libsodium-dev pkg-config
cmake -S . -B build -DCOURSE_REGISTRATION_USE_SODIUM=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

查找顺序：`pkg_check_modules(SODIUM IMPORTED_TARGET libsodium)` → `find_package(Sodium)` →
报错。都找不到时的提示会说明装 `libsodium-dev`/`pkg-config` 或改用 `SODIUM_ROOT`。

### 其它目标

`libsodium_probe`（`automated_tests/libsodium_probe_tests.cpp`）只在开关打开时编译并注册为
`add_test`，用 Argon2id MIN 参数做一次哈希+校验，验证"能编译、能链接、语义正确"。

## 5. 规则状态（2026-11-21）

| 事项 | 状态 |
|---|---|
| 学生可选列表过滤停用教师及其班级 | **已确认，待实现和联调** |
| 选课入口拒绝直接提交这些班 | **已确认，待实现和联调**（Yuhua 在选课请求入口补资格检查） |
| 原班教师停用后能否改该班时间与容量 | **仍待讨论**；现有代码允许维护（容量下限、时间冲突等检查照常） |
| 历史选退课记录、课表、任课关系保留 | 已确认，已实现并有用例 |

## 6. 测试结果（2026-11-21）

| 配置 | 结果 |
|---|---|
| 默认构建（不带 libsodium） | ctest 5/5：管理员 501 项、管理员持久化 134 项 |
| `-DCOURSE_REGISTRATION_USE_SODIUM=ON` + 动态库 | ctest 6/6（Release 与 Debug） |
| 同上但包内只有静态库 | ctest 6/6（Release 与 Debug），无 LNK4098 |
| 传不存在的 `SODIUM_ROOT` | 配置阶段失败，报 `SODIUM_ROOT 不存在：…` |
