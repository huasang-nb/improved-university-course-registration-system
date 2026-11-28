# 本机密码设置与真实密码校验

## 方案状态

本机密码设置工具目前作为暂行实现使用，最终的账号初始化、密码重置和首次改密政策仍待讨论。后续确定其他办法时，可以替换这个工具，继续复用现有 `password_hash` 字段、Argon2id 校验和保存入口。

密码基础服务已增加公共 HTTP 会话和本人改密接入，见 [公共会话说明](session_integration.md)。完整三端页面仍需在成员分支整合后验证。

## 已提供的内容

- `password_service` 为三种角色生成并校验 Argon2id 哈希。
- `make_stored_password_verifier(context)` 可以直接接到已有的 `LoginService`。
- `password_setup` 在本机控制台隐藏输入密码，并要求再输入一次。
- 只有文件保存成功，才会更新传入的 `SystemContext`。保存失败时保留原来的内存数据和主文件。

工具只处理已经存在的数据文件和账号。停用账号可以先设置密码，但设置操作保留原启停状态，停用账号仍然无法登录。
新密码至少 8 位，只允许英文字母、数字和英文半角符号，必须同时包含大写字母、小写字母和数字，符号可选。
中文和所有空白字符都会被拒绝，不会自动删除首尾空格。旧密码仍按原哈希校验。密码过期和强制首次改密没有在本阶段增加。

## 编译

沿用 Windows x64、MSVC 2022 和官方 libsodium 动态包。启用 libsodium 需要 CMake 3.21 或更新版本。包目录应包含 Debug 和 Release 的 `.lib`、`.dll` 及 `include/sodium.h`。

在仓库根目录执行，替换示例中的依赖目录。

```powershell
cmake -S . -B build/password -G "Visual Studio 17 2022" -A x64 `
  -DCOURSE_REGISTRATION_USE_SODIUM=ON `
  -DSODIUM_ROOT="C:/path/to/libsodium"
cmake --build build/password --config Debug --parallel 4
cmake --build build/password --config Release --parallel 4
```

启用后生成 `password_setup.exe` 和 `password_service_tests.exe`，所需 DLL 会复制到可执行文件旁边。默认关闭 libsodium 的原有构建继续保留。

## 为账号设置密码

1. 先停止所有使用同一 JSON 文件的程序，包括控制台演示。这个工具属于能够访问数据文件的本机维护操作，使用者应有相应的 Windows 文件权限。
2. 保留一份数据备份。读取 v1 数据后再次保存会写为 v2，旧版程序无法读取 v2。
3. 确认文件中存在需要设置密码的账号。工具会按去首尾空白并转小写的规则，在三种角色中查找唯一账号。
4. 执行下面的命令。账号和数据路径可以通过参数传入，密码在程序提示后输入。
5. 输入两次相同密码。控制台不显示密码字符，成功后输出 `Password saved`。

```powershell
.\build\password\Debug\password_setup.exe `
  --state ./build/password_demo/state.json --account A2020001
```

如果要在独立演示文件中练习，可先运行已有演示生成示例账号。

```powershell
.\build\password\Debug\registration_storage_demonstration.exe `
  --state ./build/password_demo/state.json status S2023001
```

示例数据包含管理员 `A2020001`、学生 `S2023001`、教师 `T2015007`。工具不会自动创建新账号或缺失的状态文件。

账号已有密码时，默认拒绝覆盖。确实需要在本机替换时，明确添加 `--replace`，然后重新输入两次密码。

```powershell
.\build\password\Debug\password_setup.exe `
  --state ./build/password_demo/state.json --account S2023001 --replace
```

`--replace` 是暂行工具的显式覆盖确认，在线管理员重置的权限和流程仍待讨论。密码不通过命令行参数传递，也不写入 JSON 或程序日志。控制台输入缓冲区最多容纳 4096 个 UTF-16 字符，这个限制只属于本机工具。

## 接到现有登录服务

```cpp
#include "password_service.hpp"

// context 已通过 load_state 或 initialize_storage 取得当前数据。
// 真实校验器会读取其中的 password_hash，再检查输入密码。
LoginService login(context, make_stored_password_verifier(context));
const LoginResult result = login.login(account_number, password);
```

`LoginService` 继续负责唯一账号匹配、角色识别和启停检查。校验器检查正确角色及编号对应的当前哈希。空哈希、错误密码、损坏哈希和停用账号都不能取得登录身份。不同角色仍可使用相同数字 ID。

校验器引用的 `context` 在服务使用期间必须一直存在。未来增加 HTTP 并发时，需要由调用代码协调共享数据读取与修改。也可以先复制当前哈希，再在业务锁外调用 `verify_password_hash`，校验完成后重新核对账号资料。

## 文件占用和失败处理

工具在读取 JSON 前独占打开 `<state>.password.lock`。第二个密码设置工具会被拒绝，避免两边基于同一份旧文件保存而覆盖彼此的修改。锁文件会留在磁盘上，程序退出后 Windows 会释放占用句柄，下次可以继续使用。

现有其他程序还没有采用这个锁文件协议，所以运行工具前仍需要停止它们。这个实现没有替代以后 HTTP 服务中的共同业务锁。

两次密码不一致、账号不存在、账号重名、空密码或未允许覆盖时，数据文件保留原样。实际保存失败时，原内存和主文件保留。保存过程会先更新 `.bak` 再替换主文件，后续步骤失败时，备份可能已经变化。

退出码为 `0` 表示设置成功，`1` 表示读取、输入、校验或保存失败，`2` 表示命令参数错误。`--help` 返回 `0` 并显示用法。控制台输入时可用 Escape 或 Ctrl+C 取消，取消后返回失败。

## 验证

```powershell
ctest --test-dir build/password -C Debug --output-on-failure
ctest --test-dir build/password -C Release --output-on-failure `
  -R '^(password_service|password_setup_help)$'
```

项目的 `run_registration_storage_acceptance.ps1` 已将两个新增测试加入 Release 清单。测试覆盖真实登录、三角色同数字 ID、哈希保存重载、显式覆盖、停用状态保留、保存失败和重试，以及真实身份调用选退课。

`password_setup_help` 验证程序的帮助入口。隐藏输入、两次输入不一致和两个工具争用文件的检查，需要通过实际交互控制台运行。测试、演示和实际使用各自采用独立文件。

## 依赖说明

哈希使用 libsodium 的 Argon2id 算法和交互式计算参数，随机盐和校验所需参数一起写入哈希字符串。输入缓冲区使用 `sodium_memzero` 清理。

- [libsodium 密码哈希接口](https://doc.libsodium.org/password_hashing/default_phf)
- [libsodium 内存清理说明](https://doc.libsodium.org/memory_management)
