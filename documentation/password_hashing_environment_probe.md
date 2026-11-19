# 密码哈希：本机构建环境探测与初始密码建议

- 日期：2026-11-19
- 针对问题：①Zhu 的构建环境能否接入 libsodium；②管理员新增学生/教师时初始密码怎么设置
- 结论：**MSVC 2022 可以接入并实测通过；本机 GCC 8.1 不行（本来也不支持持久化模块）**

## 1. 实测过程（未改动仓库代码）

用 libsodium 官方预编译包 `libsodium-1.0.20-msvc.zip`（x64 / v143 / static），
编译一个独立探针（不进入仓库）：

```
cl /nologo /utf-8 /std:c++17 /EHsc /DSODIUM_STATIC /I <sodium>\include probe_argon2.cpp ^
   /link /LIBPATH:<sodium>\x64\Release\v143\static libsodium.lib
```

结果（实机输出）：

```
hash=$argon2id$v=19$m=65536,t=2,p=1$vw7wUiMeN5lJDvcbxidAiA$ku63y22gMKl4WBGfcoKIDVwOEL7ll5A9R16gcPs5ykI
hash_len=97
verify_ok=0
verify_wrong=-1
```

要点：

- `crypto_pwhash_str()` 产出的是**自描述字符串**（算法、版本、m/t/p 参数、盐、哈希都在里面），
  长度 97，可直接作为 JSON 的一个字符串字段，不需要单独存 salt 或参数列。
- `crypto_pwhash_str_verify()` 正确密码返回 0，错误密码返回 -1。
- 必须定义 `SODIUM_STATIC`，否则头文件按 dllimport 声明，链接静态库会报 `__imp_*` 未解析。
- 交互参数 `OPSLIMIT_INTERACTIVE`（m=64 MiB, t=2）在本机约几十毫秒，登录可接受；
  单元测试若大量哈希，建议用 `OPSLIMIT_MIN/MEMLIMIT_MIN` 的测试配置，避免拖慢 CI。

## 2. 环境结论

| 环境 | 能否接入 libsodium | 说明 |
|---|---|---|
| MSVC 2022（本项目主力环境） | **可以，已实测** | 用官方 MSVC 预编译包即可，无需 vcpkg |
| 本机 GCC 8.1（MinGW-w64） | **不可以** | 无 `<filesystem>`，持久化模块本来就要 GCC 9+；也没有 MinGW 版 libsodium |
| GCC 9+（Kai/Yuhua 环境） | 支持范围内，未在本机实测 | 需 `apt install libsodium-dev` 或源码构建；CMake 用 `find_package(Sodium)` |
| 本机包管理器 | 无 vcpkg / nuget / msys2 | 只有 winget；建议不依赖系统级安装 |

**集成建议（不提交二进制）**：CMake 增加可选依赖，找不到时**明确报错**而不是静默降级：

```cmake
find_package(Sodium QUIET)
if (NOT Sodium_FOUND AND DEFINED SODIUM_ROOT)
    # 允许 -DSODIUM_ROOT=<解压目录> 指向预编译包
endif()
if (NOT Sodium_FOUND AND NOT DEFINED SODIUM_ROOT)
    message(FATAL_ERROR "需要 libsodium：安装 libsodium-dev，或 -DSODIUM_ROOT=<预编译包目录>")
endif()
```

并且要求 `SODIUM_STATIC`（MSVC 预编译包）或动态库两种方式都在文档里写清。

## 3. 管理员新增学生/教师：初始密码怎么设置

**原则：管理员不该知道、也不该决定别人的长期密码。**

推荐两种方案（B 更适合批量导入）：

**A. 系统生成一次性初始密码（改动最小）**

1. 创建成功时由系统生成 12–16 位随机密码（`crypto_pwhash` 之外用 `randombytes_buf` 或
   `sodium_bin2base64` 生成），只在创建结果里**返回一次**给管理员转交；
2. 账号记 `must_change_password = true`；
3. 首次登录成功、改密完成后才清掉该标志；
4. 管理员界面不显示、不可查、不写日志；`update_student/update_teacher` **不得携带密码字段**，
   避免"改个专业把密码清空"。

**B. 激活令牌（更适合"管理员不该知道密码"）**

1. 创建时生成一次性 token（带过期时间），只存 token 的哈希；
2. 学生/教师凭 token 自己设置首次密码，token 用后作废、过期作废；
3. 未激活的账号登录一律拒绝，提示走激活流程。

**旧快照与演示账号**

- `password_hash` 为空 = **尚未设置密码**：登录必须拒绝并提示重置/激活，绝不能"空密码放行"；
- 演示/种子账号用固定演示密码（例如 `Demo#2026`），只在首次初始化时哈希写入，文档标注
  "仅演示数据"，启动日志不打印明文；
- "重置密码"是独立操作：生成新的一次性密码并把 `must_change_password` 置 true，
  不复用旧哈希。

**建议字段（供 Kai 参考，属于存储侧设计）**：`password_hash`（字符串，Argon2id 完整串）、
`must_change_password`（布尔）、可选 `password_updated_at`；三个角色不要各加一份，
建议抽公共凭据结构或凭据表。

## 4. 我这边需要配合的事

1. 管理员创建学生/教师时接上"生成一次性密码 → 返回一次 → 置 must_change_password"，
   并保证 `update_*` 不触碰密码字段；
2. CMake 增加 libsodium 可选依赖与清晰的报错信息；
3. 测试用低强度 Argon2 参数，避免拖慢测试；正常构建用交互参数。

注：本节只是环境探测与建议，**尚未写入代码**。密码校验逻辑按分工由 Yuhua 负责。
