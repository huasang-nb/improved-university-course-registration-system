# 账号开户与 libsodium 构建接入（Zhu Ziliang，2026-11-19）

## 1. 本轮已落地的代码事实

| 项 | 现状 |
|---|---|
| 凭据字段 | `Student` / `Teacher` / `Administrator` 各新增 `std::string password_hash`（放在结构体末尾，聚合初始化向后兼容） |
| 资料更新 | `update_student` / `update_teacher` 只更新资料字段，**从原记录保留 `password_hash`**；调用方传空串或别的哈希都不会覆盖 |
| 新建账号 | `create_student` / `create_teacher` **不接受**调用方给的哈希，统一清空；密码只能走后续独立入口 |
| JSON 快照 | 三类人员都写入 `password_hash`（仅在非空时写）；读取时该字段**可选**，没有就是空串，旧快照可直接加载 |
| 测试 | `persistent_administration_tests` 中新增凭据保护用例（见第 4 节） |
| 未实现 | 凭据设置/重置入口、`must_change_password`、首次强制改密、激活令牌、`LoginService` 真实校验（由 Yuhua 负责） |

## 2. 首个管理员怎样在本地设置密码（建议）

不要把明文写进仓库、JSON 或脚本参数。推荐两种可接受做法：

**A. 初始化脚本读一次环境变量（推荐）**

1. 管理员手动设置环境变量，例如 `TEAM4_BOOTSTRAP_ADMIN_PASSWORD`（当前会话有效）；
2. 执行一次性初始化程序：读变量 → Argon2id 哈希 → 写入管理员记录 → **立即清空该环境变量**；
3. 程序中不打印明文、不落日志；初始化完成后提示"密码已写入，请删除该变量"。

**B. 首次启动生成一次性密码**

1. 首次初始化时生成随机密码，**只在控制台打印一次**；
2. 同时置 `must_change_password = true`（该字段尚未实现，等团队定范围）；
3. 管理员首次登录必须改密。

两种都可以；A 不需要改字段，B 需要 `must_change_password` 与强制改密流程，范围更大。

## 3. 管理员新增人员怎样衔接后续密码设置

现有代码已经保证"创建时不写密码"，因此新增人员处于**无密码 = 不可登录**状态。后续接法（范围待定）：

- **一次性密码**：创建结果返回一次，管理员转交，首次登录强制改密；
- **激活令牌**：创建时生成一次性 token（只存哈希、带过期），本人凭 token 设密码；
- 在此之前，登录对空 `password_hash` 必须**明确拒绝**（提示激活/重置），不能当空密码放行。

无论选哪种，**"设置/重置密码"都必须是独立入口**（本轮的 `update_*` 已不再承担这件事）。

## 4. libsodium 的 CMake 接入（已准备好，默认关闭）

CMakeLists 新增开关与依赖解析：

```powershell
# MSVC：指向官方 libsodium-*-msvc.zip 解压目录（含 include/ 与 x64/Release/v143/static）
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 ^
      -DCOURSE_REGISTRATION_USE_SODIUM=ON -DSODIUM_ROOT=C:\path\to\libsodium

# GCC 9+：安装 libsodium-dev 后
cmake -S . -B build -DCOURSE_REGISTRATION_USE_SODIUM=ON
```

行为：

- `SODIUM_ROOT` 存在时建立导入目标 `sodium::sodium`，链接静态库并定义 `SODIUM_STATIC`
  （不定义会按 dllimport 声明，出现 `__imp_*` 未解析）；
- 否则 `find_package(Sodium)`，找不到时给出可读报错，提示安装 `libsodium-dev` 或用 `SODIUM_ROOT`；
- 链接目标通过变量 `${COURSE_REGISTRATION_SODIUM_TARGET}` 暴露，供后续 `LoginService` 的
  密码哈希目标 `target_link_libraries(... ${COURSE_REGISTRATION_SODIUM_TARGET})` 使用。

**默认 OFF**：本分支没有任何代码使用 libsodium，打开开关不会改变现有构建；团队确认后由
LoginService 侧接入真实校验。本机已用该包做过独立探针（Argon2id 哈希/校验通过），
预编译包不提交到仓库。

## 5. 测试结果（2026-11-19）

| 配置 | 结果 |
|---|---|
| CMake Release | 5/5 通过（管理员 501 项、持久化入口 121 项） |
| CMake Debug | 见提交说明 |
| 反向验证 | 去掉"保留原哈希"逻辑后，凭据测试打印 6 处 FAIL 并返回 exit=1 |
