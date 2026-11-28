#pragma once

#include "login_service.hpp"
#include "persistence_service.hpp"

namespace course_registration {

// 只返回设置密码所需的账号信息，密码哈希仍保留在 SystemContext 中。
struct PasswordAccount {
    UserIdentity identity{};
    bool password_set = false;
};

// 在三种角色中查找唯一的账号。账号去首尾空白并转小写后比较。
// 停用账号也可以设置密码，但设置密码本身不会把账号改成启用。
OperationResult find_password_account(const SystemContext& context,
                                      const std::string& account_number,
                                      PasswordAccount& account);

// 用已保存的 Argon2id 哈希检查密码。空密码、空哈希和损坏的哈希都返回 false。
bool verify_password_hash(const std::string& password_hash,
                          std::string_view password);

// 把真实密码检查接到现有 LoginService。返回的函数始终读取 context 当前的账号资料。
// context 必须一直存在。使用多线程时，使用这个函数的代码负责协调读取和账号修改。
// 后续 HTTP 也可以先复制账号资料，再在锁外调用 verify_password_hash。
PasswordVerifier make_stored_password_verifier(const SystemContext& context);

// 新密码至少 8 位，只能使用可见的 ASCII 字符，并且同时包含大写、小写和数字。
// 登录时不使用这个规则检查旧密码，避免旧账号无法登录后修改密码。
OperationResult validate_new_password(std::string_view password);

// 下面的准备结果只在服务器内部传递，不能从请求体构造，也不能返回给页面。
// 把较慢的密码计算和实际保存分开，HTTP 层可以在不持业务锁时计算哈希。
struct PreparedPasswordChange {
    UserIdentity actor{};
    std::string previous_hash;
    std::string new_hash;
};

std::string stored_password_hash(const SystemContext& context,
                                 const UserIdentity& actor);
OperationResult prepare_password_change(const SystemContext& snapshot,
                                         const UserIdentity& actor,
                                         std::string_view current_password,
                                         std::string_view new_password,
                                         PreparedPasswordChange& prepared);

// 调用方在共同业务锁内保存。期间密码若已被其他请求修改，这次操作会被拒绝。
OperationResult apply_password_change(SystemContext& context,
                                       const StorageOptions& storage,
                                       const PreparedPasswordChange& prepared);

// 单线程调用或已由调用方协调访问时使用。函数内部保存，成功后才更新 context。
OperationResult change_password(SystemContext& context,
                                 const StorageOptions& storage,
                                 const UserIdentity& actor,
                                 std::string_view current_password,
                                 std::string_view new_password);

// 暂行的本机密码设置操作。只修改指定账号的 password_hash，并保存整份数据。
// 已设置过密码时，需要明确传入 replace_existing=true 才会替换。
// 保存成功后才把副本放回 context，保存失败时保留原来的内存数据。
// 使用这项操作前，需要停止其他使用同一数据文件的程序。
// libsodium 初始化或哈希计算失败时抛出 runtime_error，已有数据保留原样。
OperationResult set_account_password(SystemContext& context,
                                     const StorageOptions& storage,
                                     const std::string& account_number,
                                     std::string_view password,
                                     bool replace_existing = false);

}  // namespace course_registration
