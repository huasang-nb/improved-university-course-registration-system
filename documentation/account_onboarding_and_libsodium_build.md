# 账号开户与 libsodium 构建接入（Zhu Ziliang）

- 最后更新：2026-11-21
- 验收范围：**仅 Windows x64 + MSVC 2022**；Linux/GCC 未验证、未实现
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
3. 该变量由**设置它的父脚本负责清除**（父脚本在子进程退出后立即 unset）；
   初始化程序不打印明文、不写日志。

**方案 B（需要 `must_change_password`）**：首次初始化生成一次性密码，只打印一次，
首次登录强制改密。字段与流程尚未实现，范围待定。

## 3. 管理员新增人员如何衔接密码设置

创建时不写密码（已实现），账号处于"无密码 = 不可登录"。后续可选一次性密码或激活令牌，
范围待定；无论选哪种，设置/重置都必须是独立入口，`update_*` 已不承担该职责。

## 4. libsodium 构建接入（可选，默认关闭）

### 4.1 启用（MSVC 2022、x64）

```powershell
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium
cmake --build build\cmake --config Debug
cmake --build build\cmake --config Release
```

`SODIUM_ROOT` 指向官方 `libsodium-*-msvc.zip` 的解压目录，需要包含
`include\sodium.h`，以及 `x64\{Debug,Release}\v143\{dynamic,static}\libsodium.lib`。

### 4.2 配置期校验（任一不满足直接失败）

| 检查 | 失败提示 |
|---|---|
| 只设了 `SODIUM_ROOT` 没开开关 | `设置了 SODIUM_ROOT 但没有开启 COURSE_REGISTRATION_USE_SODIUM=ON` |
| 非 MSVC 或非 64 位 | 明确写出"本阶段只支持 Windows x64 + MSVC 2022" |
| 目录不存在 | `SODIUM_ROOT 不存在：…` |
| 缺头文件 | `SODIUM_ROOT 缺少头文件：…/include/sodium.h` |
| 库文件不全 | 列出期望的 dynamic/static 路径 |
| 链接失败 | Debug 与 Release 各做一次 `try_compile`，失败即报错 |

### 4.3 链接策略（消除 LNK4098）

| 包内内容 | 行为 |
|---|---|
| `dynamic` 的 Debug/Release 都有 | **优先动态库**：`SHARED IMPORTED`，`IMPORTED_IMPLIB` 链接、`IMPORTED_LOCATION` 指向 DLL，构建后自动把 `libsodium.dll` 拷到 exe 旁；不定义 `SODIUM_STATIC` |
| 只有 `static` | 定义 `SODIUM_STATIC`，`course_registration_link_sodium()` 把使用者切到 `/MT`，与静态库的运行库一致，避免 LNK4098 |

### 4.4 Linux / GCC

**本阶段未实现、未验证**，CMake 在非 MSVC 下会直接报错终止，不提供 pkg-config 分支。
后续若需要，再按 Debian `libsodium-dev`（提供 `libsodium.pc`）单独设计并实测。

## 5. 规则状态（2026-11-21）

| 事项 | 状态 |
|---|---|
| 学生可选列表过滤停用教师及其班级 | **已确认，待实现和联调** |
| 选课入口拒绝直接提交这些班 | **已确认，待实现和联调**（Yuhua 在选课请求入口补资格检查） |
| 原班教师停用后能否改该班时间与容量 | **仍待讨论**；现有代码允许维护（容量下限、时间冲突等检查照常） |
| 历史选退课记录、课表、任课关系保留 | 已确认，已实现并有用例 |

## 6. 实测结果（2026-11-21，Windows x64 + MSVC 2022）

| 场景 | 结果 |
|---|---|
| 默认构建（不带 libsodium） | `ctest` 5/5；管理员 501 项、管理员持久化 134 项 |
| 包内含动态库，Debug + Release | 构建通过，**0 次 LNK4098**；`ctest` 各 6/6 |
| 仅静态库（/MT），Debug + Release | 构建通过，**0 次 LNK4098**；`ctest` 各 6/6 |
| 不存在的 `SODIUM_ROOT` | 配置阶段失败 |
| 空目录（无头文件） | 配置阶段失败 |
| 只有头文件、没有库 | 配置阶段失败 |
| 设了 `SODIUM_ROOT` 但开关关闭 | 配置阶段失败 |

`libsodium_probe` 用 Argon2id MIN 参数做一次哈希与校验，只在开关打开时编译并注册为 `add_test`。
