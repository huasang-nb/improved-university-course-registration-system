# 账号开户与 libsodium 构建接入（Zhu Ziliang）

- 最后更新：2026-11-28
- 验收范围：**仅 Windows x64 + MSVC 2022 + 官方动态包（dynamic）**
- 静态链接、Linux/GCC 都不在本阶段范围，配置阶段直接拒绝
- 状态：构建接入已实测；密码的生成/校验/本人改密公共服务由 **Yuhua** 负责（Argon2id），
  本模块只保证构建开关、凭据字段与持久化读写正确

## 1. 本轮代码事实

| 项 | 现状 |
|---|---|
| 凭据字段 | `Student` / `Teacher` / `Administrator` 末尾各有一个 `std::string password_hash{}` |
| 快照格式 | 采用 Kai 已验收的实现：**读 v1/v2，统一写 v2**；v2 写 `password_hash`，v1 缺该字段按空串处理 |
| 资料更新 | `update_student` / `update_teacher` 从原记录恢复 `password_hash`，只更新资料字段 |
| 新建账号 | `create_student` / `create_teacher` 清空调用方传入的哈希；密码只能走后续独立入口 |
| 密码服务 | 生成/校验/本人改密由 Yuhua 的公共服务提供（本机已通过校验测试，尚未提交）；未实现：凭据设置/重置入口、首次强制改密、激活令牌 |

## 2. 首个管理员怎样设置密码（建议）

不要把明文写进仓库、JSON 或命令行参数。

1. 由**父脚本**在调用初始化程序前设置 `TEAM4_BOOTSTRAP_ADMIN_PASSWORD`；
2. 初始化程序读取一次、Argon2id 哈希、写入管理员记录；
3. 该变量由**设置它的父脚本负责清除**（父脚本在子进程退出后立即 unset）；
   初始化程序不打印明文、不写日志。

需要"首次强制改密"的方案要新增 `must_change_password` 字段，范围待定。

## 3. 管理员新增人员如何衔接密码设置

创建时不写密码（已实现），账号处于"无密码 = 不可登录"。后续可选一次性密码或激活令牌，
范围待定；无论选哪种，设置/重置都必须是独立入口，`update_*` 已不承担该职责。

## 4. libsodium：官方动态包接入（默认关闭）

### 4.1 前置条件

| 条件 | 要求 |
|---|---|
| 平台 | Windows x64（`-A x64`） |
| 编译器 | MSVC 2022（v143） |
| CMake | **3.21 或更新**（用到 `TARGET_RUNTIME_DLLS` 自动拷贝 DLL；开启时检查版本） |
| 依赖包 | 官方 `libsodium-*-msvc.zip`，**dynamic** 目录齐全 |

### 4.2 启用命令（单行，可直接复制）

```powershell
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium
cmake --build build\cmake --config Debug
cmake --build build\cmake --config Release
```

`SODIUM_ROOT` 必须包含：

```
include\sodium.h
x64\Debug\v143\dynamic\libsodium.lib
x64\Debug\v143\dynamic\libsodium.dll
x64\Release\v143\dynamic\libsodium.lib
x64\Release\v143\dynamic\libsodium.dll
```

### 4.3 配置期检查（任一不满足直接失败）

| 检查 | 报错要点 |
|---|---|
| 只设了 `SODIUM_ROOT` 没开开关 | 明确提示要同时加 `-DCOURSE_REGISTRATION_USE_SODIUM=ON` |
| 非 MSVC / 非 64 位 | 提示本阶段只支持 Windows x64 + MSVC 2022 |
| CMake < 3.21 | 提示需要 3.21+（`TARGET_RUNTIME_DLLS`） |
| 目录不存在 | `SODIUM_ROOT 不存在：…` |
| 缺 `include\sodium.h` | 给出完整路径 |
| 缺 `.lib` 或 `.dll` | **逐个列出缺失文件全路径**（Debug、Release 分别检查） |
| 只有 static、没有 dynamic | 列出四个缺失的 dynamic 文件，并提示"本阶段只验收官方动态包" |
| 配置期链接失败 | 按 Debug / Release 分别指出失败的 `.lib` 路径 |

### 4.4 链接方式

`SHARED IMPORTED`：`IMPORTED_IMPLIB_*` 用于链接，`IMPORTED_LOCATION_*` 指向 DLL；
`course_registration_link_sodium()` 在构建后用 `TARGET_RUNTIME_DLLS` 把 `libsodium.dll`
拷到可执行文件旁。不定义 `SODIUM_STATIC`，不再涉及 `/MT` 切换，因此没有 LNK4098。

### 4.5 Linux / GCC

**未实现、未验证**。CMake 在非 MSVC 或非 64 位时直接报错终止，不提供 pkg-config 分支。

## 5. 规则状态（2026-11-21）

| 事项 | 状态 |
|---|---|
| 学生可选列表过滤停用教师及其班级 | **已确认，待实现和联调** |
| 选课入口拒绝直接提交这些班 | **已确认，待实现和联调**（Yuhua 在选课请求入口补资格检查） |
| 原班教师停用后能否改该班时间与容量 | **仍待讨论**；现有代码允许维护（容量下限、时间冲突等检查照常） |
| 历史选退课记录、课表、任课关系保留 | 已确认，已实现并有用例 |

## 6. 验收实测（2026-11-21，Windows x64 + MSVC 2022，CMake 3.30.5）

| 场景 | 结果 |
|---|---|
| 完整官方动态包，Debug + Release | 构建通过，**LNK4098 出现 0 次**；`ctest` 各 6/6（含 `libsodium_probe`） |
| 缺 Debug `libsodium.dll` | 配置阶段失败，报出缺失文件全路径 |
| 缺 Release `libsodium.dll` | 配置阶段失败，报出缺失文件全路径 |
| 只提供静态包 | 配置阶段失败，列出四个缺失的 dynamic 文件并提示改用官方动态包 |
| 默认构建（关闭 libsodium） | `ctest` 5/5；管理员 501 项、管理员持久化 134 项 |

`libsodium_probe` 用 Argon2id MIN 参数做一次哈希与校验，只在开关打开时编译并注册为 `add_test`。
