# third_party 第三方依赖

本目录只放 HTTP 接入所需的两份单头文件依赖。版本、来源与校验值记录在此，升级时同步更新本文件并在提交信息里说明原因。

| 依赖 | 版本 | 文件 | 来源 | SHA-256 |
|---|---|---|---|---|
| cpp-httplib | v0.67.0 | `httplib/httplib.h` | https://github.com/yhirose/cpp-httplib/releases/tag/v0.67.0 | `3EC770608B295D12BF7BB6CCA47F4B8249F6E15261E2587BA0B288F4BFC397CE` |
| nlohmann/json | v3.11.3 | `nlohmann/nlohmann/json.hpp` | https://github.com/nlohmann/json/releases/tag/v3.11.3 | `9BEA4C8066EF4A1C206B2BE5A36302F8926F7FDC6087AF5D20B417D0CF103EA6` |

许可证：cpp-httplib 为 MIT（`httplib/LICENSE`），nlohmann/json 为 MIT（`nlohmann/LICENSE.MIT`）。两者都允许随项目分发，保留原许可证文件即可。

## 为什么放进仓库而不是要求各自安装

与 libsodium 不同，这两个库都是单头文件、无二进制、无 ABI 问题，仓库里放 1.8 MB 换来"任何机器 clone 下来就能编"，比让三个人各自找路径、各自对版本更省事。libsodium 仍是外部官方动态包，理由见 `documentation/account_onboarding_and_libsodium_build.md`。

## 使用方式

CMake 中把两个目录作为 `target_include_directories` 加入需要 HTTP 的目标：

```cmake
target_include_directories(http_teacher_admin_routes PRIVATE
    third_party/httplib)
```

nlohmann 的头文件路径按 `#include <nlohmann/json.hpp>` 组织，因此包含目录是 `third_party/nlohmann`。

## 升级步骤

1. 下载新版本单头文件，覆盖对应文件。
2. 用 `Get-FileHash -Algorithm SHA256` 计算新哈希，更新上表。
3. 编译全部目标并运行 `ctest`，确认 HTTP 测试仍通过。
4. 提交信息写清"升级原因 + 是否改动了使用到的 API"。
